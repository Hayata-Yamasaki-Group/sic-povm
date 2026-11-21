// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/block.hpp"
#include "sicx/exact/exact.hpp"
#include "sicx/model/unit_group.hpp"
// Hand-built K(i)/K examples exercise format and entry rules only. They are not SICs.
#include <iostream>

#include "decode/kernels/kernels.hpp"
#include "sic/decode/decoder.hpp"
#include "sic/sicx/exact.hpp"
#include "sic/sicx/finalize.hpp"
#include "sicx/container/producer_writer.hpp"

namespace filesystem = std::filesystem;
static unsigned checks = 0;
static void require(bool ok, const std::string& what) {
  ++checks;
  if (!ok) throw std::runtime_error(what);
}
static void reject(const sic::support::Json& good, auto change, const std::string& why) {
  auto j = good;
  change(j);
  bool refused = false;
  try {
    (void)sic::sicx::meta_from_json(j);
  } catch (const sic::Error&) {
    refused = true;
  }
  require(refused, "accepted " + why);
}
// Exhaustive independent multiplication, without the production logarithm table.
static std::vector<uint64_t> brute_word(const sic::sicx::Meta& m, uint64_t coordinate) {
  const auto& u = *m.units;
  const auto g = std::gcd(coordinate, m.d), n = m.d / g;
  uint64_t lift = 1;
  while (std::gcd(lift, m.d) != 1 || (n != 1 && lift % n != coordinate / g)) ++lift;
  std::vector<uint64_t> tuple(u.orders.size()), answer;
  bool done = false;
  do {
    uint64_t value = 1;
    for (size_t j = 0; j < tuple.size(); ++j)
      for (uint64_t a = 0; a < tuple[j]; ++a) value = value * u.generators[j] % m.d;
    if (value == lift) {
      require(answer.empty(), "duplicate discrete logarithm");
      answer.assign(m.smith.size(), 0);
      for (size_t j = 0; j < tuple.size(); ++j)
        for (size_t l = 0; l < answer.size(); ++l)
          answer[l] = (answer[l] + tuple[j] * m.unit_generator_words[j][l]) % m.smith[l];
    }
    size_t j = 0;
    while (j < tuple.size() && ++tuple[j] == u.orders[j]) {
      tuple[j] = 0;
      ++j;
    }
    done = j == tuple.size();
  } while (!done);
  require(!answer.empty(), "missing brute-force logarithm");
  return answer;
}
static void test(uint64_t d, const std::vector<uint64_t>& generators,
                 const std::vector<uint64_t>& orders, const filesystem::path& work) {
  sic::sicx::Meta m;
  m.d = d;
  m.D0 = sic::sicx::field_radicand(d);
  m.degrees = {2};
  m.width = {1, 2};
  m.D = 2;
  m.smith = {2};
  m.units = std::make_shared<const sic::sicx::UnitGroup>(d);
  m.unit_generator_words.assign(m.units->orders.size(), {1});
  m.branches = {{{"0 0 0 0", "1 0 0 0"}}};
  m.rescaling_shifts = {0};
  require(m.units->generators == generators && m.units->orders == orders,
          "canonical generators d=" + std::to_string(d));
  const filesystem::path file = work / ("d" + std::to_string(d) + ".sicx");
  sic::sicx::StreamOptions stream;
  stream.durable = false;
  sic::sicx::ProducerWriter producer(file, m, stream);
  auto add = [&](sic::sicx::ElementId desc, bool odd, long x, long y) {
    producer.add(desc, [=](const sic::sicx::LeafVisitor& visit) {
      sic::sicx::FmpzPair p;
      fmpz_set_si(p.x, x);
      fmpz_set_si(p.y, y);
      visit(odd ? 1 : 0, p.x, p.y);
    });
  };
  add({"wsq", 0, -1, -1, -1}, false, -1, 0);
  add({"act", 0, 0, -1, -1}, false, 1, 0);
  for (auto position : sic::sicx::divisor_positions(d))
    add({"rep", 1, -1, -1, static_cast<int64_t>(position)}, position == 1, position == 1 ? -4 : 2,
        m.D0 == 1 ? 0 : (position == 1 ? -2 : 2));
  producer.write_meta();
  producer.writer().finish();
  sic::sicx::Reader reader(file);
  (void)sic::sicx::verify_all(reader, 2);
  const auto& meta = reader.meta();
  const auto json = sic::sicx::meta_to_json(meta);
  require(sic::support::dump_json(json) ==
              sic::support::dump_json(sic::sicx::meta_to_json(sic::sicx::meta_from_json(json))),
          "metadata round trip");
  std::vector<sic::decode::Decoder::Request> requests;
  for (uint64_t i = 0; i < d; ++i) {
    const auto entry = meta.entry(i);
    const auto g = std::gcd(i, d);
    require(entry.representative == (g == d ? 0 : static_cast<int64_t>(g)),
            "divisor representative");
    require(entry.word == brute_word(meta, i), "entry differs from brute force");
    requests.push_back({entry.representative, entry.word});
  }
  sic::decode::Decoder::Options options;
  options.pin = false;
  options.threads = 2;
  sic::decode::Decoder decoder(reader, options);
  const auto values = decoder.decode(requests);
  sic::support::ComplexBallVector omega(1), expected(1);
  if (m.D0 != 1) {
    arb_sqrt_ui(acb_realref(omega[0]), static_cast<ulong>(m.D0), 512);
    acb_neg(omega[0], omega[0]);
    if (m.D0 % 4 == 1) {
      acb_add_ui(omega[0], omega[0], 1, 512);
      acb_mul_2exp_si(omega[0], omega[0], -1);
    }
  }
  for (uint64_t i = 0; i < d; ++i) {
    acb_add_ui(expected[0], omega[0], std::gcd(i, d) == 1 ? 2 : 1, 512);
    if (std::gcd(i, d) == 1) {
      acb_mul_onei(expected[0], expected[0]);
      if (requests[i].word[0]) acb_neg(expected[0], expected[0]);
    }
    require(acb_overlaps(values[i], expected[0]), "decoded hand-built coordinate");
  }
  // Finalize the synthetic vector from its certified decode, without claiming a SIC.
  auto finalized_meta = meta;
  const auto normalized = sic::decode::normalize(values, 0, 192);
  std::vector<std::vector<sic::sicx::BallPart>> wire(1);
  for (uint64_t i = 0; i < d; ++i) {
    wire[0].push_back(sic::sicx::outward_ball_part(acb_realref(normalized[i]), 3));
    wire[0].push_back(sic::sicx::outward_ball_part(acb_imagref(normalized[i]), 3));
  }
  const std::vector<sic::sicx::FinalizeRay> rays{{"c0", {0}, 0}};
  const auto finalization = sic::sicx::build_finalization(finalized_meta, rays, wire, 192);
  sic::sicx::finalize_in_place(file, finalized_meta, finalization, false, reader.exact_root());
  sic::sicx::Reader finalized(file);
  sic::sicx::exact::BuildOptions exact_options;
  exact_options.multiplet = "m1";
  sic::sicx::exact::Builder builder(finalized, exact_options);

  const auto exported = builder.build_json(file.filename().string(), std::string(64, '0'));
  const auto& field = exported.at("field");
  if (d == 3) {
    require(field.at("polynomial").as_str() == "omega", "rational base polynomial");
    require(field.at("polynomial_coefficients").array.size() == 2, "rational polynomial degree");
    require(builder.conj_omega()[0].zero(), "rational conjugation placeholder");
  } else if (m.D0 % 4 != 1) {
    require(field.at("embedding").as_str() == "omega -> -sqrt(D0) with sqrt(D0) > 0",
            "non-half integral basis embedding");
    require(field.at("polynomial_coefficients").array.size() == 3, "quadratic polynomial degree");
  }
  reject(
      json, [](auto& j) { j["d"] = sic::support::Json::string_value("1"); }, "d below bound");
  reject(
      json, [](auto& j) { j["d"] = sic::support::Json::string_value("1048577"); }, "d above bound");
  reject(
      json, [](auto& j) { j["galois"]["word_5"] = sic::support::Json::array_value(); },
      "old word_5 key");
  reject(
      json, [](auto& j) { j["galois"]["word_minus_1"] = sic::support::Json::array_value(); },
      "old word_minus_1 key");
  reject(
      json,
      [](auto& j) {
        j["entry_rule"]["name"] =
            sic::support::Json::string_value("sicx-2power/1");  // identity-negative
      },
      "old entry rule");
  reject(
      json, [](auto& j) { j["galois"]["unit_generator_words"].array.pop_back(); }, "missing word");
  reject(
      json,
      [](auto& j) {
        j["galois"]["unit_generator_words"].array.push_back(sic::support::Json::array_value());
      },
      "extra word");
  reject(
      json, [](auto& j) { j["galois"]["unit_generator_words"].array[0].array.clear(); },
      "word rank");
  reject(
      json,
      [](auto& j) {
        j["galois"]["unit_generator_words"].array[0].array[0] =
            sic::support::Json::string_value("2");
      },
      "word residue");
  reject(
      json, [](auto& j) { j["entry_rule"]["representatives"].array.pop_back(); },
      "missing divisor");
  reject(
      json,
      [](auto& j) {
        j["entry_rule"]["representatives"].array.push_back(sic::support::Json::string_value("1"));
      },
      "duplicate divisor");
  reject(
      json,
      [](auto& j) {
        std::swap(j["entry_rule"]["representatives"].array[0],
                  j["entry_rule"]["representatives"].array[1]);
      },
      "divisor order");
  reject(
      json, [](auto& j) { j["entry_rule"]["representatives"].array.push_back(j["d"]); },
      "extra divisor");
  reject(
      json, [](auto& j) { j["galois"]["generators"] = sic::support::Json::array_value(); },
      "stored generators");
  reject(
      json, [](auto& j) { j["field"] = sic::support::Json::object_value(); }, "stored base field");
  bool refused = false;
  try {
    (void)meta.entry(d);
  } catch (const sic::Error&) {
    refused = true;
  }
  require(refused, "coordinate bound");
  // A syntactically valid degree-four presentation isolates the order relation check.
  auto degree4 = json;
  degree4["tower"]["degrees"].array.push_back(sic::support::Json::string_value("2"));
  degree4["tower"]["branches"].array.push_back(degree4["tower"]["branches"].array[0]);
  degree4["generator_rescaling"]["shifts"].array.push_back(sic::support::Json::string_value("0"));
  degree4["galois"]["smith"].array[0] = sic::support::Json::string_value("4");
  auto wsq = degree4["elements"].array[0];
  wsq["level"] = sic::support::Json::string_value("1");
  degree4["elements"].array.insert(degree4["elements"].array.begin() + 2, wsq);
  for (size_t j = 0; j < orders.size(); ++j)
    degree4["galois"]["unit_generator_words"].array[j].array[0] =
        sic::support::Json::string_value("2");
  (void)sic::sicx::meta_from_json(degree4);
  for (size_t j = 0; j < orders.size(); ++j)
    if (orders[j] % 4) {
      reject(
          degree4,
          [=](auto& x) {
            x["galois"]["unit_generator_words"].array[j].array[0] =
                sic::support::Json::string_value("1");
          },
          "word order relation");
    }
  if (d == 3) {
    // Decode rejects rational second components at the shared leaf parser.
    sic::sicx::BlockEncoder encoder(1);
    sic::sicx::FmpzPair p;
    fmpz_one(p.x);
    fmpz_one(p.y);
    const auto id = meta.representative.at(0);
    const auto bytes = encoder.encode(id, 0, 1, 1, [&](size_t) {
      return std::tuple<uint64_t, const fmpz*, const fmpz*>(0, p.x, p.y);
    });
    sic::sicx::IntCodec codec;
    sic::sicx::PairTransform transform(1);
    bool bad = false;
    try {
      sic::sicx::decode_block(
          bytes, meta, [](auto, auto, auto) {}, [](auto, auto) {}, codec, transform, p);
    } catch (const sic::Error&) {
      bad = true;
    }
    require(bad, "rational second component");
  }
  std::cout << "d=" << d << " format/entry/decoder PASS (synthetic, not a SIC)\n";
}
int main(int argc, char** argv) {
  try {
    if (argc != 2) return 2;
    filesystem::remove_all(argv[1]);
    filesystem::create_directories(argv[1]);
    require(sic::sicx::field_radicand(2) == -3 && sic::sicx::field_radicand(3) == 1 &&
                sic::sicx::field_radicand(5) == 3 && sic::sicx::field_radicand(7) == 2,
            "base fields");
    test(3, {2}, {2}, argv[1]);
    test(5, {2}, {4}, argv[1]);
    test(7, {3}, {6}, argv[1]);
    test(9, {2}, {6}, argv[1]);
    test(6, {5}, {2}, argv[1]);
    test(12, {7, 5}, {2, 2}, argv[1]);
    // Include higher 2-powers, odd prime powers and multi-prime CRT products.
    for (uint64_t d = 2; d <= 256; ++d) {
      sic::sicx::UnitGroup group(d);
      for (uint64_t u = 0; u < d; ++u) {
        uint64_t index = group.logarithm[u], product = 1;
        if (std::gcd(u, d) != 1) {
          require(index == sic::sicx::UnitGroup::kNonunit, "nonunit table slot");
          continue;
        }
        for (size_t j = 0; j < group.orders.size(); ++j) {
          for (uint64_t a = 0; a < index % group.orders[j]; ++a)
            product = product * group.generators[j] % d;
          index /= group.orders[j];
        }
        require(product == u && index == 0, "unit logarithm reconstruction");
      }
    }
    sic::sicx::UnitGroup cap(1u << 20);
    require(cap.generators == std::vector<uint64_t>({(1u << 20) - 1, 5}),
            "dimension cap generators");
    flint_cleanup();
    std::cout << "PASS " << checks << " general-d conformance checks\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
