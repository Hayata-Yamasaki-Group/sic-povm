// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fixtures/fixture.hpp"
#include "temporary_directory.hpp"

#include <algorithm>

namespace sicx_tests {
namespace {
void compact_leaf_admission(Fixture& fixture) {
  using namespace sic::sicx;
  const sic_test::TemporaryDirectory scratch("sicx-compact-leaves");
  constexpr size_t rows = 4096;
  ByteBuffer body;
  for (size_t first = 0; first < rows; ++first) {
    put_varint(body, 0);
    put_varint(body, first);
    put_varint(body, 1);
    put_varint(body, 2);  // One all-zero coordinate.
  }
  const auto& exmt = fixture.good.front().bytes;
  const auto write_body = [&](StreamWriter& writer) {
    writer.chunk("BODY", kRequired, body, 0, 0, rows);
  };
  for (const std::string mode : {"fresh", "replay", "adopt", "retained"}) {
    const auto path = scratch.path / (mode + ".sicx");
    StreamOptions options;
    options.resume = mode == "replay" || mode == "adopt";
    if (options.resume) {
      StreamWriter prior(path);
      prior.chunk("EXMT", kRequired, exmt);
      write_body(prior);
    }
    const std::string_view phase =
        mode == "retained" ? "writer directory append" : "logical leaf rows";
    bool tight = false;
    options.admit_workspace = [&](const char* stage, uint64_t bytes) {
      if (tight && phase == stage && bytes > 48 * 1024)
        throw sic::Error(sic::ErrorKind::resource, stage);
    };
    StreamWriter writer(path, options);
    if (options.resume)
      writer.adopt(1);
    else
      writer.chunk("EXMT", kRequired, exmt);
    const auto offset = writer.offset();
    const auto partial = path.string() + ".partial", journal = path.string() + ".partial.journal";
    const auto prefix_bytes = read_file(partial), journal_bytes = read_file(journal);
    const auto append = [&] {
      if (mode == "adopt")
        writer.adopt(1);
      else
        write_body(writer);
    };
    tight = true;
    try {
      append();
      throw std::runtime_error("compact leaf workspace unexpectedly admitted");
    } catch (const sic::Error& error) {
      if (error.kind() != sic::ErrorKind::resource || phase != error.what()) throw;
    }
    if (writer.offset() != offset || writer.index().size() != 1 ||
        prefix_bytes != read_file(partial) || journal_bytes != read_file(journal))
      throw std::runtime_error("compact leaf refusal changed writer state");
    tight = false;
    append();
    if (writer.index().back().leaf_count != rows || writer.pending_replay() != 0)
      throw std::runtime_error("compact leaf retry lost coverage");
    ++fixture.tests;
    std::cout << "PASS compact leaf admission " << mode << '\n';
  }
}
}  // namespace

void directory_cases(Fixture& fixture) {
  auto& source = fixture.source;
  auto& good = fixture.good;
  auto& tests = fixture.tests;
  const auto& work = fixture.work;
  auto directory_rejected = [&](const std::string& name, DirectoryEdit edit,
                                const std::string& diagnostic) {
    const auto path = work / "directory.sicx";
    write(path, good, std::move(edit));
    std::string message;
    try {
      sic::sicx::Reader r(path);
      (void)sic::sicx::verify_all(r, 1);
    } catch (const std::exception& e) {
      message = e.what();
    }
    if (message.find(diagnostic) == std::string::npos)
      throw std::runtime_error("directory rejection " + name + " expected " + diagnostic +
                               ", got: " + message);
    ++tests;
    std::cout << "PASS reject " << name << '\n';
  };
  directory_rejected(
      "PDIR codec", [](auto&, auto& p) { p[1].codec = 1; }, "codec");
  directory_rejected(
      "PDIR decoded length", [](auto&, auto& p) { ++p[1].decoded_length; }, "length");
  directory_rejected(
      "PDIR offset", [](auto&, auto& p) { ++p[1].offset; }, "contiguous");
  directory_rejected(
      "PDIR JSON resource limit",
      [](auto&, auto& p) { p[0].length = p[0].decoded_length = sic::sicx::kMaxTextChunk + 1; },
      "resource limit");
  directory_rejected(
      "PDIR physical resource limit",
      [](auto&, auto& p) { p[1].length = p[1].decoded_length = sic::sicx::kMaxBlockBytes + 1; },
      "resource limit");
  directory_rejected(
      "PDIR EXMT leaf mapping", [](auto&, auto& p) { p[0].first_leaf = 1; },
      "EXMT directory mapping");
  directory_rejected(
      "PDIR zero leaf count", [](auto&, auto& p) { p[1].leaf_count = 0; }, "coverage");
  directory_rejected(
      "PDIR duplicate coverage", [](auto&, auto& p) { p[1].first_leaf = 0; }, "coverage");
  directory_rejected(
      "PDIR object", [](auto&, auto& p) { ++p[1].object; }, "mapping");
  directory_rejected(
      "PDIR first", [](auto&, auto& p) { ++p[1].first; }, "mapping");
  directory_rejected(
      "PDIR count", [](auto&, auto& p) { ++p[1].count; }, "coordinate sum");
  directory_rejected(
      "PDIR non-leaf mapping", [](auto&, auto& p) { p.back().leaf_count = 1; }, "non-leaf");
  directory_rejected(
      "LDIR EXMT count", [](auto& l, auto&) { ++l[0].count; }, "EXMT directory mapping");
  directory_rejected(
      "LDIR element", [](auto& l, auto&) { ++l[1].element; }, "boundary/order");
  directory_rejected(
      "LDIR first", [](auto& l, auto&) { ++l[1].first; }, "boundary/order");
  directory_rejected(
      "LDIR count", [](auto& l, auto&) { ++l[1].count; }, "boundary/order");
  directory_rejected(
      "LDIR extra leaf", [](auto& l, auto&) { l.push_back(l.back()); }, "extra logical");
  directory_rejected(
      "LDIR missing leaf", [](auto& l, auto&) { l.pop_back(); }, "missing logical");
  directory_rejected(
      "LDIR logical hash", [](auto& l, auto&) { l[1].sha256[0] ^= 1; }, "logical leaf digest");
  // Framing and roots are checked before allocations/decoding.
  const auto structural = work / "structural.sicx";
  write(structural, good);
  std::ifstream in(structural, std::ios::binary);
  const sic::sicx::ByteBuffer bytes((std::istreambuf_iterator<char>(in)), {});
  for (bool header : {true, false}) {
    auto memory = std::make_shared<RecoveringSource>(bytes);
    sic::sicx::Reader r(memory);
    const auto li = r.element_blocks(0).front(), owner = r.leaf_owner(li);
    const auto& entry = r.index()[owner];
    memory->wire[entry.offset + (header ? 0 : 16)] ^= 1;
    bool refused = false;
    try {
      (void)r.leaf(li);
    } catch (const std::exception&) {
      refused = true;
    }
    if (!refused || memory->invalidated.size() != 1 ||
        memory->invalidated[0].offset != entry.offset ||
        memory->invalidated[0].size != 16 + entry.length)
      throw std::runtime_error("corrupt unit was not rejected and invalidated completely");
    if (r.leaf(li) != source.leaf(source.element_blocks(0).front()))
      throw std::runtime_error("clean refetch after invalidation changed bytes");
    ++tests;
    std::cout << "PASS invalidate/refetch " << (header ? "header" : "payload") << '\n';
  }
  const uint64_t lo = sic::sicx::get_le(bytes.data() + bytes.size() - 112, 8),
                 po = sic::sicx::get_le(bytes.data() + bytes.size() - 96, 8);
  const uint64_t ll = sic::sicx::get_le(bytes.data() + bytes.size() - 104, 8),
                 pl = sic::sicx::get_le(bytes.data() + bytes.size() - 88, 8);
  for (const auto [at, expected] : std::vector<std::pair<size_t, sic::support::ByteRange>>{
           {0, {0, 16}},
           {bytes.size() - 1, {bytes.size() - 112, 112}},
           {size_t(lo + 4), {lo, 16 + ll}},
           {size_t(po + 52), {po, 16 + pl}},
           {bytes.size() - 80, {lo, 16 + ll}}}) {
    auto memory = std::make_shared<RecoveringSource>(bytes);
    memory->wire[at] ^= 1;
    bool refused = false;
    try {
      sic::sicx::Reader r(memory);
    } catch (const std::exception&) {
      refused = true;
    }
    const bool covered =
        std::any_of(memory->invalidated.begin(), memory->invalidated.end(), [&](const auto range) {
          return range.offset == expected.offset && range.size == expected.size;
        });
    if (!refused || !covered) throw std::runtime_error("bootstrap unit not invalidated");
    sic::sicx::Reader recovered(memory);
    if (recovered.root() != source.root())
      throw std::runtime_error("bootstrap refetch root changed");
    ++tests;
  }
  {
    const auto work = fixture.work / "full-cache-recovery";
    filesystem::remove_all(work);
    sic::transport::SplitOptions split;
    split.file = structural;
    split.assets_dir = work / "parts";
    split.parts_path = split.assets_dir / "multiplet.sicx.parts.json";
    split.prefix = "d4-m1";
    (void)sic::transport::split(split);
    sic::access::RemoteOptions options;
    options.source = split.assets_dir.string();
    options.cache = work / "cache";
    options.max_fraction = 0.001;
    options.fetch_full = true;
    auto remote = sic::access::remote_byte_source(split.parts_path, options);
    sic::sicx::Reader r(remote);
    filesystem::path full;
    for (const auto& item : filesystem::recursive_directory_iterator(options.cache))
      if (item.path().filename() == "full.sicx") full = item.path();
    if (full.empty()) throw std::runtime_error("full fallback not activated");
    const auto li = r.element_blocks(0).front();
    const auto& entry = r.index()[r.leaf_owner(li)];
    {
      std::fstream file(full, std::ios::binary | std::ios::in | std::ios::out);
      file.seekp(entry.offset + 16);
      const char bad = char(bytes.at(entry.offset + 16) ^ 1);
      file.write(&bad, 1);
      if (!file) throw std::runtime_error("full-cache corruption injection failed");
    }
    bool refused = false;
    try {
      (void)r.leaf(li);
    } catch (const std::exception&) {
      refused = true;
    }
    if (!refused || filesystem::exists(full))
      throw std::runtime_error("corrupt active full cache retained");
    if (r.leaf(li) != source.leaf(source.element_blocks(0).front()))
      throw std::runtime_error("full-cache refetch changed bytes");
    ++tests;
    std::cout << "PASS invalidate/refetch active full cache\n";
  }
  for (size_t at :
       {size_t(0), size_t(8), size_t(10), size_t(12), bytes.size() - 1, bytes.size() - 112,
        bytes.size() - 96, bytes.size() - 80, bytes.size() - 48, bytes.size() - 16, size_t(lo + 4),
        size_t(lo + 16), size_t(lo + 28), size_t(po + 4), size_t(po + 16), size_t(po + 52)}) {
    auto bad = bytes;
    bad[at] ^= 1;
    std::ofstream out(structural, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bad.data()), bad.size());
    out.close();
    bool refused = false;
    try {
      sic::sicx::Reader r(structural);
    } catch (const std::exception&) {
      refused = true;
    }
    if (!refused) throw std::runtime_error("accepted structural mutation");
    ++tests;
  }
  if (source.leaf(0) != source.chunk(0)) throw std::runtime_error("EXMT leaf 0");
  ++tests;
  compact_leaf_admission(fixture);
}
}  // namespace sicx_tests
int main(int argc, char** argv) {
  return sicx_tests::run_fixture_case(argc, argv, &sicx_tests::directory_cases);
}
