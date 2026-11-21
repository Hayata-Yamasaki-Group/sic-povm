// SPDX-License-Identifier: AGPL-3.0-or-later
#include "producer_cases.hpp"

#include <sys/resource.h>

#include <fstream>
#include <iostream>
#include <optional>
#include <source_location>
#include <system_error>

#include "../fixtures/fixture.hpp"
#include "access/range_cache.hpp"
#include "access/source_arguments.hpp"
#include "sic/sicx/finalize.hpp"
#include "sic/sicx/json.hpp"
#include "sic/sicx/text.hpp"
#include "sicx/codec/ball.hpp"
#include "sicx/codec/integer.hpp"
#include "sicx/codec/rescaling_rule.hpp"
#include "sicx/codec/unrescaled.hpp"
#include "sicx/container/directory.hpp"
#include "sicx/container/leaf_index.hpp"
#include "sicx/container/producer_writer.hpp"
#include "sicx/container/rescaling_input.hpp"
#include "sicx/exact/exact.hpp"
#include "sicx/exact/tower.hpp"
#include "sicx/io/io.hpp"
#include "sicx/model/branches.hpp"
#include "sicx/model/unit_group.hpp"
#include "support/sha256.hpp"
#include "transport/fetch.hpp"
#include "transport/parts.hpp"
#include "transport/repair.hpp"

namespace fs = std::filesystem;
using namespace sic::sicx;
using sic::ErrorKind;

namespace {
void check(bool ok, const std::string& why) {
  if (!ok) throw std::runtime_error(why);
}
template <class F>
void expect(ErrorKind kind, F&& operation,
            const std::source_location& location = std::source_location::current()) {
  try {
    operation();
  } catch (const sic::Error& error) {
    check(error.kind() == kind, std::string("wrong category at ") +
                                    std::to_string(location.line()) + ": " + error.what());
    return;
  }
  throw std::runtime_error("expected categorized failure at " + std::to_string(location.line()));
}

ByteBuffer bytes(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  check(bool(input), "open test file");
  return {std::istreambuf_iterator<char>(input), {}};
}
void write(const fs::path& path, const ByteBuffer& data) {
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char*>(data.data()), data.size());
  check(bool(output), "write test file");
}
}  // namespace

