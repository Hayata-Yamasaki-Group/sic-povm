// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
namespace sic_check::verifier::test {
using namespace sic_check::verifier;
using namespace fixture;
void json_depth_refusals(unsigned& d, DimensionContext& dimension, Model& deep, string& nested) {
  nested += string(33, ']');
  deep.extras.push_back({"PROV", 0, bytes("{\"x\":" + nested + "}")});
  dimension.resource("JSON-depth-refusal", deep);
  auto objects = small(d);
  string nested_object = "{}";
  for (int i = 0; i < 33; ++i) nested_object = "{\"x\":" + nested_object + "}";
  objects.extras.push_back({"PROV", 0, bytes(nested_object)});
  dimension.resource("JSON-object-depth-refusal", objects);
  auto exponent = small(d);
  exponent.meta["tower"] =
      "{\"branch_bits\":\"128\",\"branches\":[[\"1 100001 0 0\",\"1 0 0 "
      "0\"]],\"degrees\":[\"2\"]}";
  dimension.resource("branch-exponent-refusal", exponent);
}

void payload_size_refusals(unsigned& d, DimensionContext& dimension, Model& bigInt,
                           ByteBuffer& encoded) {
  var(encoded, 2 * ((1u << 20) + 1));
  bigInt.elements[0].leaves[0] = encoded;
  dimension.resource("integer-length-refusal", bigInt);
  auto jsonSize = small(d);
  jsonSize.change_plan = [](auto& plan) {
    plan.push_back({"PROV", 0, ByteBuffer((64u << 20) + 1, 'x')});
  };
  dimension.resource("JSON-length-refusal", jsonSize);
}

void exact_decode_refusals(size_t& /* valid */, size_t& /* rejects */) {
  {
    Opts options;
    options.path = fixtures_dir + "/d4-valid-exact.sicx";
    Container c;
    open_container(c, options);
    verify_chunks(c, options);
    branch_check_exact(c, options);
    Opts wrong;
    wrong.path = fixtures_dir + "/d4-branch-wrong-root.sicx";
    Container invalid;
    open_container(invalid, wrong);
    verify_chunks(invalid, wrong);
    bool rejected = false;
    try {
      branch_check_exact(invalid, wrong);
    } catch (const Reject&) {
      rejected = true;
    }
    require(rejected, "exact fallback rejects zero selected roots");
    Integer d, x, y, r, back;
    fmpz_set_si(d, -3);
    for (auto [xx, yy, rr] : vector<std::tuple<Int64, Int64, Int64>>{{0, -1, -1}, {2, -3, 0}}) {
      fmpz_set_si(x, xx);
      fmpz_set_si(y, yy);
      r_from_x(r, x, y, d);
      x_from_r(back, r, y, d);
      require(fmpz_equal_si(r, rr) && fmpz_equal(back, x),
              "independent negative-D0 floor convention");
    }
    for (bool radius : {false, true}) {
      ByteBuffer b(40);
      set64(b, radius ? 16 : 32, 1);
      set64(b, radius ? 24 : 8, (1ull << 40) + 1);
      BallPart part;
      bool bad = false;
      try {
        parse_ball_part(b.data(), 1, part);
      } catch (const Reject&) {
        bad = true;
      }
      require(bad, "nonzero BALL exponent bound");
    }
  }
}

void field_dimension_refusals(size_t& valid, size_t& rejects) {
  // The field-degree scope cap has two enforcement copies; keep the rest coherent.
  for (unsigned levels : {40u, 41u}) {
    auto m = small(4);
    m.elements.clear();
    vector<string> degrees(levels, "2"), zero(levels, "0"), branches;
    for (unsigned i = 0; i < levels; ++i) {
      element(m, "wsq", i, -1);
      if (i % 2 == 0) {
        Integer exp;
        fmpz_set_si(exp, -Int64(i / 2));
        branches.push_back("[\"0 0 0 0\",\"1 " + hexz(exp) + " 0 0\"]");
      } else {
        Integer n, q, test, exp;
        fmpz_set_ui(n, 2);
        fmpz_mul_2exp(n, n, 2 * (127 - i / 2));
        fmpz_sqrt(q, n);
        fmpz_mul_ui(test, q, 2);
        fmpz_add_ui(test, test, 1);
        fmpz_mul(test, test, test);
        fmpz_mul_2exp(n, n, 2);
        if (fmpz_cmp(n, test) > 0) fmpz_add_ui(q, q, 1);
        auto v = fmpz_val2(q);
        fmpz_tdiv_q_2exp(q, q, v);
        fmpz_set_si(exp, -128 + Int64(v));
        branches.push_back("[\"0 0 0 0\",\"" + hexz(q) + " " + hexz(exp) + " 1 -80\"]");
      }
    }
    element(m, "rep", 0, 1);
    element(m, "rep", 1, 0);
    element(m, "rep", 2, 0);
    m.meta["tower"] = "{\"branch_bits\":\"128\",\"branches\":" + array(branches, false) +
                      ",\"degrees\":" + array(degrees) + "}";
    m.meta["galois"] = galois(m.d, {std::to_string(1ull << levels)});
    m.meta["generator_rescaling"] =
        "{\"rule\":\"sicx-2adic-greedy/1.0\",\"shifts\":" + array(zero) + "}";
    if (levels == 40) {
      require(run(write(m), "degree-scope-boundary") == 0,
              "degree cap boundary admits structurally sparse presentation");
      ++valid;
    } else
      bad(rejects, "degree-scope-exceeded", m);
  }
}

void directory_storage_refusals(size_t& /* valid */, size_t& /* rejects */) {
  // Length-consistent sparse directories exercise refusal before any large read/allocation.
  for (bool logical : {true, false}) {
    auto original = write(small(4));
    Uint64 old_ld = le64(original.file.data() + original.file.size() - 104),
           old_pd = le64(original.file.data() + original.file.size() - 88);
    Uint64 cap = (1ull << 24) + 1, ld_len = logical ? 8 + 64 * cap : old_ld,
           pd_len = logical ? old_pd : 8 + 112 * cap;
    Uint64 pd_off = original.ld + 16 + ld_len, footer_off = pd_off + 16 + pd_len;
    string path = fixtures_dir + (logical ? "/sparse-LDIR.sicx" : "/sparse-PDIR.sicx");
    {
      std::ofstream file(path, std::ios::binary);
      auto put = [&](Uint64 off, const ByteBuffer& b) {
        file.seekp(off);
        file.write((const char*)b.data(), b.size());
      };
      put(0, ByteBuffer(original.file.begin(), original.file.begin() + original.ld));
      ByteBuffer header = bytes("LDIR");
      u32(header, 1);
      u64le(header, ld_len);
      u64le(header, logical ? cap : le64(original.file.data() + original.ld + 16));
      put(original.ld, header);
      put(original.ld + 24, ByteBuffer(original.file.begin() + original.ld + 24,
                                       original.file.begin() + original.pd));
      header = bytes("PDIR");
      u32(header, 1);
      u64le(header, pd_len);
      u64le(header, logical ? le64(original.file.data() + original.pd + 16) : cap);
      put(pd_off, header);
      put(pd_off + 24,
          ByteBuffer(original.file.begin() + original.pd + 24, original.file.end() - 112));
      ByteBuffer footer;
      u64le(footer, original.ld);
      u64le(footer, ld_len);
      u64le(footer, pd_off);
      u64le(footer, pd_len);
      append(footer, original.exact);
      append(footer, original.root);
      u64le(footer, footer_off + 112);
      append(footer, bytes(string("SICXEND\x1a", 8)));
      put(footer_off, footer);
      if (!file) throw std::runtime_error("sparse directory write failed");
    }
    int status = 0;
    Opts opt;
    opt.path = path;
    try {
      Container c;
      open_container(c, opt);
    } catch (const PrecErr&) {
      status = 3;
    } catch (const Reject&) {
      status = 2;
    }
    std::filesystem::remove(path);
    require(status == 3,
            logical ? "LDIR cap refuses before allocation" : "PDIR cap refuses before allocation");
  }
}

void hash_and_text_bounds(size_t& /* valid */, size_t& /* rejects */) {
  {
    Sha256 h;
    h.len = UINT64_MAX / 8 + 1;
    Hash out;
    bool rejected = false;
    try {
      h.final(out.data());
    } catch (const Reject&) {
      rejected = true;
    }
    require(rejected, "SHA-256 bit count overflow");
    h = Sha256();
    h.len = UINT64_MAX;
    rejected = false;
    try {
      h.update("x", 1);
    } catch (const Reject&) {
      rejected = true;
    }
    require(rejected, "SHA-256 byte count overflow");
    std::unique_ptr<FILE, decltype(&fclose)> fp(tmpfile(), &fclose);
    if (!fp) throw std::runtime_error("tmpfile failed");
    fputs("01234567890123456789", fp.get());
    rewind(fp.get());
    string line;
    bool newline = false, refused = false;
    try {
      bounded_text_line(fp.get(), line, newline, 16);
    } catch (const PrecErr&) {
      refused = true;
    }
    require(refused && line.size() == 16, "bounded hostile text line");
  }
}
}  // namespace sic_check::verifier::test
