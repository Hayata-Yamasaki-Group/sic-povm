// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
namespace sic_check::verifier::test {
using namespace sic_check::verifier;
using namespace fixture;
void exact_metadata_refusals(unsigned& d, DimensionContext& dimension) {
  dimension.meta("unknown-EXMT", "body", "{}");
  dimension.meta("features", "required_features", "[\"unknown\"]");
  dimension.meta("dimension", "d", "\"1\"");
  dimension.meta("decimal", "d", "\"04\"");
  dimension.meta("version", "version", "[\"1\",\"1\"]");
  dimension.meta("entry-rule", "entry_rule",
                 "{\"name\":\"sicx-divisors/1.0\",\"representatives\":[\"0\"]}");
  dimension.meta("smith-product", "galois", galois(d, {"2", "2"}));
  for (string smith : {"[]", "[\"1\"]", "[\"3\"]"})
    dimension.meta("smith-" + smith, "galois",
                   "{\"smith\":" + smith + ",\"unit_generator_words\":[[\"0\"]]}");
  dimension.meta("word-residue", "galois", galois(d, {"2"}, {{"2"}}));
  dimension.meta("rescaling-rule", "generator_rescaling", "{\"rule\":\"none\",\"shifts\":[\"0\"]}");
  dimension.meta("shifts-count", "generator_rescaling",
                 "{\"rule\":\"sicx-2adic-greedy/1.0\",\"shifts\":[]}");
  for (string value : {"0", "1", "4", "37", "999999999999999989"})
    dimension.meta(
        "tower-degree-" + value, "tower",
        "{\"branch_bits\":\"128\",\"branches\":[[\"0 0 0 0\",\"1 0 0 0\"]],\"degrees\":[\"" +
            value + "\"]}");
  dimension.meta("branches-count", "tower",
                 "{\"branch_bits\":\"128\",\"branches\":[],\"degrees\":[\"2\"]}");
  for (string bits : {"64", "129", "256"})
    dimension.meta("branch-bits-" + bits, "tower",
                   "{\"branch_bits\":\"" + bits +
                       "\",\"branches\":[[\"0 0 0 0\",\"1 0 0 0\"]],\"degrees\":[\"2\"]}");
  for (string dump :
       {"0 1 0 0", "2 0 0 0", "0 0 2 -80", "0 0 0 1", "0 0 1 -80", "0 0 0 0 ", "0 0 A -80"})
    dimension.meta("branch-dump-" + hex((const Uint8*)dump.data(), dump.size()), "tower",
                   "{\"branch_bits\":\"128\",\"branches\":[[" + quote(dump) +
                       ",\"1 0 0 0\"]],\"degrees\":[\"2\"]}");
  dimension.mutate("branch-wrong-root", [](Model& m) {
    m.meta["tower"] =
        "{\"branch_bits\":\"128\",\"branches\":[[\"1 0 0 0\",\"0 0 0 "
        "0\"]],\"degrees\":[\"2\"]}";
  });
  for (string key : {"kind", "level", "axis", "scale", "max_bits", "levels"})
    dimension.mutate("descriptor-" + key, [=](Model& m) {
      auto& f = m.elements[1].fields;
      f[key] = key == "kind" ? "\"rep\"" : key == "levels" ? "[\"0\"]" : "\"99\"";
    });
  {
    auto m = small(d);
    m.elements[0].fields["scale"] = "\"0\"";
    dimension.descriptor_reject("zero scale", m);
  }
  {
    auto m = small(d);
    m.elements[0].fields["max_bits"] = "\"8388609\"";
    dimension.descriptor_reject("max_bits bound", m);
  }
  {
    auto m = small(d);
    m.elements[0].fields["levels"] = "[\"0\"]";
    dimension.descriptor_reject("support field level", m);
  }
  {
    auto m = small(d);
    m.elements[2].fields["levels"] = "[\"0\",\"0\"]";
    dimension.descriptor_reject("duplicate support level", m);
  }
  {
    auto m = small(d);
    m.elements.back().fields["position"] = "\"999\"";
    dimension.descriptor_reject("representative position", m);
  }
  {
    auto m = small(d);
    element(m, "rep", 0, 0);
    dimension.descriptor_reject("extra element", m);
  }
  dimension.meta("smith-unit-factor", "galois", galois(d, {"1", "2"}));
  dimension.meta("word-width", "galois", galois(d, {"2"}, {{"0", "0"}}));
  dimension.meta("entry-name-only", "entry_rule",
                 "{\"name\":\"other/1\",\"representatives\":" + array([&] {  // identity-negative
                   vector<string> v;
                   for (Uint64 r = 0; r == 0 || r <= d / 2; r = r ? r * 2 : 1)
                     v.push_back(std::to_string(r));
                   return v;
                 }()) +
                     "}");
  dimension.meta("branch-third-component", "tower",
                 "{\"branch_bits\":\"128\",\"branches\":[[\"0 0 0 0\",\"1 0 0 0\",\"0 0 0 "
                 "0\"]],\"degrees\":[\"2\"]}");
  dimension.mutate("wsq-level-order", [](Model& m) { m.elements[0].fields["level"] = "\"1\""; });
  dimension.mutate("nonprimitive-odd-rep", [](Model& m) {
    m.elements[2].leaves[0] = leaf(2, 0, 1, 3);
    m.elements[2].fields["max_bits"] = "\"3\"";
  });
  dimension.mutate("scale-zero", [](Model& m) { m.elements[0].fields["scale"] = "\"0\""; });
  dimension.mutate("scale-leading-zero",
                   [](Model& m) { m.elements[0].fields["scale"] = "\"01\""; });
  dimension.mutate("descriptor-extra", [](Model& m) { m.elements[0].fields["field"] = "\"0\""; });
  dimension.mutate("representative-missing", [](Model& m) { m.elements.pop_back(); });
  dimension.mutate("leaf-element", [](Model& m) { m.elements[0].leaves[0][0] = 1; });
  dimension.mutate("leaf-first", [](Model& m) { m.elements[0].leaves[0][1] = 1; });
  dimension.mutate("leaf-count", [](Model& m) { m.elements[0].leaves[0][2] = 2; });
  for (ByteBuffer run : vector<ByteBuffer>{
           {0}, {4}, {3, 0, 0}, {3, 1, 0}, {3, 4, 1, 0, 0}, {3, 2}, {0x80, 0}, {2, 0}})
    dimension.mutate("run-" + hex(run.data(), run.size()), [=](Model& m) {
      m.elements[0].leaves[0] = {0, 0, 1};
      append(m.elements[0].leaves[0], run);
    });
}

void ray_metadata_refusals(DimensionContext& dimension) {
  dimension.raysbad(
      "empty-rays",
      "[{\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c0\",\"word\":[\"0\"]}]", "[]");
  dimension.raysbad("ray-unknown-key", "\"anchor\":\"0\"", "\"anchor\":\"0\",\"bad\":\"0\"");
  for (string label : {"c00", "c-1", "x0", "c1000000000000000000"})
    dimension.raysbad("label-" + label, "\"c0\"", quote(label));

  dimension.raysbad("ray-word-residue", "\"word\":[\"0\"]", "\"word\":[\"2\"]");
  dimension.raysbad("anchor-range", "\"anchor\":\"0\"", "\"anchor\":\"99\"");
  dimension.raysbad("accuracy-tight", "\"199\"", "\"198\"");
  dimension.raysbad("working-zero", "\"256\"", "\"0\"");
  dimension.raysbad("limbs-zero", "\"limbs\":\"1\"", "\"limbs\":\"0\"");
  dimension.raysbad("decoder-draft-sha", "source_commit", "sha256");
  dimension.raysbad("decoder-commit", "0000000000000000000000000000000000000000", "BAD");
  for (auto [name, off, value] :
       vector<std::tuple<string, size_t, Uint64>>{{"flags", 0, 2},
                                                  {"exponent", 8, (1ull << 40) + 1},
                                                  {"radius-even", 16, 2},
                                                  {"radius-large", 16, 1ull << 30},
                                                  {"mantissa-even", 32, 2},
                                                  {"anchor-negative", 0, 1},
                                                  {"anchor-imag", 72, 1},
                                                  {"zero-E", 48, 1},
                                                  {"zero-e", 64, 1},
                                                  {"negative-zero", 40, 1},
                                                  {"norm", 32, 3}})
    dimension.mutate(
        "BALL-" + name, [=](Model& m) { set64(m.extras[1].payload, off, value); }, true);
  dimension.mutate(
      "BALL-zero-radii",
      [](Model& m) {
        set64(m.extras[1].payload, 16, 0);
        set64(m.extras[1].payload, 24, 0);
      },
      true);
  dimension.mutate(
      "BALL-size", [](Model& m) { m.extras[1].payload.pop_back(); }, true);
  // Mutations with all physical and logical hashes rebuilt reach semantic checks.
  for (const auto& [name, json] : std::map<string, string>{
           {"whitespace", "{ }"},
           {"bare-number", "{\"x\":1}"},
           {"duplicate-key", "{\"x\":\"0\",\"x\":\"1\"}"},
           {"unsorted-key", "{\"z\":\"0\",\"a\":\"1\"}"},
           {"slash-escape", "{\"x\":\"\\/\"}"},
           {"uppercase-control", "{\"x\":\"\\u001F\"}"},
           {"unicode-escape", "{\"x\":\"\\u0020\"}"},
           {"boolean", "{\"x\":true}"},
           {"trailing-json", "{}{}"},
           {"raw-control", string("{\"x\":\"\n\"}")},
           {"bad-utf8", string("{\"x\":\"") + char(0xc0) + char(0x80) + "\"}"}})
    dimension.mutate("JSON-" + name, [=](Model& m) {
      m.extras.push_back({"PROV", 0, bytes(json)});
    });
  dimension.mutate("nonalternating-runs", [](Model& m) {
    m.elements[2].fields["levels"] = "[\"0\"]";
    m.elements[2].coords = 2;
    m.elements[2].leaves[0] = {2, 0, 2, 2, 2};
  });
  dimension.mutate("nonminimal-levels", [](Model& m) {
    m.elements[2].fields["levels"] = "[\"0\"]";
    m.elements[2].coords = 2;
    m.elements[2].leaves[0] = {2, 0, 2, 3, 2, 1, 0, 2};
  });
  dimension.mutate("zero-root", [](Model& m) {
    m.elements[0].fields["max_bits"] = "\"0\"";
    m.elements[0].leaves[0] = leaf(0, 0, 1, 0);
  });
  dimension.mutate("duplicate-PROV", [](Model& m) {
    m.extras.push_back({"PROV", 0, bytes("{}")});
    m.extras.push_back(m.extras.back());
  });
  dimension.mutate(
      "duplicate-RAYS", [](Model& m) { m.extras.insert(m.extras.begin(), m.extras[0]); }, true);
  dimension.mutate(
      "duplicate-PREC",
      [](Model& m) {
        m.extras.push_back({"PREC", 0, bytes("{}")});
        m.extras.push_back(m.extras.back());
      },
      true);
  dimension.raysbad("ray-word-length", "\"word\":[\"0\"]", "\"word\":[]");
  dimension.raysbad("accuracy-bound", "\"199\"", "\"1048577\"");
  dimension.raysbad("accuracy-minus-zero", "\"199\"", "\"-0\"");
  dimension.raysbad("working-bound", "\"256\"", "\"1048577\"");
  dimension.raysbad("limbs-bound", "\"limbs\":\"1\"", "\"limbs\":\"1025\"");
  for (const string& name :
       {"sic-decode/2 (contraction: acb_dot per class value)",       // identity-negative
        "sic_decode_sicx/2 (contraction: acb_dot per class value)",  // identity-negative
        "sic-decode/1", "sic-decode/1.0.0", "sic-decode/1.0.1",      // identity-negative
        "other/1.0.0"})                                              // identity-negative
    dimension.raysbad("decoder-identifier-" + name, "sic-decode/1.0", name);
  dimension.raysbad("decoder-empty-name", "sic-decode/1.0", "");
  dimension.raysbad("decoder-extra-key", "\"name\":\"sic-decode/1.0\"",
                    "\"bad\":\"0\",\"name\":\"sic-decode/1.0\"");
  string ray0 = "{\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c0\",\"word\":[\"0\"]}";
  dimension.raysbad("duplicate-ray-label", ray0, ray0 + "," + ray0);
  string c2 = ray0;
  c2.replace(c2.find("c0"), 2, "c2");
  dimension.raysbad("numeric-ray-order", ray0, c2 + "," + ray0);
}

void json_sibling_depth(size_t& valid, string& pre, Model& siblings, string& json) {
  for (int i = 0; i < 64; ++i) {
    if (i) json += ",";
    json += "{}";
  }
  json += "]}";
  siblings.extras.push_back({"PROV", 0, bytes(json)});
  require(run(write(siblings), pre + "JSON-empty-siblings") == 0, "empty JSON sibling depth");
  ++valid;
}

void ray_parser_contracts(size_t& /* valid */, size_t& /* rejects */) {
  // RAYS parser contracts do not rely on later BALL width/gauge checks.
  {
    auto input = small(4, true);
    string original(input.extras[0].payload.begin(), input.extras[0].payload.end());
    auto ray_reject = [&](string name, string from, string to) {
      string json = original;
      auto at = json.find(from);
      if (at == string::npos) throw std::runtime_error("bad RAYS test replacement");
      json.replace(at, from.size(), to);
      Jv root = parse_canonical((const Uint8*)json.data(), json.size(), "test");
      Meta m;
      m.d = 4;
      m.smith = {2};
      bool rejected = false;
      try {
        parse_rays(m, j_get(root, "balls", 'o'), j_get(root, "rays", 'a'));
      } catch (const Reject&) {
        rejected = true;
      }
      require(rejected, "RAYS parser contract " + name);
    };
    ray_reject("limbs-zero", "\"limbs\":\"1\"", "\"limbs\":\"0\"");
    ray_reject("limbs-bound", "\"limbs\":\"1\"", "\"limbs\":\"1025\"");
    ray_reject("anchor-range", "\"anchor\":\"0\"", "\"anchor\":\"4\"");
    ray_reject("accuracy-bound", "\"199\"", "\"1048577\"");
    ray_reject("rays-empty",
               "[{\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c0\",\"word\":[\"0\"]}]",
               "[]");
    string first = "{\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c2\",\"word\":[\"0\"]}";
    string second =
        "{\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c1\",\"word\":[\"1\"]}";
    ray_reject("numeric-label-order",
               "{\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c0\",\"word\":[\"0\"]}",
               first + "," + second);
    ray_reject("duplicate-label",
               "{\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c0\",\"word\":[\"0\"]}",
               "{\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c0\",\"word\":[\"0\"]},{"
               "\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c0\",\"word\":[\"1\"]}");
    auto model = small(4);
    auto b = write(model);
    Opts opt;
    opt.path = fixtures_dir + "/d4-valid-exact.sicx";
    Container c;
    open_container(c, opt);
    verify_chunks(c, opt);
    branch_check(c, opt);
    Decoder decoder(c);
    decoder.set_prec(512);
    vector<ACB> f(2);
    acb_set_si(f[0].v, -1);
    acb_one(f[1].v);
    ACB candidate, result;
    acb_set_si(candidate.v, 2);
    arb_add_error_2exp_si(acb_realref(candidate.v), 0);
    bool refused = false;
    try {
      decoder.newton(f, candidate.v, result.v);
    } catch (const PrecErr&) {
      refused = true;
    }
    require(refused, "initial Newton certification needs strict interior");
    Opts ratio;
    ratio.decode = "ratio";
    require(run(write(small(4, true)), "ratio-zero-denominators", ratio, true) == 3,
            "optional ratio cannot pass vacuously");
  }
}

void scalar_parser_contracts(size_t& /* valid */, size_t& /* rejects */) {
  // Focused parser contracts isolate checks that whole-file validation also enforces.
  {
    auto no = [&](const string& name, auto operation) {
      bool rejected = false;
      try {
        operation();
      } catch (const Reject&) {
        rejected = true;
      }
      require(rejected, name);
    };
    for (string json : {"", "[", "[\"x\"", "{", "{\"x\"", "{\"x\":\"y\"", "\"abc", "\"abc\\",
                        "\"\\u01ff\"", "\"\\u00FF\"", "\"\\u0020\"", "{}{}", "\"\n\""})
      no("JSON parser contract " + json,
         [&] { parse_canonical((const Uint8*)json.data(), json.size(), "test"); });
    for (string dec : {"-0", "01", "-01", "+1", "", "1000000000000000000"})
      no("signed decimal contract " + dec, [&] { to_i64(dec, "test"); });
    no("checked addition overflow", [&] { checked_add(UINT64_MAX, 1); });
    no("checked multiplication overflow", [&] { checked_mul(UINT64_MAX, 2); });
    for (ByteBuffer b : vector<ByteBuffer>{{0x80, 0}, {1}, {4, 1, 0}, {4, 1}})
      no("integer primitive contract " + hex(b.data(), b.size()), [&] {
        Cur cur{b.data(), b.data() + b.size()};
        Integer z;
        rd_int(cur, z);
      });
    for (string dump : {"0 0 0 0 ", "0 0 A -80", "2 0 0 0", "0 1 0 0", "0 0 2 -80", "0 0 0 1"})
      no("Arb dump primitive contract " + dump, [&] {
        Integer a, b, c, d;
        parse_arbdump(dump, a, b, c, d);
      });
    for (auto [off, value] : vector<std::pair<size_t, Uint64>>{{0, 2},
                                                               {8, (1ull << 40) + 1},
                                                               {32, 2},
                                                               {16, 2},
                                                               {16, 1ull << 30},
                                                               {48, 1},
                                                               {64, 1},
                                                               {40, 1}}) {
      ByteBuffer b(80);
      set64(b, off, value);
      no("BALL part primitive contract " + std::to_string(off) + "-" + std::to_string(value), [&] {
        BallPart p;
        parse_ball_part(b.data() + (off >= 40 ? 40 : 0), 1, p);
      });
    }
    no("closed logical block consumes all bytes", [&] {
      ByteBuffer b{0, 0, 1, 2, 0};
      parse_block(b.data(), b.size(), 0, 0, 1, Integer{}, true,
                  [](Uint64, const fmpz_t, const fmpz_t) {});
    });
    no("range read bound contract", [&] {
      File f;
      f.open(fixtures_dir + "/d4-valid-exact.sicx");
      f.read(f.size, 1);
    });
    no("raw range read bound contract", [&] {
      File f;
      f.open(fixtures_dir + "/d4-valid-exact.sicx");
      Uint8 b;
      f.read(f.size, &b, 1);
    });
    for (string malformed : {"[]", "{}", "{\"a\":[]}"}) {
      Jv j = parse_canonical((const Uint8*)malformed.data(), malformed.size(), "test");
      no("required key contract " + malformed, [&] { j_get(j, "a", 's'); });
    }
    bool refused = false;
    Real wide;
    arb_zero(wide.v);
    arb_add_error_2exp_si(wide.v, 0);
    try {
      rounded_binary64(wide.v, 128);
    } catch (const PrecErr&) {
      refused = true;
    }
    require(refused, "binary64 unique rounding refusal");
  }
}
}  // namespace sic_check::verifier::test