void producer_failures(const Reader& good, const fs::path& work) {
  const std::vector<unsigned> degrees{2};
  expect(ErrorKind::invalid_format, [&] { own_weight({"wsq", 1}, degrees); });
  expect(ErrorKind::invalid_format, [&] { own_weight({"fcoef", 0}, degrees); });
  expect(ErrorKind::invalid_format, [&] { own_weight({"unknown", 0, -1, 0}, degrees); });
  Meta meta;
  meta.degrees = degrees;
  meta.width = {1, 2};
  sic::support::flint::Fmpz x, y;
  fmpz_one(x);
  const LeafSource empty = [](const LeafVisitor&) {};
  expect(ErrorKind::invalid_format, [&] {
    RescalingCollector collector(meta);
    collector.add({"rep", 2}, empty);
  });
  expect(ErrorKind::invalid_format, [&] {
    RescalingCollector collector(meta);
    collector.add({"rep", 1}, [&](const LeafVisitor& visit) { visit(2, x, y); });
  });
  expect(ErrorKind::invalid_format, [&] {
    element_stats(meta, {"rep", 1}, [&](const LeafVisitor& visit) {
      visit(0, x, y);
      visit(0, x, y);
    });
  });
  expect(ErrorKind::invalid_format, [&] {
    element_stats(meta, {"rep", 1}, [&](const LeafVisitor& visit) { visit(2, x, y); });
  });
  const StreamOptions options;
  expect(ErrorKind::resource,
         [&] { ProducerWriter writer(work / "bad-target.sicx", good.meta(), options, 0); });
  expect(ErrorKind::io,
         [&] { merge_element_parts(work / "missing-part.sicx", {work / "absent-part"}, options); });
  for (bool existing : {false, true}) {
    ProducerWriter writer(work / (existing ? "existing-side-writer" : "missing-side-writer"),
                          good.meta(), options);
    const auto side = work / "existing-side";
    std::ofstream(side.string() + ".elements.json") << "exists";
    expect(ErrorKind::io, [&] {
      writer.finish_part(existing ? side : work / "absent-parent" / "side", "source");
    });
  }
  // Changed producer callbacks remain internal contract violations.
  for (bool scale_change : {false, true}) {
    ProducerWriter writer(work / (scale_change ? "changed-scale" : "changed-level"), meta, options);
    unsigned pass = 0;
    bool caught = false;
    try {
      writer.add({"rep", 1}, [&](const LeafVisitor& visit) {
        ++pass;
        fmpz_set_ui(x, scale_change && pass == 1 ? 2 : 1);
        visit(!scale_change && pass == 2 ? 1 : 0, x, y);
      });
    } catch (const sic::Error&) {
      throw std::runtime_error("internal source contract lost its standard exception");
    } catch (const std::logic_error&) {
      caught = true;
    }
    check(caught, "changed source must fail with logic_error");
  }
  StreamOptions part_options;
  part_options.part = true;
  const auto part = work / "held-part";
  for (bool order_failure : {false, true}) {
    ProducerWriter writer(work / (order_failure ? "order-writer" : "offset-writer"), good.meta(),
                          options);
    const auto& first = good.meta().elements.front();
    writer.add(element_id_of(good.meta(), first), unrescaled_source(good, 0));
    if (order_failure) {
      expect(ErrorKind::invalid_format,
             [&] { writer.add(element_id_of(good.meta(), first), unrescaled_source(good, 0)); });
    } else {
      bool caught = false;
      try {
        writer.set_first_element(1);
      } catch (const sic::Error&) {
        throw std::runtime_error("set_first_element internal contract lost standard exception");
      } catch (const std::logic_error&) {
        caught = true;
      }
      check(caught, "late element offset must fail with logic_error");
    }
  }
  {
    ProducerWriter writer(part, good.meta(), part_options);
    for (size_t i = 0; i < good.meta().elements.size(); ++i)
      writer.add(element_id_of(good.meta(), good.meta().elements[i]), unrescaled_source(good, i));
    writer.finish_part(part, "held-source");
  }
  const auto side_path = part.string() + ".elements.json";
  const auto index_path = part.string() + ".idx";
  const ByteBuffer side_bytes = bytes(side_path), index_bytes = bytes(index_path);
  const auto side_text = std::string(side_bytes.begin(), side_bytes.end());
  const sic::support::Json side = sic::support::JsonParser::parse(side_text);
  const auto store_side = [&](const fs::path& name, const sic::support::Json& value) {
    const auto text = sic::support::dump_json(value);
    write(name.string() + ".elements.json", ByteBuffer(text.begin(), text.end()));
  };
  unsigned merge_attempt = 0;
  const auto merge = [&] {
    merge_element_parts(work / ("merged-held-" + std::to_string(++merge_attempt) + ".sicx"), {part},
                        options);
  };
  auto changed = side;
  changed["source"] = sic::support::Json::string_value("");
  store_side(part, changed);
  expect(ErrorKind::invalid_format, merge);
  changed = side;
  changed["first"] = sic::support::Json::integer_value(1);
  store_side(part, changed);
  expect(ErrorKind::invalid_format, merge);
  write(side_path, side_bytes);
  const auto second = work / "held-second-part";
  for (bool source_change : {false, true}) {
    changed = side;
    if (source_change)
      changed["source"] = sic::support::Json::string_value("different-source");
    else
      changed["header"]["d"] = sic::support::Json::integer_value(good.meta().d + 1);
    store_side(second, changed);
    expect(ErrorKind::integrity, [&] {
      merge_element_parts(work / "merged-held.sicx", {part, second}, options);
    });
  }
  write(index_path, ByteBuffer{});
  expect(ErrorKind::invalid_format, merge);
  auto mutated = index_bytes;
  mutated[8] ^= 1;
  write(index_path, mutated);
  expect(ErrorKind::invalid_format, merge);
  mutated = index_bytes;
  // First entry's type must denote a tower or body block.
  mutated[16] = 'X';
  write(index_path, mutated);
  expect(ErrorKind::invalid_format, merge);
  write(index_path, index_bytes);
  auto part_bytes = bytes(part);
  auto extra = part_bytes;
  extra.push_back(0);
  write(part, extra);
  expect(ErrorKind::invalid_format, merge);
  write(part, part_bytes);
  mutated = index_bytes;
  mutated.back() ^= 1;
  write(index_path, mutated);
  expect(ErrorKind::integrity, merge);
  write(index_path, index_bytes);
  // Authenticate changed directory entries so deeper coverage checks are reached.
  const auto index_count = get_le(index_bytes.data() + 8, 8);
  std::vector<IndexEntry> entries;
  for (uint64_t i = 0; i < index_count; ++i)
    entries.push_back(IndexEntry::get(index_bytes.data() + 16 + i * kIndexEntryBytes));
  for (bool missing : {false, true}) {
    auto edited = entries;
    if (missing) {
      const auto object = edited.front().object;
      for (auto& entry : edited)
        if (entry.object == object) entry.object = object + 1;
    } else {
      ++edited.front().first;
    }
    ByteBuffer encoded(index_bytes.begin(), index_bytes.begin() + 16);
    for (const auto& entry : edited) entry.put(encoded);
    const auto root = merkle_root(edited);
    encoded.insert(encoded.end(), root.begin(), root.end());
    write(index_path, encoded);
    bool caught = false;
    try {
      merge();
    } catch (const sic::Error& error) {
      caught = error.kind() == ErrorKind::invalid_format &&
               std::string(error.what())
                       .find(missing ? "missing private part leaves"
                                     : "private part logical coverage") != std::string::npos;
    }
    check(caught, "authenticated part did not reach its intended coverage gate");
  }
  write(index_path, index_bytes);
}
