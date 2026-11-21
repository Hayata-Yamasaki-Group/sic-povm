// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
namespace sic_check::verifier::test {
using namespace sic_check::verifier;
using namespace fixture;
void rescaling_and_entry_contracts(unsigned& d, size_t& valid, size_t& rejects,
                                   DimensionContext& dimension, string& pre, Output& /* a */,
                                   Output& /* b */, string& /* json */, string& /* nested */) {
  dimension.meta("entry-rule-name", "entry_rule",
                 "{\"name\":\"other/1\",\"representatives\":[\"0\"]}");  // identity-negative
  for (unsigned levels : {0u, 41u, 63u}) {
    vector<string> degrees(levels, "2");
    dimension.meta("tower-bound-" + std::to_string(levels), "tower",
                   "{\"branch_bits\":\"128\",\"branches\":[],\"degrees\":" + array(degrees) + "}");
  }
  auto inverse = small(d);
  setup_inverse(inverse);
  bad(rejects, pre + "inverse-rescaling-record", inverse);
  inverse.meta["generator_rescaling"] =
      "{\"rule\":\"sicx-2adic-greedy/1.0\",\"shifts\":[\"1\",\"0\"]}";
  require(run(write(inverse), pre + "inverse-rescaling-positive") == 0,
          "inverse rescaling fixture branch sanity");
  ++valid;
  {
    auto ordered = inverse;
    ordered.extras = small(d, true).extras;
    string prefix =
        "{\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c0\",\"word\":[\"0\"]}";
    string a =
        "{\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c0\",\"word\":[\"0\",\"1\"]}";
    string b =
        "{\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c1\",\"word\":[\"1\",\"0\"]}";
    string rays(ordered.extras[0].payload.begin(), ordered.extras[0].payload.end());
    rays.replace(rays.find(prefix), prefix.size(), a + "," + b);
    ordered.extras[0].payload = bytes(rays);
    auto second = ordered.extras[1];
    second.obj = 1;
    ordered.extras.push_back(second);
    require(run(write(ordered), pre + "first-coordinate-first-Smith-word-order", {}, true) == 0,
            "word order uses first-coordinate-first Smith tuples");
    ++valid;
    a.replace(a.find("[\"0\",\"1\"]"), 9, "[\"1\",\"0\"]");
    b.replace(b.find("[\"1\",\"0\"]"), 9, "[\"0\",\"1\"]");
    // Reconstruct from the same RAYS prefix; labels stay numeric while words reverse.
    rays = string(ordered.extras[0].payload.begin(), ordered.extras[0].payload.end());
    auto begin = rays.find("[{\"accuracy_bits\"");
    rays.replace(begin, rays.size() - begin - 1, "[" + a + "," + b + "]");
    ordered.extras[0].payload = bytes(rays);
    bad(rejects, pre + "reversed-Smith-word-order", ordered);
  }
  auto smaller = inverse;
  smaller.meta["galois"] = galois(d, {"2"});
  bad(rejects, pre + "smith-product-smaller", smaller);
  {
    auto sparse = small(d, true);
    string ray = "{\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c2\",\"word\":[\"0\"]}";
    string ray10 = ray;
    ray10.replace(ray10.find("c2"), 2, "c10");
    ray10.replace(ray10.find("[\"0\"]"), 5, "[\"1\"]");
    string r(sparse.extras[0].payload.begin(), sparse.extras[0].payload.end());
    auto at = r.find("[{\"accuracy_bits\"");
    r.replace(at, r.size() - at - 1, "[" + ray + "," + ray10 + "]");
    sparse.extras[0].payload = bytes(r);
    auto second = sparse.extras[1];
    second.obj = 1;
    sparse.extras.push_back(second);
    require(run(write(sparse), pre + "sparse-rays", {}, true) == 0,
            "sparse numeric labels without c0");
    ++valid;
    auto reversed = sparse;
    string badword = ray, badword10 = ray10;
    badword.replace(badword.find("[\"0\"]"), 5, "[\"1\"]");
    badword10.replace(badword10.find("[\"1\"]"), 5, "[\"0\"]");
    string reversejson(reversed.extras[0].payload.begin(), reversed.extras[0].payload.end());
    auto begin = reversejson.find("[{\"accuracy_bits\"");
    reversejson.replace(begin, reversejson.size() - begin - 1,
                        "[" + badword + "," + badword10 + "]");
    reversed.extras[0].payload = bytes(reversejson);
    bad(rejects, pre + "ray-word-order", reversed);
    auto wide = small(d, true);
    r = string(wide.extras[0].payload.begin(), wide.extras[0].payload.end());
    at = r.find("\"limbs\":\"1\"");
    r.replace(at, 11, "\"limbs\":\"2\"");
    wide.extras[0].payload = bytes(r);
    ByteBuffer payload;
    for (unsigned part = 0; part < 2 * d; ++part) {
      ByteBuffer record(wide.extras[1].payload.begin() + part * 40,
                        wide.extras[1].payload.begin() + (part + 1) * 40);
      u64le(record, 0);
      append(payload, record);
    }
    wide.extras[1].payload = payload;
    require(run(write(wide), pre + "nonminimal-ball-width", {}, true) == 0,
            "nonminimal ball width permitted");
    ++valid;
  }

  {
    auto translated = small(d, true);
    string json(translated.extras[0].payload.begin(), translated.extras[0].payload.end());
    json.replace(json.find("\"word\":[\"0\"]"), 12, "\"word\":[\"1\"]");
    translated.extras[0].payload = bytes(json);
    require(run(write(translated), pre + "c0-nonzero-word", {}, true) == 0, "amended c0 word");
    ++valid;
    auto nested = small(d);
    nested.extras.push_back({"PROV", 0, bytes("{\"nested\":{\"generator_rescaling\":{}}}")});
    require(run(write(nested), pre + "PROV-nested-rescaling") == 0,
            "nested PROV spelling permitted");
    ++valid;
    for (string value : {"\"free text\"", "[]", "{\"rule\":\"other\"}"}) {
      auto top = small(d);
      top.extras.push_back({"PROV", 0, bytes("{\"generator_rescaling\":" + value + "}")});
      bad(rejects, pre + "PROV-top-ban-" + std::to_string(rejects), top);
    }
    auto deferred = small(d, true);
    deferred.change_file = [](ByteBuffer& f) {
      size_t off = 16 + 16 + le64(f.data() + 24);
      f[off] = 'X';
    };
    require(run(write(deferred), pre + "partial-unread-header", Opts{.partial_elements = {2}}) == 0,
            "ordinary unread header deferred");
    ++valid;
    require(
        run(write(deferred), pre + "partial-selected-header", Opts{.partial_elements = {0}}) == 2,
        "selected header checked");
    ++rejects;
    auto all = small(d);
    all.elements[0].fields["scale"] = "\"4\"";
    all.elements[0].fields["max_bits"] = "\"3\"";
    all.elements[1].fields["scale"] = "\"4\"";
    all.elements[1].fields["max_bits"] = "\"3\"";
    Opts partall;
    for (size_t i = 0; i < all.elements.size(); ++i) partall.partial_elements.push_back(i);
    require(run(write(all), pre + "partial-all-rescaling", partall) == 2,
            "all exact partial rescaling mandatory");
    ++rejects;
  }

  {
    auto invalid = small(d, true);
    string rays(invalid.extras[0].payload.begin(), invalid.extras[0].payload.end());
    rays.replace(rays.find("\"limbs\":\"1\""), 11, "\"limbs\":\"3\"");
    invalid.extras[0].payload = bytes(rays);
    ByteBuffer payload;
    for (unsigned j = 0; j < 2 * d; ++j) {
      payload.insert(payload.end(), invalid.extras[1].payload.begin() + 40 * j,
                     invalid.extras[1].payload.begin() + 40 * (j + 1));
      u64le(payload, 0);
      u64le(payload, 0);
    }
    set64(payload, 48, 1);
    invalid.extras[1].payload = payload;
    bad(rejects, pre + "wide-BALL-invalid-norm", invalid);
  }
}

void wide_rescaling_weights(size_t& valid, size_t& /* rejects */) {
  // Sparse genuine multiquadratic tower: legal recorded shifts reconstruct valuations
  // larger than uint64_t. Unit-support constraints force every recorded shift exactly.
  {
    Model m = small(4);
    m.elements.clear();
    vector<string> deg(20, "2"), zero(20, "0"), shifts(20, "999999999999999999"), branches, levels;
    const int primes[] = {3,  7,  11, 13, 17, 19, 23, 29, 31, 37,
                          41, 43, 47, 53, 59, 61, 67, 71, 73, 79};
    for (int i = 0; i < 20; ++i) {
      Integer a, q, tmp;
      fmpz_set_ui(a, primes[i]);
      fmpz_mul_2exp(a, a, 256);
      fmpz_sqrt(q, a);
      fmpz_mul_ui(tmp, q, 2);
      fmpz_add_ui(tmp, tmp, 1);
      fmpz_mul(tmp, tmp, tmp);
      fmpz_mul_2exp(a, a, 2);
      if (fmpz_cmp(a, tmp) > 0) fmpz_add_ui(q, q, 1);
      Int64 E = -128;
      while (!fmpz_is_odd(q)) {
        fmpz_tdiv_q_2exp(q, q, 1);
        ++E;
      }
      fmpz_set_si(tmp, E);
      branches.push_back("[" + quote(hexz(q) + " " + hexz(tmp) + " 1 -80") + ",\"0 0 0 0\"]");
      element(m, "wsq", i, 1, Uint64(primes[i]) * (1ull << i));
      element(m, "act", i, -1, Uint64(primes[i]) * (1ull << i));
      m.elements.back().fields["axis"] = quote(std::to_string(i));
      levels.push_back(std::to_string(i));
    }
    m.meta["tower"] = "{\"branch_bits\":\"128\",\"branches\":" + array(branches, false) +
                      ",\"degrees\":" + array(deg) + "}";
    m.meta["galois"] = galois(m.d, deg);
    m.meta["generator_rescaling"] =
        "{\"rule\":\"sicx-2adic-greedy/1.0\",\"shifts\":" + array(shifts) + "}";
    const Uint64 n = 1ull << 20, id = m.elements.size();
    element(m, "rep", 0, 0, 1, levels, n);
    auto& rep = m.elements.back();
    rep.fields["max_bits"] = "\"1\"";
    for (Uint64 first = 0; first < n; first += 16384) {
      ByteBuffer b;
      var(b, id);
      var(b, first);
      var(b, 16384);
      auto nonzero = [&](Uint64 j) { return (j && !(j & (j - 1))) || j == n - 1; };
      for (Uint64 j = first; j < first + 16384;) {
        bool nz = nonzero(j);
        Uint64 end = j + 1;
        while (end < first + 16384 && nonzero(end) == nz) ++end;
        var(b, 2 * (end - j) + nz);
        if (nz)
          for (Uint64 k = j; k < end; ++k) {
            integer(b, 1);
            integer(b, 0);
          }
        j = end;
      }
      rep.leaves[first / 16384] = std::move(b);
    }
    for (Uint64 r : {1u, 2u}) element(m, "rep", r, 0);
    m.grouped = true;
    Opts opt;
    opt.branch_degree_limit = n;
    require(run(write(m), "inverse-rescaling-wide-valuations", opt) == 0,
            "valid source valuations exceed uint64 without materialization");
    ++valid;
  }
}

void quadratic_square_branches(size_t& /* valid */, size_t& /* rejects */) {
  // Exact-only height admission: interval arithmetic cannot decide the correlated singleton.
  // g0=sqrt5, and((B+2)+(-B+2B*omega)/g0)/2=1 for B=2^16500.
  {
    auto m = small(4);
    m.elements.clear();
    element(m, "wsq", 0, 1, 5);
    element(m, "wsq", 1, 1, 2, {"0"}, 2);
    Integer B, r0, r1, tmp, q, exp;
    fmpz_one(B);
    fmpz_mul_2exp(B, B, 16500);
    fmpz_fdiv_q_2exp(r0, B, 1);
    fmpz_add_ui(r0, r0, 1);
    fmpz_mul(tmp, B, B);
    fmpz_mul_ui(tmp, tmp, 5);
    fmpz_sqrt(r1, tmp);
    fmpz_fdiv_q_2exp(r1, r1, 1);
    fmpz_neg(r1, r1);
    auto big = [&](ByteBuffer& bytes, const fmpz_t value) {
      Integer n;
      fmpz_abs(n, value);
      Uint64 count = (fmpz_bits(n) + 7) / 8;
      var(bytes, 2 * count + (fmpz_sgn(value) < 0));
      for (Uint64 j = 0; j < count; ++j) {
        bytes.push_back(fmpz_fdiv_ui(n, 256));
        fmpz_fdiv_q_2exp(n, n, 8);
      }
    };
    ByteBuffer data;
    var(data, 1);
    var(data, 0);
    var(data, 2);
    var(data, 5);
    big(data, r0);
    integer(data, 0);
    big(data, r1);
    big(data, B);
    m.elements[1].leaves[0] = data;
    m.elements[1].fields["max_bits"] = "\"16502\"";
    element(m, "rep", 0, 1);
    element(m, "rep", 1, 0);
    element(m, "rep", 2, 0);
    fmpz_set_ui(tmp, 5);
    fmpz_mul_2exp(tmp, tmp, 256);
    fmpz_sqrt(q, tmp);
    fmpz_mul_ui(r0, q, 2);
    fmpz_add_ui(r0, r0, 1);
    fmpz_mul(r0, r0, r0);
    fmpz_mul_2exp(tmp, tmp, 2);
    if (fmpz_cmp(tmp, r0) > 0) fmpz_add_ui(q, q, 1);
    auto valuation = fmpz_val2(q);
    fmpz_tdiv_q_2exp(q, q, valuation);
    fmpz_set_si(exp, -128 + Int64(valuation));
    m.meta["tower"] = "{\"branch_bits\":\"128\",\"branches\":[[\"" + hexz(q) + " " + hexz(exp) +
                      " 1 -80\",\"0 0 0 0\"],[\"1 0 0 0\",\"0 0 0 0\"]],\"degrees\":[\"2\",\"2\"]}";
    m.meta["galois"] = galois(m.d, {"2", "2"});
    m.meta["generator_rescaling"] = "{\"rule\":\"sicx-2adic-greedy/1.0\",\"shifts\":[\"0\",\"0\"]}";
    require(run(write(m), "exact-coefficient-height-admission") == 3,
            "exact coefficient height admission before algebraic expansion");
  }
}

void cubic_zero_branches(size_t& valid, size_t& /* rejects */) {
  // Two close, nonzero conjugates require the all-root separation check at p=128.
  {
    auto m = small(4);
    m.elements.clear();
    Integer a, coefficient;
    fmpz_one(a);
    fmpz_mul_2exp(a, a, 400);
    for (int k = 0; k < 3; ++k) {
      if (k == 0) {
        fmpz_add_ui(coefficient, a, 1);
        fmpz_neg(coefficient, coefficient);
      }
      if (k == 1) {
        fmpz_mul_ui(coefficient, a, 2);
        fmpz_add_ui(coefficient, coefficient, 1);
      }
      if (k == 2) {
        fmpz_add_ui(coefficient, a, 2);
        fmpz_neg(coefficient, coefficient);
      }
      element(m, "fcoef", 0, fmpz_sgn(coefficient));
      auto& e = m.elements.back();
      e.fields["k"] = quote(std::to_string(k));
      e.fields["max_bits"] = quote(std::to_string(fmpz_bits(coefficient)));
      fmpz_abs(coefficient, coefficient);
      e.fields["scale"] = quote(dec(coefficient));
    }
    element(m, "rep", 0, 1, 3);
    element(m, "rep", 1, 0);
    element(m, "rep", 2, 0);
    m.meta["tower"] =
        "{\"branch_bits\":\"256\",\"branches\":[[\"1 0 1 -100\",\"1 -c8 1 "
        "-100\"]],\"degrees\":[\"3\"]}";
    m.meta["galois"] = galois(m.d, {"3"});
    require(run(write(m), "d4-close-nonzero-roots") == 0, "all-root separation minimum at256");
    ++valid;
  }
}

void signed_base_coordinates(size_t& valid, size_t& /* rejects */) {
  // Negative-D0 pair transform, independent d=2 exact input (the spec's z leaves).
  {
    auto m = small(2);
    auto& rep = m.elements.back();
    rep.fields["levels"] = "[\"0\"]";
    rep.fields["scale"] = "\"2\"";
    rep.fields["max_bits"] = "\"2\"";
    rep.coords = 2;
    ByteBuffer b;
    var(b, 3);
    var(b, 0);
    var(b, 2);
    var(b, 5);
    integer(b, -1);
    integer(b, -1);
    integer(b, 0);
    integer(b, -1);
    rep.leaves[0] = b;
    require(run(write(m), "d2-balanced-pairs") == 0, "negative D0 balanced pairs");
    ++valid;
  }
}
}  // namespace sic_check::verifier::test
