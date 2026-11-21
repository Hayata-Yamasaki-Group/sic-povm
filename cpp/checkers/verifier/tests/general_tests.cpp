// SPDX-License-Identifier: AGPL-3.0-or-later
#include "general_tests.hpp"

#include "test_context.hpp"
namespace sic_check::verifier::test {
using namespace sic_check::verifier;
using namespace fixture;
void general_tests(size_t& valid, size_t& rejects) {
  using namespace fixture;
  for (auto [d, discriminant] : vector<std::pair<unsigned, int>>{
           {2, -3}, {3, 1}, {4, 5}, {5, 3}, {6, 21}, {7, 2}, {9, 15}, {12, 13}}) {
    auto m = small(d);
    m.elements.clear();
    Algebraic expected, root;
    if (d == 3)
      qqbar_one(expected.v);
    else {
      qqbar_set_si(expected.v, discriminant);
      qqbar_sqrt(expected.v, expected.v);
      qqbar_neg(expected.v, expected.v);
      if ((discriminant % 4 + 4) % 4 == 1) {
        qqbar_add_ui(expected.v, expected.v, 1);
        qqbar_div_ui(expected.v, expected.v, 2);
      }
    }
    qqbar_sqrt(root.v, expected.v);
    auto component = [&](bool imaginary) {
      Algebraic value, grid;
      if (imaginary)
        qqbar_im(value.v, root.v);
      else
        qqbar_re(value.v, root.v);
      ACB enclosure;
      qqbar_get_acb(enclosure.v, value.v, 1024);
      Real rounded, half;
      arb_set_d(half.v, 0.5);
      arb_mul_2exp_si(rounded.v, acb_realref(enclosure.v), 128);
      arb_add(rounded.v, rounded.v, half.v, 1024);
      arb_floor(rounded.v, rounded.v, 1024);
      Integer q, exponent;
      require(arb_get_unique_fmpz(q, rounded.v), "writer branch grid resolved");
      qqbar_set_fmpz(grid.v, q);
      qqbar_mul_2exp_si(grid.v, grid.v, -128);
      bool exact = qqbar_equal(grid.v, value.v);
      fmpz_set_si(exponent, -128);
      if (fmpz_is_zero(q))
        fmpz_zero(exponent);
      else {
        auto power = fmpz_val2(q);
        fmpz_tdiv_q_2exp(q, q, power);
        fmpz_add_ui(exponent, exponent, power);
      }
      return hexz(q) + " " + hexz(exponent) + (exact ? " 0 0" : " 1 -80");
    };
    m.meta["tower"] = "{\"branch_bits\":\"128\",\"branches\":[[" + quote(component(false)) + "," +
                      quote(component(true)) + "]],\"degrees\":[\"2\"]}";
    element(m, "wsq", 0, 1);
    if (d != 3) {
      Int64 r = discriminant == 21 || discriminant == 13 ? -1 : 0;
      ByteBuffer b{0, 0, 1, 3};
      integer(b, r);
      integer(b, 1);
      m.elements[0].leaves[0] = b;
    }
    vector<unsigned> positions{0};
    for (unsigned g = 1; g < d; ++g)
      if (d % g == 0) positions.push_back(g);
    for (auto g : positions) element(m, "rep", g, g == 0 ? 1 : 0, g == 0 ? 2 : 1);
    string label = "general-base-d" + std::to_string(d);
    require(run(write(m), label) == 0, "numeric branch uses derived integral basis");
    ++valid;
    Opts options;
    options.path = fixtures_dir + "/" + label + ".sicx";
    Container c;
    open_container(c, options);
    verify_chunks(c, options);
    require(fmpz_equal_si(c.M.D0, discriminant), "derived base discriminant/sentinel");
    branch_check_exact(c, options);
    require(qqbar_equal(c.base_roots[0].v, root.v), "exact branch uses derived integral basis");
    Decoder decoder(c);
    decoder.set_prec(512);
    RootSet empty;
    ACB actual, reference;
    decoder.eval_elem(0, empty, actual.v);
    qqbar_get_acb(reference.v, expected.v, 512);
    require(acb_overlaps(actual.v, reference.v), "numeric base embedding agrees with exact oracle");
  }
  for (unsigned d :
       {2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 12u, 15u, 16u, 18u, 20u, 24u, 25u, 27u, 45u, 64u, 256u}) {
    vector<Uint64> positions{0}, lifts, orders, locals, moduli;
    for (unsigned g = 1; g < d; ++g)
      if (d % g == 0) positions.push_back(g);
    unsigned rest = d;
    for (unsigned p = 2; p <= rest; ++p) {
      if (rest % p) continue;
      unsigned q = 1;
      while (rest % p == 0) q *= p, rest /= p;
      vector<Uint64> a, o;
      if (p == 2) {
        if (q >= 4) a.push_back(q - 1), o.push_back(2);
        if (q >= 8) a.push_back(5), o.push_back(q / 4);
      } else {
        unsigned phi = 0;
        for (unsigned x = 1; x < q; ++x) phi += std::gcd(x, q) == 1;
        for (unsigned x = 1; x < q; ++x) {
          if (std::gcd(x, q) != 1) continue;
          unsigned v = x, order = 1;
          while (v != 1) v = v * x % q, ++order;
          if (order == phi) {
            a.push_back(x);
            o.push_back(order);
            break;
          }
        }
      }
      for (size_t j = 0; j < a.size(); ++j) {
        for (unsigned u = 1; u < d; ++u)
          if (u % q == a[j] && u % (d / q) == 1 % (d / q)) {
            lifts.push_back(u);
            orders.push_back(o[j]);
            locals.push_back(a[j]);
            moduli.push_back(q);
            break;
          }
      }
    }
    auto m = small(d, true);
    vector<vector<string>> words(lifts.size(), {"1"});
    m.meta["galois"] = galois(d, {"2"}, words);
    auto out = write(m);
    string prefix = "general-d" + std::to_string(d);
    Opts full;
    full.decode = "full";
    require(run(out, prefix, full, true) == 0, "general dimension full decode " + prefix);
    ++valid;
    Container c;
    Opts opt;
    opt.path = fixtures_dir + "/" + prefix + ".sicx";
    open_container(c, opt);
    require(c.M.representatives == positions, "complete divisor positions");
    require(c.M.units.size() == lifts.size(), "derived generator count");
    for (size_t j = 0; j < lifts.size(); ++j) {
      const auto& gen = c.M.units[j];
      require(gen.lift == lifts[j] && gen.order == orders[j] && gen.modulus == moduli[j] &&
                  gen.local == locals[j],
              "smallest primitive root, order and CRT lift");
    }
    std::map<Uint64, vector<Uint64>> tuples;
    std::function<void(size_t, Uint64, vector<Uint64>)> enumerate = [&](size_t j, Uint64 u,
                                                                        vector<Uint64> tuple) {
      if (j == lifts.size()) {
        require(tuples.emplace(u, tuple).second, "unit tuple uniqueness");
        return;
      }
      for (Uint64 a = 0; a < orders[j]; ++a, u = u * lifts[j] % d) {
        auto next = tuple;
        next.push_back(a);
        enumerate(j + 1, u, next);
      }
    };
    enumerate(0, 1, {});
    Decoder decoder(c);
    bool outside = false;
    try {
      Uint64 r;
      vector<Uint64> w;
      decoder.entry_word(d, r, w);
    } catch (const UsageErr&) {
      outside = true;
    }
    require(outside, "coordinate domain checked before entry lookup");
    for (Uint64 coordinate = 0; coordinate < d; ++coordinate) {
      Uint64 g = std::gcd(coordinate, Uint64(d)), n = d / g, lift = 0;
      for (Uint64 u = 1; u < d; ++u)
        if (std::gcd(u, Uint64(d)) == 1 && (n == 1 || u % n == coordinate / g)) {
          lift = u;
          break;
        }
      require(lift && tuples.count(lift), "surjective reduction and smallest lift");
      require(unit_exponents(lift, c.M.units) == tuples.at(lift), "full unit tuple oracle");
      Uint64 position;
      vector<Uint64> word;
      decoder.entry_word(coordinate, position, word);
      Uint64 expected = 0;
      for (Uint64 a : tuples.at(lift)) expected = (expected + a) % 2;
      require(position == (g == d ? 0 : g) && word == vector<Uint64>{expected},
              "entry rule against exhaustive unit/lift oracle");
    }
    auto metadata_bad = [&](const string& label, Model model) {
      auto bytes = write(model);
      bool rejected = false;
      try {
        Meta parsed;
        parse_exmt(parsed, bytes.file.data() + 32, le64(bytes.file.data() + 24));
      } catch (const Reject&) {
        rejected = true;
      }
      require(rejected, "G metadata rejection " + prefix + " " + label);
      require(run(bytes, prefix + "-bad-" + label) == 2, "G full rejection " + label);
      ++rejects;
    };
    auto bad_meta = [&](const string& key, const string& value, const string& label) {
      auto copy = m;
      copy.meta[key] = value;
      metadata_bad(label, copy);
    };
    vector<string> pos;
    for (Uint64 v : positions) pos.push_back(std::to_string(v));
    for (string mode : {"missing", "extra", "reordered", "duplicate", "nondivisor"}) {
      auto list = pos;
      if (mode == "missing") list.pop_back();
      if (mode == "extra") list.push_back(std::to_string(d));
      if (mode == "reordered") std::swap(list[0], list[1]);
      if (mode == "duplicate") list[1] = list[0];
      if (mode == "nondivisor") list[1] = std::to_string(d + 1);
      bad_meta("entry_rule",
               "{\"name\":\"sicx-divisors/1.0\",\"representatives\":" + array(list) + "}", mode);
    }
    bad_meta(
        "entry_rule",
        "{\"name\":\"sicx-2power/1\",\"representatives\":" + array(pos) + "}",  // identity-negative
        "unsupported-rule");
    for (string key : {"word_5", "word_minus_1", "generators", "orders", "primitive_root"}) {
      std::map<string, string> fields{{"smith", "[\"2\"]"},
                                      {"unit_generator_words", array(
                                                                   [&] {
                                                                     vector<string> rows;
                                                                     for (const auto& w : words)
                                                                       rows.push_back(array(w));
                                                                     return rows;
                                                                   }(),
                                                                   false)},
                                      {key, "[]"}};
      bad_meta("galois", object(fields), "forbidden-" + key);
    }
    auto extra = words;
    extra.push_back({"0"});
    bad_meta("galois", galois(d, {"2"}, extra), "extra-word");
    if (!words.empty()) {
      auto missing = words;
      missing.pop_back();
      vector<string> rows;
      for (const auto& w : missing) rows.push_back(array(w));
      bad_meta("galois", "{\"smith\":[\"2\"],\"unit_generator_words\":" + array(rows, false) + "}",
               "missing-word");
      for (auto [label, w] : vector<std::pair<string, vector<string>>>{
               {"rank", {}}, {"rank-extra", {"0", "0"}}, {"residue", {"2"}}}) {
        auto changed = words;
        changed[0] = w;
        bad_meta("galois", galois(d, {"2"}, changed), label);
      }
      // D=4 and W=1 violates the even local order 2 when d=3,4,6,12.
      if (orders[0] % 4 != 0) {
        auto changed = small(d);
        changed.meta["tower"] =
            "{\"branch_bits\":\"128\",\"branches\":[[\"0 0 0 0\",\"1 0 0 0\"],[\"0 0 0 0\",\"1 0 0 "
            "0\"]],\"degrees\":[\"2\",\"2\"]}";
        changed.meta["generator_rescaling"] =
            "{\"rule\":\"sicx-2adic-greedy/1.0\",\"shifts\":[\"0\",\"0\"]}";
        changed.elements.clear();
        element(changed, "wsq", 0, -1);
        element(changed, "wsq", 1, -1, 2);
        for (Uint64 r : positions) element(changed, "rep", r, 0);
        changed.meta["galois"] = galois(d, {"4"}, words);
        metadata_bad("order-relation", changed);
      }
    }
    for (string key : {"base_field", "D0", "embedding"}) bad_meta(key, "\"1\"", "override-" + key);
    auto missing = m;
    missing.elements.pop_back();
    metadata_bad("missing-representative", missing);
    auto duplicate = m;
    duplicate.elements.back().fields["position"] = "\"0\"";
    metadata_bad("duplicate-representative", duplicate);
    if (d == 3) {
      auto rational = m;
      // Q pairs remain two integers on disk but nonzero y is forbidden.
      rational.elements[0].leaves[0] = {0, 0, 1, 3, 3, 1, 2, 1};
      require(run(write(rational), "general-d3-nonzero-y") == 2, "rational base rejects y != 0");
      ++rejects;
    }
  }
  // Odd composite ratio path: coordinate3 is a divisor, while floor(d/2)=4 is not.
  {
    auto m = small(9, true);
    auto& e = m.elements.back();
    e.fields["scale"] = "\"2\"";
    e.fields["max_bits"] = "\"2\"";
    e.leaves[0] = leaf(m.elements.size() - 1, 0, 1, 1);
    string rays(m.extras[0].payload.begin(), m.extras[0].payload.end());
    rays.replace(rays.find("\"limbs\":\"1\""), 11, "\"limbs\":\"4\"");
    rays.replace(rays.find("199"), 3, "249");
    m.extras[0].payload = bytes(rays);
    Real value, rounded, half;
    Integer q;
    arb_set_ui(value.v, 3);
    arb_sqrt(value.v, value.v, 1024);
    arb_inv(value.v, value.v, 1024);
    arb_mul_2exp_si(rounded.v, value.v, 256);
    arb_set_d(half.v, 0.5);
    arb_add(rounded.v, rounded.v, half.v, 1024);
    arb_floor(rounded.v, rounded.v, 1024);
    require(arb_get_unique_fmpz(q, rounded.v), "writer certified normalized midpoint");
    slong power = fmpz_val2(q);
    fmpz_tdiv_q_2exp(q, q, power);
    ByteBuffer ball;
    for (unsigned j = 0; j < 18; ++j) {
      bool nonzero = j == 0 || j == 6 || j == 12;
      u64le(ball, 0);
      u64le(ball, nonzero ? Uint64(-256 + power) : 0);
      u64le(ball, nonzero ? 1 : 0);
      u64le(ball, nonzero ? Uint64(-250) : 0);
      Integer part;
      for (unsigned limb = 0; limb < 4; ++limb) {
        if (nonzero) fmpz_fdiv_q_2exp(part, q, 64 * limb);
        u64le(ball, fmpz_get_ui(part));
      }
    }
    m.extras[1].payload = ball;
    Opts mode;
    mode.decode = "ratio";
    require(run(write(m), "general-d9-divisor-ratio", mode, true) == 0,
            "odd composite divisor ratio");
    ++valid;
    mode.decode = "full";
    require(run(write(m), "general-d9-divisor-full", mode, true) == 0,
            "odd composite normalized decode");
    ++valid;
  }
}
}  // namespace sic_check::verifier::test
