// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
namespace sic_check::norm::test {
using namespace sic_check::norm;
using fixture::Document;
using fixture::Node;
using fixture::num;
using fixture::overwrite;
void metadata_refusals(TestContext& context, Document& original) {
  context.exmt("unknown EXMT key", [](auto& j) { j.object["body"] = Node::o(); });
  context.exmt("missing EXMT key", [](auto& j) { j.object.erase("format"); });
  context.exmt("unknown format", [](auto& j) { j["format"] = "SIC3"; });
  context.exmt("EXMT version", [](auto& j) { j["version"].array[1] = "1"; });
  context.exmt("EXMT features", [](auto& j) { j["required_features"].array.push_back("future"); });
  for (const auto& value :
       {"0", "1", "3", "1048577", "04", "+4", "-0", "4x", "1000000000000000000"})
    context.exmt(std::string("dimension ") + value, [=](auto& j) { j["d"] = value; });
  context.exmt("degree composite", [](auto& j) { j["tower"]["degrees"].array[0] = "4"; });
  context.exmt("degree above scope", [](auto& j) { j["tower"]["degrees"].array[0] = "37"; });
  context.exmt("degree below scope", [](auto& j) { j["tower"]["degrees"].array[0] = "1"; });
  context.exmt("empty tower", [](auto& j) { j["tower"]["degrees"] = Node::a(); });
  context.exmt("tower bound", [](auto& j) { j["tower"]["degrees"].array.assign(41, "2"); });
  context.exmt(
      "tower allocation", [](auto& j) { j["tower"]["degrees"].array.assign(17, "2"); }, 2);
  context.exmt("branch count", [](auto& j) { j["tower"]["branches"] = Node::a(); });
  context.exmt("branch pair",
               [](auto& j) { j["tower"]["branches"].array[0] = Node::a({"0 0 0 0"}); });
  for (const auto& s : {"1280", "0", "64", "129"})
    context.exmt(std::string("branch_bits ") + s, [=](auto& j) { j["tower"]["branch_bits"] = s; });
  for (const auto& s : {"01 0 0 0", "2 -1 0 0", "0 1 0 0", "1 0 0 1", "1 0 2 -80",
                        "1 0 40000001 -80", "1 0 0 0 ", "+1 0 0 0", "1 0 -1 0", "1 0 1 -80"})
    context.exmt(std::string("branch string ") + s,
                 [=](auto& j) { j["tower"]["branches"].array[0].array[0] = s; });
  context.exmt("wrong branch",
               [](auto& j) { j["tower"]["branches"].array[0].array[0] = "3 0 0 0"; });
  context.exmt("nonminimal branch precision", [](auto& j) { j["tower"]["branch_bits"] = "256"; });
  context.exmt("Smith empty", [](auto& j) { j["galois"]["smith"] = Node::a(); });
  context.exmt("Smith less than two", [](auto& j) { j["galois"]["smith"].array[0] = "1"; });
  context.exmt("Smith does not divide", [](auto& j) { j["galois"]["smith"].array[0] = "3"; });
  context.exmt("Smith product", [](auto& j) { j["galois"]["smith"].array.push_back("2"); });
  context.exmt("word rank",
               [](auto& j) { j["galois"]["unit_generator_words"].array[0].array.clear(); });
  context.exmt("word residue",
               [](auto& j) { j["galois"]["unit_generator_words"].array[0].array[0] = "2"; });
  context.exmt("entry name",
               [](auto& j) { j["entry_rule"]["name"] = "sicx-other/1"; });  // identity-negative
  context.exmt("entry representatives",
               [](auto& j) { j["entry_rule"]["representatives"].array[1] = "2"; });
  context.exmt("entry representative count",
               [](auto& j) { j["entry_rule"]["representatives"].array.pop_back(); });
  context.exmt("rescaling rule", [](auto& j) { j["generator_rescaling"]["rule"] = "unknown"; });
  context.exmt("rescaling count",
               [](auto& j) { j["generator_rescaling"]["shifts"].array.clear(); });
  context.exmt("negative shift",
               [](auto& j) { j["generator_rescaling"]["shifts"].array[0] = "-1"; });
  context.exmt(
      "valid recorded nonzero shift",
      [](auto& j) { j["generator_rescaling"]["shifts"].array[0] = "1"; }, 0);
  context.exmt("element kind", [](auto& j) { j["elements"].array[0]["kind"] = "fcoef"; });
  context.exmt("element field level", [](auto& j) { j["elements"].array[0]["level"] = "1"; });
  context.exmt("missing element",
               [](auto& j) { j["elements"].array.erase(j["elements"].array.begin()); });
  context.exmt("extra element",
               [](auto& j) { j["elements"].array.push_back(j["elements"].array.back()); });
  context.exmt("element order",
               [](auto& j) { std::swap(j["elements"].array[1], j["elements"].array[2]); });
  context.exmt("level set range",
               [](auto& j) { j["elements"].array[0]["levels"] = Node::a({"0"}); });
  context.exmt("level set duplicate", [](auto& j) {
    j["elements"].array[1]["levels"] = Node::a({"0", "0"});
  });
  context.exmt("representative position",
               [](auto& j) { j["elements"].array[1]["position"] = "3"; });
  context.exmt("max_bits range", [](auto& j) { j["elements"].array[0]["max_bits"] = "8388609"; });
  context.exmt("max_bits exceeded", [](auto& j) { j["elements"].array[0]["max_bits"] = "0"; });
  context.exmt("max_bits unattained", [](auto& j) { j["elements"].array[0]["max_bits"] = "2"; });
  for (const auto& s : {"0", "01", "+1", "-1", "1x"})
    context.exmt(std::string("scale ") + s, [=](auto& j) { j["elements"].array[0]["scale"] = s; });
  context.exmt("scale length",
               [](auto& j) { j["elements"].array[0]["scale"] = std::string(4096, '1'); });
  auto encoded_leaf = [&](const std::string& name, ByteBuffer payload, int outcome = 1) {
    context.mutation(
        name,
        [=](auto& d) {
          d.leaves[0].payload = payload;
          d.sync_leaves();
        },
        outcome);
  };
  for (auto [name, b] : std::vector<std::pair<std::string, ByteBuffer>>{
           {"wrong leaf id", {1, 0, 1, 3, 2, 1, 0}},
           {"wrong leaf first", {0, 1, 1, 3, 2, 1, 0}},
           {"wrong leaf count", {0, 0, 2, 3, 2, 1, 0}},
           {"run zero", {0, 0, 1, 0}},
           {"run overrun", {0, 0, 1, 4}},
           {"run underrun", {0, 0, 1}},
           {"negative zero", {0, 0, 1, 3, 1, 0}},
           {"zero in data", {0, 0, 1, 3, 0, 0}},
           {"overlong varint", {0x80, 0, 0, 1, 3, 2, 1, 0}},
           {"overflow varint", {255, 255, 255, 255, 255, 255, 255, 255, 255, 2}},
           {"unterminated varint", {128}},
           {"integer leading zero", {0, 0, 1, 3, 4, 1, 0, 0}},
           {"truncated integer", {0, 0, 1, 3, 4, 1}},
           {"leaf trailing byte", {0, 0, 1, 3, 2, 1, 0, 0}}})
    encoded_leaf(name, b);
  ByteBuffer huge{0, 0, 1, 3};
  fixture::var(huge, 2 * ((1u << 20) + 1));
  encoded_leaf("integer magnitude refusal", huge, 2);
  context.mutation("primitive gcd", [](auto& d) {
    d.leaves[1].payload = fixture::leaf_bytes(1, 0, {{3, 0}});
    d.sync_leaves();
    d.exmt["elements"].array[1]["max_bits"] = "2";
  });
  context.mutation("residual rescaling", [](auto& d) {
    d.leaves[0].payload = fixture::leaf_bytes(0, 0, {{1, 0}});
    d.exmt["elements"].array[0]["scale"] = "4";
    d.exmt["elements"].array[0]["max_bits"] = "3";
    d.sync_leaves();
  });
  auto malformed_json = [&](const std::string& name, std::string payload) {
    // PROV has no mandatory key set: parser defects cannot be masked by EXMT's schema.
    context.mutation(name,
                     [=](auto& d) { d.edit_prov = [=](auto& b) { b = fixture::bytes(payload); }; });
  };
  for (const auto& text :
       {" {}", "{} ", "{\"a\":\"x\",\"a\":\"y\"}", "{\"b\":\"x\",\"a\":\"y\"}", "{\"a\":1}",
        "{\"a\":null}", "{\"a\":true}", "{\"a\":\"\\/\"}", "{\"a\":\"\\n\"}", "{\"a\":\"\\u0041\"}",
        "{\"a\":\"\\u001F\"}", "{\"a\":\"\\ud800\"}", "{", "?}", "{'a\":\"v\"}", "{\"a\"?\"b\"}",
        "{\"a\":\"b\"?\"c\":\"d\"}", "{\"a\":\"\\x0001\"}", "{\"a\":\"\\u000g\"}"})
    malformed_json(std::string("JSON ") + text, text);
  for (auto [name, value] :
       std::vector<std::pair<std::string, std::string>>{{"control", std::string(1, char(1))},
                                                        {"overlong lead", "\xc0\xaf"},
                                                        {"overlong three", "\xe0\x80\xaf"},
                                                        {"overlong four", "\xf0\x80\x80\xaf"},
                                                        {"surrogate", "\xed\xa0\x80"},
                                                        {"above maximum", "\xf4\x90\x80\x80"},
                                                        {"continuation", "\xc2\x41"},
                                                        {"invalid two-byte lead", "\x9f\xbf"},
                                                        {"lone continuation", "\x80"},
                                                        {"invalid lead", "\xf5\x80\x80\x80"},
                                                        {"truncated", "\xe2\x82"}})
    malformed_json("PROV UTF8 " + name, "{\"text\":\"" + value + "\"}");
  context.mutation(
      "valid free PROV text", [](auto& d) { d.prov.object["text"] = "A / text"; }, 0);
  auto ray = [&](const std::string& name, const std::function<void(Node&)>& edit) {
    context.mutation(name, [&](auto& d) { edit(d.rays); });
  };
  ray("RAYS unknown key", [](auto& j) { j.object["x"] = "x"; });
  ray("RAYS empty", [](auto& j) { j["rays"].array.clear(); });
  for (const auto& label : {"c00", "c01", "c-1", "c+1", "c", "C0", "x0", "c1000000000000000000"})
    ray(std::string("label ") + label, [=](auto& j) { j["rays"].array[0]["label"] = label; });
  ray("duplicate label", [](auto& j) { j["rays"].array.push_back(j["rays"].array[0]); });
  ray("descending label", [](auto& j) {
    auto x = j["rays"].array[0];
    x["label"] = "c1";
    j["rays"].array.insert(j["rays"].array.begin(), x);
  });
  auto nonzero_c0 = original;
  nonzero_c0.rays["rays"].array[0]["word"].array[0] = "1";
  context.run(nonzero_c0.encode(), "c0 nonzero word permitted");
  ray("ray residue", [](auto& j) { j["rays"].array[0]["word"].array[0] = "2"; });
  ray("ray rank", [](auto& j) { j["rays"].array[0]["word"].array.clear(); });
  ray("anchor range", [](auto& j) { j["rays"].array[0]["anchor"] = "4"; });
  ray("accuracy range", [](auto& j) { j["rays"].array[0]["accuracy_bits"] = "1048577"; });
  ray("accuracy not tight", [](auto& j) { j["rays"].array[0]["accuracy_bits"] = "126"; });
  for (auto [field, value] : std::vector<std::pair<std::string, std::string>>{
           {"limbs", "0"}, {"limbs", "1025"}, {"working_bits", "0"}, {"working_bits", "1048577"}})
    ray("balls " + field + value, [=](auto& j) { j["balls"][field] = value; });
  ray("decoder unsupported sha256 field", [](auto& j) {
    j["decoder"].object["sha256"] = std::string(64, 'a');
    j["decoder"].object.erase("source_commit");
  });
  for (const char* name :
       {"sic-decode/2 (contraction: acb_dot per class value)",       // identity-negative
        "sic_decode_sicx/2 (contraction: acb_dot per class value)",  // identity-negative
        "sic-decode/1", "sic-decode/1.0.0", "sic-decode/1.0.1",      // identity-negative
        "other/1.0.0"})                                              // identity-negative
    ray(std::string("decoder identifier ") + name, [=](auto& j) { j["decoder"]["name"] = name; });
  ray("decoder empty name", [](auto& j) { j["decoder"]["name"] = ""; });
  ray("decoder long name", [](auto& j) { j["decoder"]["name"] = std::string(257, 'a'); });
  for (const auto& source : {std::string(39, 'a'), std::string(40, 'A'), std::string(40, 'g')})
    ray("decoder source commit", [=](auto& j) { j["decoder"]["source_commit"] = source; });
  auto ball = [&](const std::string& name, size_t at, uint64_t value) {
    context.mutation(name, [=](auto& d) { overwrite(d.get("BALL").payload, at, value); });
  };
  ball("BALL flags", 0, 2);
  ball("BALL midpoint even", 32, 2);
  ball("BALL midpoint zero exponent", 32, 0);
  // The zero midpoint may carry a radius and E=0: that alone is canonical; gauge rejects it.
  ball("BALL radius even", 16, 2);
  ball("BALL radius limit", 16, (1u << 30) + 1);
  ball("BALL E range", 8, UINT64_C(1) << 41);
  ball("BALL e range", 24, uint64_t(-(INT64_C(1) << 41)));
  ball("BALL zero midpoint negative", 40, 1);
  ball("BALL zero midpoint E", 48, 1);
  ball("BALL zero radius e", 64, 1);
  ball("BALL all zero radii", 16, 0);
  ball("BALL anchor imaginary nonzero", 72, 1);
  ball("BALL anchor negative", 0, 1);
  ball("BALL norm", 32, 3);
  context.mutation("BALL truncated", [](auto& d) { d.get("BALL").payload.pop_back(); });
  context.mutation("BALL trailing bytes", [](auto& d) { d.get("BALL").payload.push_back(0); });
  context.mutation("PROV duplicate rescaling",
                   [](auto& d) { d.prov.object["generator_rescaling"] = Node::o(); });
  context.mutation("PROV object", [](auto& d) { d.prov = Node::a(); });
  context.mutation("PREC unknown key", [](auto& d) { d.prec.object["working_bits"] = "128"; });
  context.mutation("PREC numeric", [](auto& d) { d.prec.object["branch_bits"] = "-1"; });
  context.mutation("PREC object", [](auto& d) { d.prec = Node::a(); });
}

void trusted_root_routes(TestContext& context, std::filesystem::path& scratch, ByteBuffer& valid,
                         ReadOptions& trust) {
  trust.exact_root = root(valid, 32);
  trust.file_root = root(valid, 64);
  trust.whole_sha256 = digest(valid);
  context.run(valid, "all trusted identities", 0, trust);
  ReadOptions file_only, hash_only;
  file_only.file_root = root(valid, 64);
  hash_only.whole_sha256 = digest(valid);
  context.run(valid, "file trust authenticates exact root transitively", 0, file_only);
  context.run(valid, "whole hash authenticates both roots transitively", 0, hash_only);
  write(scratch, valid);
  Reader file_trusted(scratch.string(), file_only), hash_trusted(scratch.string(), hash_only);
  check(file_trusted.report().exact_trust == TrustRoute::file_root &&
            file_trusted.report().file_trust == TrustRoute::direct &&
            file_trusted.report().trusted_exact &&
            file_trusted.summary().find("via file root and complete LDIR") != std::string::npos,
        "file trust route reporting");
  check(hash_trusted.report().exact_trust == TrustRoute::whole_sha256 &&
            hash_trusted.report().file_trust == TrustRoute::whole_sha256 &&
            hash_trusted.report().trusted_exact && hash_trusted.report().trusted_file &&
            hash_trusted.summary().find("via whole-file SHA-256") != std::string::npos,
        "whole-hash trust route reporting");
  check(file_trusted.summary().find("spot check not performed") != std::string::npos,
        "optional spot check scope reporting");
}

void trusted_root_mismatches(TestContext& context, ByteBuffer& valid, ReadOptions& trust,
                             ReadOptions& wrong) {
  (*wrong.exact_root)[0] ^= 1;
  context.run(valid, "expected exact mismatch", 1, wrong);
  wrong = trust;
  (*wrong.file_root)[0] ^= 1;
  context.run(valid, "expected file mismatch", 1, wrong);
  wrong = trust;
  (*wrong.whole_sha256)[0] ^= 1;
}
}  // namespace sic_check::norm::test
