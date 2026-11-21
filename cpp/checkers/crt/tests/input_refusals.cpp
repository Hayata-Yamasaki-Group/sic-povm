// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
#include "test_general.hpp"
namespace sic_check::crt::test {
namespace filesystem = std::filesystem;
using namespace sic_check::crt::sicx;
void rejection_cases() {
  auto original = fixture::File().encode();
  auto original_final = fixture::File(4, true).encode();
  auto byte = [&](const string& name, size_t pos, uint8_t value) {
    auto b = original;
    b.at(pos) = value;
    negative(name, b);
  };
  byte("magic", 0, 0);
  byte("major", 8, 2);
  byte("minor", 10, 1);
  byte("physical features", 12, 1);
  byte("footer magic", original.size() - 1, 0);
  for (size_t field : {0, 8, 16, 24, 96}) {
    auto b = original;
    fixture::set(b, b.size() - 112 + field, UINT64_MAX);
    negative("footer geometry/overflow " + std::to_string(field), b);
  }
  for (size_t field : {32, 64}) {
    auto b = original;
    b[b.size() - 112 + field] ^= 1;
    negative("tree root " + std::to_string(field), b);
  }
  for (size_t n : {0, 15, 64, 104, 111})
    negative(
        "truncated " + std::to_string(n),
        fixture::ByteBuffer(original.begin(), original.begin() + std::min(n, original.size())));
  {
    auto b = original;
    b.push_back(0);
    negative("bytes after footer", b);
  }
  for (bool physical : {false, true}) {
    for (Uint64 count : {0ull, (1ull << 24) + 1}) {
      auto b = original;
      size_t footer = b.size() - 112;
      Uint64 dir = fixture::get(b, footer + (physical ? 16 : 0));
      fixture::set(b, dir + 16, count);
      negative("directory count", b);
    }
    auto b = original;
    size_t footer = b.size() - 112;
    Uint64 dir = fixture::get(b, footer + (physical ? 16 : 0));
    b[dir + 4] = 0;
    negative("directory flags", b);
  }
  auto directory = [&](const string& name, bool physical, size_t entry, size_t offset, Uint64 value,
                       int width = 8) {
    auto b = original;
    Uint64 dir = fixture::get(b, b.size() - 112 + (physical ? 16 : 0));
    fixture::set(b, dir + 24 + entry * (physical ? 112 : 64) + offset, value, width);
    fixture::reseal(b);
    negative(name, b);
  };
  directory("LDIR reserved", false, 1, 4, 1, 4);
  directory("PDIR reserved", true, 1, 28, 1, 4);
  directory("zero leaf count", false, 1, 24, 0);
  directory("wrong logical element", false, 1, 8, 99);
  directory("wrong logical first", false, 1, 16, 1);
  directory("logical boundary count", false, 1, 24, 2);
  directory("EXMT logical marker", false, 0, 24, 0);
  directory("physical offset overflow", true, 1, 8, UINT64_MAX);
  directory("encoded length overflow", true, 1, 16, UINT64_MAX);
  directory("decoded length", true, 1, 32, 999);
  directory("codec", true, 1, 24, 1, 4);
  directory("first logical coverage", true, 1, 40, 0);
  directory("empty physical leaf span", true, 1, 48, 0);
  directory("physical leaf overflow", true, 1, 48, UINT64_MAX);
  directory("physical wrong object", true, 1, 56, 99);
  directory("physical first coordinate", true, 1, 64, 1);
  directory("physical coordinate count", true, 1, 72, 2);
  directory("EXMT coordinate marker", true, 0, 72, 1);
  directory("physical payload digest", true, 1, 80, 0);
  directory("logical digest", false, 1, 32, 0);
  for (const string& type : {"META", "INDX", "WHAT", "LDIR", "PDIR", "A B!"})
    for (uint32_t flags : {0u, 1u})
      mutation("closed grammar " + type, [&](auto& f) {
        fixture::P p;
        p.type = type;
        p.flags = flags;
        f.physical.push_back(p);
      });
  mutation("duplicate EXMT", [](auto& f) { f.physical.push_back(f.physical.front()); });
  mutation("missing EXMT", [](auto& f) { f.physical.erase(f.physical.begin()); });
  mutation("TOWR after BODY", [](auto& f) { std::swap(f.physical[1], f.physical.back()); });
  mutation("wrong required flag", [](auto& f) { f.physical[1].flags = 0; });
  mutation("reserved flags", [](auto& f) { f.physical[1].flags = 3; });
  mutation("optional codec", [](auto& f) {
    fixture::P p;
    p.type = "PROV";
    p.flags = 0;
    p.codec = 7;
    p.bytes = fixture::text("{}");
    f.physical.push_back(p);
  });
  mutation("PROV before elements", [](auto& f) {
    fixture::P p;
    p.type = "PROV";
    p.flags = 0;
    p.bytes = fixture::text("{}");
    f.physical.insert(f.physical.begin() + 1, p);
  });
  mutation("duplicate PROV", [](auto& f) {
    fixture::P p;
    p.type = "PROV";
    p.flags = 0;
    p.bytes = fixture::text("{}");
    f.physical.push_back(p);
    f.physical.push_back(p);
  });
  mutation("BALL without RAYS", [](auto& f) {
    fixture::P p;
    p.type = "BALL";
    f.physical.push_back(p);
  });
  mutation("PREC without RAYS", [](auto& f) {
    fixture::P p;
    p.type = "PREC";
    p.flags = 0;
    f.physical.push_back(p);
  });
  mutation(
      "RAYS without BALL", [](auto& f) { f.physical.pop_back(); }, true);
  mutation(
      "extra BALL", [](auto& f) { f.physical.push_back(f.physical.back()); }, true);
  mutation(
      "duplicate RAYS", [](auto& f) { f.physical.push_back(f.physical[f.physical.size() - 2]); },
      true);
  mutation(
      "BALL wrong ray index", [](auto& f) { f.physical.back().object = 1; }, true);
  mutation(
      "BALL count", [](auto& f) { f.physical.back().count = 3; }, true);
  meta("unknown EXMT key", "{\"d\"", "{\"body\":{},\"d\"");
  meta("missing EXMT key", "\"required_features\":[],", "");
  meta("format", "\"format\":\"SICX\"", "\"format\":\"sicx\"");
  meta("metadata version", "\"version\":[\"1\",\"0\"]", "\"version\":[\"1\",\"1\"]");
  meta("logical features", "\"required_features\":[]", "\"required_features\":[\"unknown\"]");
  for (const string& d : {"0", "1", "1048577", "04", "-0", "1000000000000000000"})
    meta("d bound/spelling " + d, "\"d\":\"4\"", "\"d\":\"" + d + "\"");
  for (const string& deg : {"[]", "[\"1\"]", "[\"4\"]", "[\"37\"]"})
    meta("degrees " + deg, "\"degrees\":[\"2\"]", "\"degrees\":" + deg);
  meta("huge prime degree admission", "\"degrees\":[\"2\"]",
       "\"degrees\":[\"999999999999999989\"]");
  meta("tower level count", "\"degrees\":[\"2\"]",
       "\"degrees\":" + fixture::nums(vector<Uint64>(63, 2)));
  meta("tower degree product", "\"degrees\":[\"2\"]",
       "\"degrees\":" + fixture::nums(vector<Uint64>(41, 2)));
  meta("Smith multiplication overflow", "\"smith\":[\"2\"]",
       "\"smith\":" + fixture::nums(vector<Uint64>(64, 2)));
  meta("scale decimal", "\"scale\":\"2\"", "\"scale\":\"1x\"");
  meta("scale length", "\"scale\":\"2\"", "\"scale\":\"" + string(4096, '1') + "\"");
  meta("branches count", "\"branches\":[[", "\"branches\":[[\"0 0 0 0\",\"0 0 0 0\"],[");
  meta("branch bits power", "\"branch_bits\":\"128\"", "\"branch_bits\":\"129\"");
  meta("branch bits minimum", "\"branch_bits\":\"128\"", "\"branch_bits\":\"256\"");
  meta("unknown tower key", "\"branch_bits\":\"128\"", "\"bad\":\"0\",\"branch_bits\":\"128\"");
  for (const string& smith : {"[]", "[\"1\"]", "[\"3\"]", "[\"2\",\"2\"]"})
    meta("Smith " + smith, "\"smith\":[\"2\"]", "\"smith\":" + smith);
  meta("word length", "\"unit_generator_words\":[[\"0\"]]", "\"unit_generator_words\":[[]]");
  meta("word residue", "\"unit_generator_words\":[[\"0\"]]", "\"unit_generator_words\":[[\"2\"]]");
  meta("rescaling rule", "sicx-2adic-greedy/1.0", "wrong");
  meta("rescaling count", "\"shifts\":[\"0\"]", "\"shifts\":[]");
  meta("rescaling signed", "\"shifts\":[\"0\"]", "\"shifts\":[\"-1\"]");
  meta("entry rule", "sicx-divisors/1.0", "wrong");
  meta("entry representatives", "\"representatives\":[\"0\",\"1\",\"2\"]",
       "\"representatives\":[\"0\",\"2\"]");
  meta("wrong kind", "\"kind\":\"wsq\"", "\"kind\":\"fcoef\"");
  meta("wrong field level", "\"level\":\"0\"", "\"level\":\"1\"");
  meta("unknown element key", "\"kind\":\"wsq\"", "\"axis\":\"0\",\"kind\":\"wsq\"");
  meta("level outside field", "\"level\":\"0\",\"levels\":[]",
       "\"level\":\"0\",\"levels\":[\"0\"]");
  meta("duplicate levels", "\"kind\":\"rep\",\"levels\":[]",
       "\"kind\":\"rep\",\"levels\":[\"0\",\"0\"]");
  meta("missing representative", "\"position\":\"2\"", "\"position\":\"4\"");
  for (const string& scale : {"0", "01", "-1"})
    meta("scale " + scale, "\"scale\":\"2\"", "\"scale\":\"" + scale + "\"");
  meta("max_bits bound", "\"max_bits\":\"2\"", "\"max_bits\":\"8388609\"");
  meta("max_bits unattained", "\"max_bits\":\"2\"", "\"max_bits\":\"3\"");
  meta("max_bits exceeded", "\"max_bits\":\"2\"", "\"max_bits\":\"1\"");
  meta("residual rescaling", "\"scale\":\"2\"", "\"scale\":\"4\"");
  mutation("nonprimitive", [](auto& f) {
    f.leaves[0].bytes = fixture::leaf(0, 0, 1, {{0, 2, 0}});
    fixture::replace(f.meta, "\"max_bits\":\"2\"", "\"max_bits\":\"3\"");
    f.rebuild_physical(false);
  });
  mutation("zero convention", [](auto& f) {
    f.leaves[0].bytes = fixture::leaf(0, 0, 1, {});
    f.rebuild_physical(false);
  });
  auto rawleaf = [&](const string& n, fixture::ByteBuffer bytes) {
    mutation(n, [&](auto& f) {
      f.leaves[0].bytes = bytes;
      f.rebuild_physical(false);
    });
  };
  rawleaf("wrong leaf element", {1, 0, 1, 3, 2, 1, 0});
  rawleaf("wrong leaf first", {0, 1, 1, 3, 2, 1, 0});
  rawleaf("wrong leaf count", {0, 0, 2, 3, 2, 1, 0});
  rawleaf("redundant varint", {0x80, 0, 0, 1, 3, 2, 1, 0});
  rawleaf("varint truncated", {0x80});
  rawleaf("varint overflow", {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 2});
  rawleaf("varint too long", fixture::ByteBuffer(11, 0x80));
  rawleaf("empty run", {0, 0, 1, 0});
  rawleaf("overrun", {0, 0, 1, 4});
  rawleaf("underrun", {0, 0, 1});
  rawleaf("negative zero", {0, 0, 1, 3, 1, 0});
  rawleaf("magnitude noncanonical", {0, 0, 1, 3, 4, 1, 0, 0});
  rawleaf("magnitude truncated", {0, 0, 1, 3, 4, 1});
  rawleaf("zero data run", {0, 0, 1, 3, 0, 0});
  rawleaf("leaf trailing byte", {0, 0, 1, 3, 2, 1, 0, 0});
  mutation("nonminimal level set", [](auto& f) {
    fixture::replace(f.meta, fixture::descriptor("rep", 0, {}, "1", 1),
                     fixture::descriptor("rep", 0, {0}, "1", 1));
    f.leaves[1].count = 2;
    f.leaves[1].bytes = fixture::leaf(1, 0, 2, {{0, 1, 0}});
    f.rebuild_physical(false);
  });
  mutation("nonmaximal zero runs", [](auto& f) {
    fixture::replace(f.meta, fixture::descriptor("rep", 0, {}, "1", 1),
                     fixture::descriptor("rep", 0, {0}, "1", 1));
    f.leaves[1].count = 2;
    f.leaves[1].bytes = {1, 0, 2, 2, 2};
    f.rebuild_physical(false);
  });
  mutation("nonmaximal data runs", [](auto& f) {
    fixture::replace(f.meta, fixture::descriptor("rep", 0, {}, "1", 1),
                     fixture::descriptor("rep", 0, {0}, "1", 1));
    f.leaves[1].count = 2;
    f.leaves[1].bytes = {1, 0, 2, 3, 2, 1, 0, 3, 2, 1, 0};
    f.rebuild_physical(false);
  });
  {
    auto f = fixture::odd(true);
    fixture::replace(f.meta, fixture::descriptor("actdelta", 0, {}, "1", 1) + ",", "");
    negative("missing actdelta", f.encode());
  }
  {
    auto f = fixture::odd(true);
    fixture::replace(f.meta, fixture::descriptor("actodd", 0, {}, "1", 1, 0, 1),
                     fixture::descriptor("actodd", 0, {}, "1", 1, 0, 2));
    negative("incomplete odd action coefficients", f.encode());
  }
  {
    auto f = fixture::odd(true);
    for (int k = 0; k < 3; ++k)
      fixture::replace(f.meta,
                       fixture::descriptor("actodd", 0, {}, "1", k == 1 ? 1 : 0, 0, k) + ",", "");
    negative("actdelta without action axis", f.encode());
  }
  {
    auto f = fixture::odd();
    fixture::replace(f.meta, fixture::descriptor("fcoef", 0, {}, "2", 2, -1, 0),
                     fixture::descriptor("fcoef", 0, {}, "1", 0, -1, 0));
    f.leaves[0].bytes = fixture::leaf(0, 0, 1, {});
    f.rebuild_physical(false);
    negative("non-simple odd polynomial", f.encode());
  }
  {
    auto f = fixture::odd();
    fixture::replace(f.meta, fixture::descriptor("fcoef", 0, {}, "2", 2, -1, 0),
                     fixture::descriptor("fcoef", 0, {}, "1", 0, -1, 0));
    fixture::replace(f.meta, fixture::descriptor("fcoef", 0, {}, "1", 0, -1, 1),
                     fixture::descriptor("fcoef", 0, {}, "1", 1, -1, 1));
    f.leaves[0].bytes = fixture::leaf(0, 0, 1, {});
    f.leaves[1].bytes = fixture::leaf(1, 0, 1, {{0, -1, 0}});
    f.rebuild_physical(false);
    negative("zero root odd polynomial", f.encode());
  }
  {
    auto f = fixture::dyadic();
    fixture::replace(f.meta, "1 0 1 -80", "100000000000000000000000000000001 -80 1 -80");
    negative("wrong half-integer tie rounding", f.encode());
  }
  {
    auto f = fixture::dyadic(true);
    fixture::replace(f.meta, "\"branch_bits\":\"256\"", "\"branch_bits\":\"128\"");
    negative("branch precision below minimum", f.encode());
  }
  mutation("rescaling undo/reapply mismatch", [](auto& f) {
    fixture::replace(f.meta, "\"shifts\":[\"0\"]", "\"shifts\":[\"3\"]");
    fixture::replace(f.meta, "\"scale\":\"2\"", "\"scale\":\"4\"");
    fixture::replace(f.meta, "\"max_bits\":\"2\"", "\"max_bits\":\"3\"");
  });
  mutation("zero element scale", [](auto& f) {
    f.leaves.back().bytes = fixture::leaf(3, 0, 1, {});
    fixture::replace(f.meta, fixture::descriptor("rep", 2, {}, "1", 1),
                     fixture::descriptor("rep", 2, {}, "2", 0));
    f.rebuild_physical(false);
  });
  mutation("zero element max_bits", [](auto& f) {
    f.leaves.back().bytes = fixture::leaf(3, 0, 1, {});
    f.rebuild_physical(false);
  });
  {
    fixture::File f(4, false, true);
    fixture::replace(f.meta, "\"levels\":[\"0\",\"1\",", "\"levels\":[\"1\",\"0\",");
    negative("decreasing level set", f.encode());
  }
  mutation("duplicate representative",
           [](auto& f) { fixture::replace(f.meta, "\"position\":\"2\"", "\"position\":\"1\""); });
  mutation("missing element", [](auto& f) {
    fixture::replace(f.meta, "," + fixture::descriptor("rep", 2, {}, "1", 1), "");
  });
  mutation("extra element", [](auto& f) {
    fixture::replace(f.meta, fixture::descriptor("rep", 2, {}, "1", 1),
                     fixture::descriptor("rep", 2, {}, "1", 1) + "," +
                         fixture::descriptor("rep", 4, {}, "1", 1));
  });
  mutation("non-leaf claiming coverage", [](auto& f) {
    fixture::P p;
    p.type = "PROV";
    p.flags = 0;
    p.first_leaf = 1;
    p.leaf_count = 1;
    p.bytes = fixture::text("{}");
    f.physical.push_back(p);
  });
  for (const string& type : {"PROV", "PREC"})
    mutation(
        "optional flags must be zero",
        [&](auto& f) {
          fixture::P p;
          p.type = type;
          p.flags = 1;
          p.bytes = fixture::text("{}");
          f.physical.push_back(p);
        },
        type == "PREC");
  mutation(
      "PREC before BALL",
      [](auto& f) {
        fixture::P p;
        p.type = "PREC";
        p.flags = 0;
        p.bytes = fixture::text("{}");
        f.physical.insert(f.physical.end() - 1, p);
      },
      true);
  mutation(
      "all BALL radii zero",
      [](auto& f) {
        fixture::set(f.physical.back().bytes, 16, 0);
        fixture::set(f.physical.back().bytes, 24, 0);
      },
      true);
  // Fully rehashed auxiliary JSON mutations.
  auto auxiliary = [&](const string& name, const string& type, const string& json) {
    mutation(
        name,
        [&](auto& f) {
          fixture::P p;
          p.type = type;
          p.flags = 0;
          p.bytes = fixture::text(json);
          f.physical.push_back(p);
        },
        type == "PREC");
  };
  auxiliary("PROV override", "PROV", "{\"generator_rescaling\":{}}");
  auxiliary("PROV type", "PROV", "[]");
  auxiliary("PREC unknown", "PREC", "{\"unknown\":\"1\"}");
  auxiliary("PREC decimal", "PREC", "{\"delta_bits\":\"01\"}");
  for (const string& json :
       {"{ \"x\":\"a\"}", "{\"x\":1}", "{\"x\":true}", "{\"x\":false}", "{\"x\":null}",
        "{\"x\":\"a\",\"x\":\"b\"}", "{\"z\":\"a\",\"a\":\"b\"}", "{\"x\":\"\\n\"}",
        "{\"x\":\"\\/\"}", "{\"x\":\"\\u0020\"}", "{\"x\":\"\\u000A\"}", "{\"x\":\"\\ud800\"}",
        "{}x"})
    auxiliary("canonical JSON " + json, "PROV", json);
  for (const string& bad :
       {string("\xc0\x80", 2), string("\xed\xa0\x80", 3), string("\xf4\x90\x80\x80", 4),
        string("\x80", 1), string("\xe2\x82", 2)})
    auxiliary("UTF-8", "PROV", string("{\"x\":\"") + bad + "\"}");
  for (const string& label : {"x0", "c00", "c-1", "c", "c1000000000000000000"})
    raymeta("label " + label, "\"label\":\"c0\"", "\"label\":\"" + label + "\"");
  raymeta(
      "RAYS empty",
      "\"rays\":[{\"accuracy_bits\":\"127\",\"anchor\":\"0\",\"label\":\"c0\",\"word\":[\"0\"]}]",
      "\"rays\":[]");
  raymeta("anchor range", "\"anchor\":\"0\"", "\"anchor\":\"4\"");

  raymeta("ray word range", "\"word\":[\"0\"]", "\"word\":[\"2\"]");
  raymeta("ray word length", "\"word\":[\"0\"]", "\"word\":[]");
  raymeta("ball limbs zero", "\"limbs\":\"1\"", "\"limbs\":\"0\"");
  raymeta("ball limbs high", "\"limbs\":\"1\"", "\"limbs\":\"1025\"");
  raymeta("working bits", "\"working_bits\":\"128\"", "\"working_bits\":\"0\"");
  raymeta("accuracy tight", "\"accuracy_bits\":\"127\"", "\"accuracy_bits\":\"128\"");
  raymeta("accuracy range", "\"accuracy_bits\":\"127\"", "\"accuracy_bits\":\"1048577\"");
  raymeta("unknown RAYS key", "{\"balls\"", "{\"bad\":{},\"balls\"");
  for (const string& decoder :
       {"{\"name\":\"x\",\"sha256\":\"abc\"}",
        "{\"name\":\"\",\"source_commit\":\"0000000000000000000000000000000000000000\"}",
        "{\"name\":\"x\",\"source_commit\":\"ABC\"}"})
    raymeta("decoder", ",\"rays\":", ",\"decoder\":" + decoder + ",\"rays\":");
  for (const string& name :
       {"sic-decode/2 (contraction: acb_dot per class value)",       // identity-negative
        "sic_decode_sicx/2 (contraction: acb_dot per class value)",  // identity-negative
        "sic-decode/1", "sic-decode/1.0.0", "sic-decode/1.0.1",      // identity-negative
        "other/1.0.0"})                                              // identity-negative
    raymeta("decoder identifier", ",\"rays\":",
            ",\"decoder\":{\"name\":\"" + name +
                "\",\"source_commit\":\"0000000000000000000000000000000000000000\"},\"rays\":");
  for (const auto& [name, off, value] : std::vector<std::tuple<string, size_t, Uint64>>{
           {"flags", 0, 2},
           {"negative zero", 40, 1},
           {"zero exponent", 48, 1},
           {"even midpoint", 32, 2},
           {"radius even", 16, 2},
           {"radius bound", 16, 1ull << 30},
           {"zero radius exponent", 64, 1},
           {"exponent range", 8, (1ull << 40) + 1},
           {"radius exponent range", 24, (1ull << 40) + 1},
           {"anchor negative", 0, 1},
           {"anchor imaginary", 72, 1},
           {"norm", 32, 3},
           {"all radii zero", 16, 0}})
    mutation(
        "BALL " + name, [&](auto& f) { fixture::set(f.physical.back().bytes, off, value); }, true);
  mutation(
      "BALL short", [](auto& f) { f.physical.back().bytes.pop_back(); }, true);
  for (const string& label : {"c0", "c4"})
    raymeta(
        "duplicate/non-increasing numeric label",
        "\"rays\":[{\"accuracy_bits\":\"127\",\"anchor\":\"0\",\"label\":\"c0\",\"word\":[\"0\"]}]",
        "\"rays\":[{\"accuracy_bits\":\"127\",\"anchor\":\"0\",\"label\":\"" + label +
            "\",\"word\":[\"0\"]},{\"accuracy_bits\":\"127\",\"anchor\":\"0\",\"label\":\"c0\","
            "\"word\":[\"0\"]}]");
  // Arbitrary canonical-looking branch strings must also pass exact rounding, not merely isolation.
  for (const string& part : {"0 1 0 0", "2 0 0 0", "1 0 2 -80", "1 0 40000001 -80", "01 0 0 0",
                             "-0 0 0 0", "1 0 0 1", "1 0 -1 -80", "1 0 1 -080", "1 0 1 -8A"}) {
    fixture::File f;
    fixture::replace(f.meta, fixture::sqrt2_box(), part);
    negative("Arb syntax/normalization", f.encode());
  }
  {
    fixture::File f;
    auto branch = fixture::sqrt2_box();
    auto larger = branch;
    fixture::replace(larger, " 1 -80", " 3 -80");
    fixture::replace(f.meta, branch, larger);
    negative("noncanonical isolation radius", f.encode());
  }
  {
    fixture::File f;
    fixture::replace(f.meta, fixture::sqrt2_box(), "1 0 0 0");
    negative("wrong exact selected root", f.encode());
  }
}

void fuzz_cases() {
  std::mt19937_64 rng(0x5349435831);
  auto original = fixture::File(8, true).encode();
  for (unsigned i = 0; i < 3000; ++i) {
    auto b = original;
    size_t j = rng() % b.size();
    b[j] ^= 1u << (rng() % 8);
    try {
      Input in;
      in.open(save(b));
    } catch (const Reject&) {
      ++passed;
      continue;
    } catch (const Refuse&) {
      ++passed;
      continue;
    }
    throw std::runtime_error("one-byte corruption accepted at offset " + std::to_string(j));
  }
}
}  // namespace sic_check::crt::test
