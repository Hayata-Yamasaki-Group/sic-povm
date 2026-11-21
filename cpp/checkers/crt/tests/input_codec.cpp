// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
#include "test_general.hpp"
namespace sic_check::crt::test {
namespace filesystem = std::filesystem;
using namespace sic_check::crt::sicx;
void boundary_cases() {
  for (Uint64 d : {4, 8}) {
    fixture::File a(d, false, true);
    auto x = a.encode();
    a.rebuild_physical(true);
    auto y = a.encode();
    Input i, j;
    i.open(save(x));
    j.open(save(y));
    require(i.exact_root == j.exact_root, "rechunk changed exact root");
    require(i.merkle != j.merkle, "rechunk failed to change file root");
    ++passed;
    auto before = j.exact_root;
    auto file = j.merkle;
    a.finalize();
    auto finalized = a.encode();
    Uint64 old_lo = fixture::get(y, y.size() - 112),
           new_lo = fixture::get(finalized, finalized.size() - 112),
           new_po = fixture::get(finalized, finalized.size() - 112 + 16);
    require(std::equal(y.begin(), y.begin() + old_lo, finalized.begin()),
            "finalization changed exact prefix");
    require(
        fixture::ByteBuffer(y.begin() + old_lo, y.begin() + fixture::get(y, y.size() - 112 + 16)) ==
            fixture::ByteBuffer(finalized.begin() + new_lo, finalized.begin() + new_po),
        "finalization changed LDIR bytes");
    Input k;
    k.open(save(finalized));
    require(k.exact_root == before && k.merkle != file, "finalization root invariants");
    ++passed;
  }
  // Empty tree and unbalanced RFC tree and SHA known answers, compared with OpenSSL.
  for (size_t n = 0; n < 20; ++n) {
    std::vector<fixture::ByteBuffer> entries;
    vector<Hash> leaves;
    for (size_t i = 0; i < n; ++i) {
      fixture::ByteBuffer b(i + 1, (uint8_t)i);
      entries.push_back(b);
      leaves.push_back(tree_leaf(b.data(), b.size()));
    }
    require(tree(leaves, 0, n) == fixture::tree(entries, 0, n), "RFC6962 independent oracle");
    ++passed;
  }
  const string abc = "abc";
  require(hex(sha(abc.data(), abc.size())) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 golden");
  ++passed;
  auto bytes = fixture::File().encode();
  for (bool objects : {false, true}) {
    fixture::File f;
    fixture::P p;
    p.type = "PROV";
    p.flags = 0;
    string deep = "\"x\"";
    for (int i = 0; i < 33; ++i) deep = objects ? "{\"x\":" + deep + "}" : "[" + deep + "]";
    p.bytes = fixture::text("{\"x\":" + deep + "}");
    f.physical.push_back(p);
    refusal("JSON nesting", f.encode(), [](auto&) {});
  }
  refusal("chunk limit", bytes, [](auto& in) { in.limits.chunk_bytes = 1; });
  refusal("JSON limit", bytes, [](auto& in) { in.limits.json_bytes = 1; });
  refusal("directory limit", bytes, [](auto& in) { in.limits.directory_bytes = 1; });
  refusal("entry limit", bytes, [](auto& in) { in.limits.directory_entries = 1; });
  refusal("algebraic limit", fixture::odd().encode(),
          [](auto& in) { in.limits.algebraic_degree = 1; });
  refusal("algebraic height", fixture::odd().encode(),
          [](auto& in) { in.limits.algebraic_bits = 128; });
  refusal(
      "branch bits limit", bytes, [](auto& in) { in.limits.branch_bits = 64; },
      "branch precision limit");
  refusal("constraint limit", bytes, [](auto& in) { in.limits.rescaling_constraints = 1; });
  refusal("pair limit", bytes, [](auto& in) { in.limits.coordinate_pairs = 0; });
  // Declared huge sizes are refused/rejected before allocating.
  auto b = bytes;
  fixture::set(b, 16 + 8, UINT64_MAX);
  negative("header length huge", b);
}

void primitive_cases() {
  {
    Input in;
    vector<fmpz> x, y;
    bool caught = false;
    try {
      in.coordinates(SIZE_MAX, x, y);
    } catch (const Reject& e) {
      require(string(e.what()).find("element id") != string::npos,
              "coordinate API index rejection category");
      caught = true;
    }
    require(caught && x.empty() && y.empty(), "invalid coordinate API index modified output");
    ++passed;
  }
  auto fails = [&](const string& category, const auto& fn, bool limit = false) {
    try {
      fn();
    } catch (const Reject& e) {
      require(!limit && string(e.what()).find(category) != string::npos,
              "wrong primitive rejection for " + category + ": " + e.what());
      ++passed;
      return;
    } catch (const Refuse& e) {
      require(limit && string(e.what()).find(category) != string::npos,
              "wrong primitive refusal for " + category + ": " + e.what());
      ++passed;
      return;
    }
    throw std::runtime_error("primitive guard missing: " + category);
  };
  fails("addition overflow", [&] { add(UINT64_MAX, 1); });
  fails("multiplication overflow", [&] { mul(UINT64_MAX, 2); });
  fails(
      "SHA-256 message size", [&] { sha_message_size(UINT64_MAX); }, true);
  for (const auto& [s, category] : std::vector<std::pair<string, string>>{
           {"", "unexpected end"},
           {"[]x", "trailing bytes"},
           {"[", "unexpected end"},
           {"[\"x\"", "unterminated array"},
           {"[\"x\"!", "bad array"},
           {"{!", "object key expected"},
           {"{\"a\"!", "':' expected"},
           {"{\"a\":\"b\"", "unterminated object"},
           {"{\"a\":\"b\"!", "bad object"},
           {"1", "only strings"},
           {"\"x", "unterminated string"},
           {string("\"\n\""), "raw control"},
           {"\"\\", "bad escape"},
           {"\"\\u0\"", "must be \\u00"},
           {"\"\\u000A\"", "hex must be lowercase"},
           {"\"\\u0020\"", "escape for byte"},
           {"\"\\n\"", "disallowed escape"},
           {string("\"\xe2\"", 3), "truncated UTF-8"},
           {string("\"\xe2x\x80\"", 5), "invalid UTF-8 continuation"}}) {
    fails(category, [&] { JParser().parse_top((const Uint8*)s.data(), s.size()); });
  }
  Jv scalar, object;
  object.t = 'o';
  object.o.push_back({"x", scalar});
  fails("object expected", [&] { j_get(scalar, "x", 's'); });
  fails("missing key", [&] { j_get(object, "y", 's'); });
  fails("wrong JSON type", [&] { j_get(object, "x", 'a'); });
  fails("object expected", [&] { j_keys(scalar, {}, "test"); });
  fails("missing key", [&] { j_keys(object, {"x", "y"}, "test"); });
  fails("array expected", [&] { j_uvec(scalar, "test"); });
  Jv array;
  array.t = 'a';
  array.a.push_back(object);
  fails("string expected", [&] { j_uvec(array, "test"); });
  for (const auto& [b, category] : std::vector<std::pair<fixture::ByteBuffer, string>>{
           {{0x80}, "truncated"},
           {{0x80, 0}, "non-canonical"},
           {{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 2}, "value >="},
           {{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}, "longer than"}}) {
    fails(category, [&] {
      Cur c{b.data(), b.data() + b.size()};
      rd_varint(c);
    });
  }
  for (const auto& [b, category] : std::vector<std::pair<fixture::ByteBuffer, string>>{
           {{1}, "negative zero"}, {{2}, "truncated magnitude"}, {{2, 0}, "most significant"}}) {
    fails(category, [&] {
      Cur c{b.data(), b.data() + b.size()};
      Z z;
      rd_int(c, z.v);
    });
  }
  fixture::ByteBuffer large;
  fixture::var(large, 2 * ((1ull << 20) + 1));
  fails(
      "magnitude longer",
      [&] {
        Cur c{large.data(), large.data() + large.size()};
        Z z;
        rd_int(c, z.v);
      },
      true);
  fails("64 lowercase hex", [&] { parse_hash("00"); });
  fails("invalid lowercase hash", [&] { parse_hash(string(64, 'X')); });
  fails("negative decimal zero", [&] { signed_decimal("-0", "test"); });
  for (const auto& [s, category] :
       std::vector<std::pair<string, string>>{{"1 0 0", "exactly four"},
                                              {"-0 0 0 0", "noncanonical integer"},
                                              {"G 0 0 0", "lowercase hexadecimal"},
                                              {"2 0 0 0", "midpoint normalization"},
                                              {"1 0 2 0", "radius normalization"}}) {
    fails(category, [&] { BallString b(s); });
  }
  fails(
      "mantissa size", [&] { BallString b(string((1u << 21) + 1, '1') + " 0 0 0"); }, true);
  Input in;
  in.open(save(fixture::File().encode()), {}, false);
  fails("chunk index out of range", [&] { in.payload(SIZE_MAX); });
  fails("chunk index", [&] { in.verify_chunk(SIZE_MAX); });
  fails("chunk index", [&] { in.read_chunk(SIZE_MAX); });
  fails("exact element chunk", [&] { in.read_chunk(0); });
  fails("element id out of range", [&] { in.element(SIZE_MAX); });
  fails("element id", [&] {
    vector<fmpz> x, y;
    in.coordinates(SIZE_MAX, x, y);
  });
  in.limits.dense_coordinates = 0;
  fails(
      "dense coordinate",
      [&] {
        vector<fmpz> x, y;
        in.coordinates(0, x, y);
      },
      true);
  fails("read past end", [&] {
    Uint8 b;
    in.f.read(in.f.size, &b, 1);
  });
  fails("read past end", [&] { in.f.read(in.f.size, 1); });
  fails("only be opened once", [&] { in.f.open(in.path); });
  fails("cannot open", [&] {
    File f;
    f.open((scratch / "missing").string());
  });
  {
    int ends[2];
    require(pipe(ends) == 0, "pipe failed");
    close(ends[1]);
    fails("file seek failed", [&] {
      File f;
      f.open("/proc/self/fd/" + std::to_string(ends[0]));
    });
    close(ends[0]);
  }
  {
    int ends[2];
    require(pipe(ends) == 0, "pipe failed");
    close(ends[1]);
    File f;
    f.f = fdopen(ends[0], "rb");
    f.size = 1;
    fails("read error", [&] {
      Uint8 b;
      f.read(0, &b, 1);
    });
  }
  fails("read past end", [&] { in.f.read(in.f.size + 1, 0); });
  in.ch[1].codec = 1;
  fails("unsupported codec", [&] { in.payload(1); });
  in.ch[1].codec = 0;
  in.limits.chunk_bytes = 1;
  fails(
      "physical payload", [&] { in.payload(1); }, true);
  in.limits.chunk_bytes = 256ull << 20;
  ++in.ch[1].decoded;
  fails("raw decoded length", [&] { in.payload(1); });
  --in.ch[1].decoded;
  in.limits.coordinate_pairs = 0;
  fails(
      "physical chunk nonzero", [&] { in.read_chunk(1); }, true);
  {
    std::fstream file(in.path, std::ios::in | std::ios::out | std::ios::binary);
    file.seekp(in.ch[1].off + 16);
    file.put('x');
    file.close();
    filesystem::last_write_time(in.path,
                                filesystem::last_write_time(in.path) + std::chrono::seconds(1));
    fails("source snapshot changed", [&] { in.payload(1); });
  }
  Input algebraic;
  AlgebraicNumber two(2), root;
  qqbar_sqrt(root.v, two.v);
  algebraic.limits.algebraic_degree = 1;
  fails(
      "degree/height limit", [&] { algebraic.algebraic_limit(root); }, true);
  fails(
      "quadratic branch degree/height", [&] { algebraic.sqrt_checked(two); }, true);
  algebraic.limits.algebraic_degree = 3;
  fails(
      "operation degree/height", [&] { algebraic.op(root, root, '+'); }, true);
  algebraic.limits.algebraic_degree = 4096;
  algebraic.limits.algebraic_bits = 8;
  fails(
      "resultant height", [&] { algebraic.op(AlgebraicNumber(1), AlgebraicNumber(1), '+'); }, true);
  algebraic.limits.algebraic_bits = 1 << 20;
  fails("zero trace-dual", [&] { algebraic.op(AlgebraicNumber(1), AlgebraicNumber(), '/'); });
  fails(
      "dyadic exponent limit", [&] { algebraic.scaled(AlgebraicNumber(1), (1 << 20) + 1); }, true);
  algebraic.limits.algebraic_bits = 2;
  fails(
      "dyadic height", [&] { algebraic.scaled(AlgebraicNumber(2), 1); }, true);
  {
    Input bounded;
    bounded.limits.branch_bits = 64;
    fails(
        "rounding precision limit", [&] { bounded.component_box(root, 128); }, true);
    AlgebraicNumber x = root;
    // [0,2] isolates +sqrt(2), but the interval derivative contains zero.
    arb_one(acb_realref(QQBAR_ENCLOSURE(x.v)));
    arb_add_error_2exp_si(acb_realref(QQBAR_ENCLOSURE(x.v)), 0);
    fails(
        "comparison precision limit", [&] { bounded.real_sign(x); }, true);
    arb_indeterminate(acb_realref(QQBAR_ENCLOSURE(x.v)));
    A enclosure;
    fails(
        "nonfinite algebraic comparison", [&] { bounded.real_enclosure(enclosure.v, x, 64); },
        true);
  }
  // Exercise the algebraic fallback independently of the certified quadratic path.
  for (int kind = 0; kind < 3; ++kind) {
    fixture::File f;
    if (kind == 0) fixture::replace(f.meta, "\"branch_bits\":\"128\"", "\"branch_bits\":\"256\"");
    if (kind == 1) fixture::replace(f.meta, fixture::sqrt2_box(), "1 0 0 0");
    if (kind == 2) {
      auto branch = fixture::sqrt2_box();
      auto broad = branch;
      fixture::replace(broad, " 1 -80", " 3 -80");
      fixture::replace(f.meta, branch, broad);
    }
    Input a;
    a.open(save(f.encode()), {}, false);
    fails(kind == 0   ? "minimum canonical"
          : kind == 1 ? "no exact root"
                      : "canonical exact rounding",
          [&] { a.check_branches_algebraic(); });
  }
  {
    fixture::File f;
    f.leaves[0].bytes = fixture::leaf(0, 0, 1, {});
    fixture::replace(f.meta, fixture::descriptor("wsq", 0, {}, "2", 2),
                     fixture::descriptor("wsq", 0, {}, "1", 0));
    f.rebuild_physical(false);
    Input a;
    a.open(save(f.encode()), {}, false);
    fails("zero/non-simple quadratic", [&] { a.quadratic_branches(); });
    fails("zero/non-simple quadratic", [&] { a.check_branches_algebraic(); });
  }
}
}  // namespace sic_check::crt::test
