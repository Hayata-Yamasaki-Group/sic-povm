// SPDX-License-Identifier: AGPL-3.0-or-later
#include <iostream>

#include "test_context.hpp"
namespace sic_check::norm::test {
using namespace sic_check::norm;
using fixture::Document;
using fixture::Node;
using fixture::num;
using fixture::overwrite;
void codec_primitives(TestContext& context, std::filesystem::path& dir) {
  context.expect_invalid([&] { (void)add_checked(UINT64_MAX, 1); }, "checked addition overflow");
  context.expect_invalid([&] { (void)mul_checked(UINT64_MAX, 2); },
                         "checked multiplication overflow");
  context.expect_invalid([&] { (void)little(ByteBuffer(16), 0, 9); }, "fixed-width load bounds");
  context.expect_invalid([&] { (void)words_add({0}, {0}, {}); }, "word addition rank");
  Meta out_of_range;
  out_of_range.d = 2;
  context.expect_invalid([&] { (void)out_of_range.entry(2); }, "entry coordinate range");
  context.expect_invalid([&] { (void)parse_hash(std::string(63, '0')); }, "digest width");
  context.expect_invalid([&] { (void)parse_hash(std::string(65, '0')); }, "digest over-width");
  context.expect_invalid([&] { Reader missing((dir / "absent" / "file.sicx").string()); },
                         "missing source file");
  for (auto [s, diagnostic] :
       std::vector<std::pair<std::string, std::string>>{{"1 0 0 0 0", "Arb extra field"},
                                                        {"1 0 0", "Arb missing field"},
                                                        {"1  0 0", "Arb empty field"},
                                                        {"g 0 0 0", "Arb hexadecimal digit"}})
    context.expect_invalid(
        [&] {
          ComplexBall z;
          canonical_ball(acb_realref(z.v), s);
        },
        std::string("branch dump primitive ") + s, diagnostic);
  {
    Algebraic one, zero, quotient;
    qqbar_one(one.v);
    BoundedAlgebra arithmetic;
    context.expect_invalid([&] { arithmetic.div(quotient.v, one.v, zero.v); }, "division by zero");
    std::vector<Algebraic> polynomial(3);
    qqbar_set_si(polynomial[1].v, -1);
    qqbar_one(polynomial[2].v);
    context.expect_invalid([&] { (void)polynomial_roots(polynomial); }, "simple zero root");
    qqbar_one(polynomial[0].v);
    qqbar_set_si(polynomial[1].v, -2);
    context.expect_invalid([&] { (void)polynomial_roots(polynomial); }, "repeated root");
  }
  Json wrong_kind;
  wrong_kind.kind = Json::object;
  context.expect_invalid([&] { (void)wrong_kind.str(); }, "JSON string type");
  context.expect_invalid([&] { (void)wrong_kind.items(); }, "JSON array type");
  for (const ByteBuffer& malformed :
       {ByteBuffer{0x80}, ByteBuffer{0x80, 0},
        ByteBuffer{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 2}})
    context.expect_invalid(
        [&] {
          Cursor c(malformed);
          (void)c.varint();
        },
        "invalid unsigned LEB128");
  for (const ByteBuffer& malformed : {ByteBuffer{1}, ByteBuffer{2, 0}, ByteBuffer{4, 1}})
    context.expect_invalid(
        [&] {
          Cursor c(malformed);
          (void)c.integer();
        },
        "invalid integer primitive");
  const ByteBuffer trailing{0};
  context.expect_invalid(
      [&] {
        Cursor c(trailing);
        c.end();
      },
      "trailing canonical leaf bytes");
  check(hex(digest({})) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "SHA empty vector");
  check(hex(digest(fixture::bytes("abc"))) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA abc vector");
  check(hex(logical_digest(Logical{"TOWR", 0, 0, 1, {}}, ByteBuffer{0, 0, 1, 3, 2, 1, 0})) ==
            "1b329f4d0aaa2367266a53131762432a218072b6882eb4a9b012d083856ab936",
        "external domain-separated leaf hash vector");
  for (size_t n : {55, 56, 63, 64, 65, 1000}) {
    ByteBuffer b(n, 'a');
    HashState h;
    for (size_t i = 0; i < n;) {
      size_t count = std::min<size_t>(7, n - i);
      h.update(std::span(b).subspan(i, count));
      i += count;
    }
    check(h.finish() == digest(b), "SHA incremental padding");
  }
  for (uint64_t dimension : {2, 3, 4, 5, 6, 7, 8, 9, 12, 32}) {
    Document d(dimension);
    auto b = d.encode();
    context.run(b, "valid d" + std::to_string(dimension));
    write(dir / ("d" + std::to_string(dimension) + ".sicx"), b);
    Document exact(dimension, false);
    context.run(exact.encode(), "exact only");
    check(root(exact.encode(), 32) == root(b, 32), "exact root changed on finalization");
    check(root(exact.encode(), 64) != root(b, 64), "file root unchanged on finalization");
    auto alternate = d;
    alternate.prov.object["source"] = "independent fixture";
    check(root(alternate.encode(), 32) == root(b, 32), "PROV affected exact root");
    check(root(alternate.encode(), 64) != root(b, 64), "PROV did not affect file root");
    context.run(alternate.encode(), "PROV change");
  }
}

void corruption_fuzz_and_root_refusals(TestContext& context, std::filesystem::path& scratch,
                                       Document& original, ByteBuffer& valid, Document& odd,
                                       ReadOptions& large, Document& nonchain) {
  context.run(nonchain.encode(), "Smith divisibility chain deliberately not required", 0, large);
  uint64_t random = UINT64_C(0xf3ecac710001);
  auto next_random = [&]() {
    random ^= random << 13;
    random ^= random >> 7;
    random ^= random << 17;
    return random;
  };
  size_t fuzz_accepted = 0, fuzz_rejected = 0, fuzz_refused = 0;
  for (unsigned i = 0; i < 800; ++i) {
    ByteBuffer sample;
    if (i < 400) {
      sample = valid;
      sample[size_t(next_random() % sample.size())] ^= uint8_t(1u << (next_random() % 8));
    } else {
      auto d = original;
      size_t offset = size_t(next_random() % d.exmt.dump().size());
      uint8_t bit = uint8_t(1u << (next_random() % 8));
      d.edit_exmt = [=](auto& payload) { payload[offset] ^= bit; };
      sample = d.encode();
    }
    write(scratch, sample);
    try {
      Reader tested(scratch.string());
      check(i >= 400, "raw one-bit corruption accepted");
      ++fuzz_accepted;
    } catch (const InvalidInput&) {
      ++fuzz_rejected;
    } catch (const ResourceRefusal&) {
      ++fuzz_refused;
    }
  }
  std::cout << "deterministic fuzz seed 0xf3ecac710001: " << fuzz_accepted << " accepted, "
            << fuzz_rejected << " rejected, " << fuzz_refused << " refused (800 inputs)\n";

  auto zero_root = original;
  zero_root.leaves[0].payload = fixture::leaf_bytes(0, 0, {{0, 0}});
  zero_root.exmt["elements"].array[0]["max_bits"] = "0";
  zero_root.sync_leaves();
  context.run(zero_root.encode(), "zero and repeated quadratic root", 1);
  auto simple_zero = odd;
  simple_zero.leaves[0].payload = fixture::leaf_bytes(0, 0, {{0, 0}});
  simple_zero.leaves[1].payload = fixture::leaf_bytes(1, 0, {{-1, 0}});
  simple_zero.exmt["elements"].array[0]["max_bits"] = "0";
  simple_zero.exmt["elements"].array[1]["max_bits"] = "1";
  simple_zero.sync_leaves();
  context.run(simple_zero.encode(), "simple zero defining root", 1);
  auto repeated = odd;
  repeated.leaves[0].payload = fixture::leaf_bytes(0, 0, {{1, 0}});
  repeated.leaves[1].payload = fixture::leaf_bytes(1, 0, {{-1, 0}});
  repeated.exmt["elements"].array[0]["scale"] = "2";
  repeated.exmt["elements"].array[0]["max_bits"] = "2";
  repeated.exmt["elements"].array[1]["scale"] = "3";
  repeated.exmt["elements"].array[1]["max_bits"] = "2";
  repeated.sync_leaves();
  context.run(repeated.encode(), "repeated nonzero cubic defining root", 1);
}
}  // namespace sic_check::norm::test
