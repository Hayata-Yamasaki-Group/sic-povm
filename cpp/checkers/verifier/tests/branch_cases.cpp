// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
namespace sic_check::verifier::test {
using namespace sic_check::verifier;
using namespace fixture;
void odd_branch_and_action_contracts(size_t& valid, size_t& rejects) {
  // Odd-degree cyclic towers: Shanks X^3-aX^2-(a+3)X-1 and its exact order-3 action.
  for (unsigned d : {4u, 8u})
    for (unsigned branchBits : {128u, 256u}) {
      Model m = small(d);
      m.elements.clear();
      fmpz_t param, disc, value;
      fmpz_init(param);
      fmpz_init(disc);
      fmpz_init(value);
      if (branchBits == 256) fmpz_one(param), fmpz_mul_2exp(param, param, 200);
      fmpz_mul(disc, param, param);
      fmpz_mul_ui(value, param, 3);
      fmpz_add(disc, disc, value);
      fmpz_add_ui(disc, disc, 9);
      fmpz_poly_t poly;
      fmpz_poly_init(poly);
      fmpz_poly_set_coeff_si(poly, 0, -1);
      fmpz_add_ui(value, param, 3);
      fmpz_neg(value, value);
      fmpz_poly_set_coeff_fmpz(poly, 1, value);
      fmpz_neg(value, param);
      fmpz_poly_set_coeff_fmpz(poly, 2, value);
      fmpz_poly_set_coeff_si(poly, 3, 1);
      AlgebraicArray rr(3);
      qqbar_roots_fmpz_poly(rr.v, poly, 0);
      size_t selected = 0;
      for (size_t i = 1; i < 3; ++i)
        if (qqbar_cmpabs(rr.v + i, rr.v + selected) < 0) selected = i;
      // Independently round a very tight enclosure; these irrational test roots cannot be ties.
      ACB enclosure;
      qqbar_get_acb(enclosure.v, rr.v + selected, 1024);
      arb_t rounded, half;
      arb_init(rounded);
      arb_init(half);
      arb_set_d(half, 0.5);
      arb_mul_2exp_si(rounded, acb_realref(enclosure.v), branchBits);
      arb_add(rounded, rounded, half, 1024);
      arb_floor(rounded, rounded, 1024);
      fmpz_t q, exp;
      fmpz_init(q);
      fmpz_init_set_si(exp, -Int64(branchBits));
      if (!arb_get_unique_fmpz(q, rounded))
        throw std::runtime_error("fixture branch rounding unresolved");
      while (!fmpz_is_odd(q)) {
        fmpz_tdiv_q_2exp(q, q, 1);
        fmpz_add_ui(exp, exp, 1);
      }
      string branch = hexz(q) + " " + hexz(exp) + " 1 " + (branchBits == 128 ? "-80" : "-100");
      m.meta["tower"] = "{\"branch_bits\":\"" + std::to_string(branchBits) + "\",\"branches\":[[" +
                        quote(branch) + ",\"0 0 0 0\"]],\"degrees\":[\"3\"]}";
      m.meta["galois"] = galois(m.d, {"3"});
      auto constant = [&](string kind, int k, const fmpz_t val) {
        element(m, kind, 0, 1);
        auto& e = m.elements.back();
        if (k >= 0) e.fields["k"] = quote(std::to_string(k));
        if (kind == "actodd") e.fields["axis"] = "\"0\"";
        if (fmpz_is_zero(val)) {
          e.value = 0;
          e.fields["scale"] = "\"1\"";
          e.fields["max_bits"] = "\"0\"";
          e.leaves[0] = leaf(m.elements.size() - 1, 0, 1, 0);
        } else {
          e.value = fmpz_sgn(val);
          fmpz_abs(value, val);
          e.fields["scale"] = quote(dec(value));
          e.fields["max_bits"] = quote(std::to_string(fmpz_bits(val)));
          e.leaves[0] = leaf(m.elements.size() - 1, 0, 1, e.value);
        }
      };
      for (int k = 0; k < 3; ++k) {
        Integer coeff;
        fmpz_poly_get_coeff_fmpz(coeff, poly, k);
        constant("fcoef", k, coeff);
      }
      constant("actdelta", -1, disc);
      Integer v;
      fmpz_mul_si(v, disc, -2);
      constant("actodd", 0, v);
      fmpz_add_ui(v, param, 1);
      fmpz_mul(v, v, disc);
      fmpz_neg(v, v);
      constant("actodd", 1, v);
      constant("actodd", 2, disc);
      element(m, "rep", 0, 1, 3);
      for (Uint64 r = 1; r <= d / 2; r *= 2) element(m, "rep", r, 0);
      string tag = "d" + std::to_string(d) + "-odd-" + std::to_string(branchBits);
      require(run(write(m), tag) == 0, "valid cyclic odd tower and canonical branch minimum");
      ++valid;
      if (branchBits == 128) {
        Opts limit;
        limit.branch_degree_limit = 2;
        limit.path = fixtures_dir + "/" + tag + ".sicx";
        Container admitted;
        open_container(admitted, limit);
        verify_chunks(admitted, limit);
        bool refused = false;
        try {
          branch_check_exact(admitted, limit);
        } catch (const PrecErr&) {
          refused = true;
        }
        require(refused, "exact fallback degree admission");
        // Force genuinely nonsingleton imaginary enclosures on this small
        // all-real cubic. A cap below its degree rules out exact fallback.
        limit.branch_degree_limit = 1;
        pad_real_roots = true;
        padded_roots = 0;
        require(numeric_branch_check(admitted), "real-root equality certified with ball padding");
        branch_check(admitted, limit);
        auto base = numeric_base(admitted, 1024, nullptr);
        pad_real_roots = false;
        require(padded_roots >= 9 && admitted.base_roots.empty(),
                "forced interval path and decoder reuse without algebraic roots");
        require(arb_is_zero(acb_imagref(base.r[0].v)), "proved real base component is exact zero");
      }
      if (branchBits == 128) {
        auto wrong = m;
        wrong.elements[1].fields["k"] = "\"2\"";
        auto data = write(wrong);
        bool rejected = false;
        try {
          Meta parsed;
          parse_exmt(parsed, data.file.data() + 32, le64(data.file.data() + 24));
        } catch (const Reject&) {
          rejected = true;
        }
        require(rejected, "fcoef coefficient order contract");
        auto zero = m;
        zero.elements[3].value = 0;
        zero.elements[3].fields["scale"] = "\"1\"";
        zero.elements[3].fields["max_bits"] = "\"0\"";
        zero.elements[3].leaves[0] = leaf(3, 0, 1, 0);
        require(run(write(zero), tag + "-zero-delta-container") == 0,
                "zero delta is outside mandatory container algebraic premises");
        ++valid;
        Opts options;
        options.path = fixtures_dir + "/" + tag + "-zero-delta-container.sicx";
        Container c;
        open_container(c, options);
        verify_chunks(c, options);
        rescaling_check(c, options);
        branch_check(c, options);
        Decoder dec(c);
        dec.set_prec(1024);
        auto base = dec.base();
        string reason;
        try {
          dec.apply_generator(base, 0);
        } catch (const PrecErr& e) {
          reason = e.what();
        }
        require(reason.find("odd action delta contains zero") != string::npos,
                "odd delta certified nonzero before division");
        auto absent = m;
        absent.elements.erase(absent.elements.begin() + 3);
        data = write(absent);
        rejected = false;
        try {
          Meta parsed;
          parse_exmt(parsed, data.file.data() + 32, le64(data.file.data() + 24));
        } catch (const Reject&) {
          rejected = true;
        }
        require(rejected, "actdelta present iff odd action contract");
      }
      auto missing = m;
      missing.elements[6].fields["k"] = "\"3\"";
      bad(rejects, tag + "-incomplete-action", missing);
      auto delta = m;
      delta.elements[3].fields["kind"] = "\"act\"";
      bad(rejects, tag + "-delta-rule", delta);
      auto repeated = m;
      // (X-omega)^3 over Q(sqrt(5)): a nonrational repeated-root regression for FLINT's
      // precondition.
      repeated.elements.clear();
      for (int k = 0; k < 3; ++k) {
        element(repeated, "fcoef", 0, 1, k ? 3 : 1);
        auto& e = repeated.elements.back();
        e.fields["k"] = quote(std::to_string(k));
        e.fields["max_bits"] = "\"2\"";
        ByteBuffer b;
        var(b, k);
        var(b, 0);
        var(b, 1);
        var(b, 3);
        integer(b, 1);
        integer(b, k == 0 ? -2 : k == 1 ? 1 : -1);
        e.leaves[0] = b;
      }
      element(repeated, "rep", 0, 1, 3);
      for (Uint64 r = 1; r <= d / 2; r *= 2) element(repeated, "rep", r, 0);
      bad(rejects, tag + "-nonrational-repeated-root", repeated);
      if (branchBits == 128) {
        auto negated = m;
        for (size_t j = 3; j < 7; ++j)
          if (negated.elements[j].value)
            negated.elements[j].leaves[0] = leaf(j, 0, 1, -negated.elements[j].value);
        auto a = write(m), b = write(negated);
        require(a.exact != b.exact, "signed action data bind exact root");
        require(run(b, tag + "-simultaneous-sign-negation") == 0,
                "both Vandermonde signs permitted");
        ++valid;
      }
      arb_clear(rounded);
      arb_clear(half);
      fmpz_clear(q);
      fmpz_clear(exp);
      fmpz_poly_clear(poly);
      fmpz_clear(param);
      fmpz_clear(disc);
      fmpz_clear(value);
    }
}

void exact_nearest_even_ties(size_t& /* valid */, size_t& /* rejects */) {
  for (long numerator : {5l, 7l, -5l, -7l}) {
    Algebraic x;
    qqbar_set_si(x.v, numerator);
    qqbar_mul_2exp_si(x.v, x.v, -129);
    arb_t b;
    arb_init(b);
    string expected = numerator == 5    ? "1 -7f 1 -80"
                      : numerator == 7  ? "1 -7e 1 -80"
                      : numerator == -5 ? "-1 -7f 1 -80"
                                        : "-1 -7e 1 -80";
    require(exact_component(x.v, 128, b) == expected, "exact positive/negative nearest-even tie");
    arb_clear(b);
  }
}

void interval_ties_and_source_snapshot(size_t& /* valid */, size_t& /* rejects */) {
  {
    for (long n : {5l, 7l, -5l, -7l}) {
      Real x, box;
      arb_set_si(x.v, n);
      arb_mul_2exp_si(x.v, x.v, -129);
      Algebraic z;
      qqbar_set_si(z.v, n);
      qqbar_mul_2exp_si(z.v, z.v, -129);
      Real expected;
      require(interval_component(x.v, 128, box.v, 512) == exact_component(z.v, 128, expected.v),
              "interval exact tie equals algebraic rule");
    }
    Real x, box;
    arb_set_si(x.v, 1025);
    arb_mul_2exp_si(x.v, x.v, -11);
    mag_set_ui_2exp_si(arb_radref(x.v), 1, -11);
    bool refused = false;
    try {
      interval_component(x.v, 0, box.v, 512);
    } catch (const PrecErr&) {
      refused = true;
    }
    require(refused, "interval touching nearest-even boundary refuses");
    string path = fixtures_dir + "/snapshot-change.sicx";
    std::filesystem::copy_file(fixtures_dir + "/d4-valid-exact.sicx", path,
                               std::filesystem::copy_options::overwrite_existing);
    File source;
    source.open(path);
    {
      std::ofstream f(path, std::ios::binary | std::ios::app);
      f.put(0);
    }
    bool rejected = false;
    try {
      source.stable();
    } catch (const Reject&) {
      rejected = true;
    }
    require(rejected, "changed source snapshot rejected");
  }
}

void uncertain_rounding_and_short_reads(size_t& /* valid */, size_t& /* rejects */) {
  {
    Real x, box;
    arb_one(x.v);
    arb_add_error_2exp_si(x.v, -500);
    bool refused = false;
    try {
      interval_component(x.v, 128, box.v, 1024);
    } catch (const PrecErr&) {
      refused = true;
    }
    require(refused, "singleton equality cannot be guessed from a narrow interval");
    arb_set_d(x.v, 0.5);
    arb_add_error_2exp_si(x.v, -500);
    refused = false;
    try {
      interval_component(x.v, 0, box.v, 1024);
    } catch (const PrecErr&) {
      refused = true;
    }
    require(refused, "nearest grid integer unresolved across half tie");
    // Inject the state after a source shrinks: declared bytes, empty underlying stream.
    File file;
    file.f = tmpfile();
    if (!file.f) throw std::runtime_error("tmpfile failed");
    file.size = 16;
    Uint8 bytes[16]{};
    bool rejected = false;
    try {
      file.read(0, bytes, sizeof(bytes));
    } catch (const Reject&) {
      rejected = true;
    }
    require(rejected, "short read rejects before using incomplete bytes");
  }
}

void binary64_and_ball_storage_bounds(size_t& /* valid */, size_t& /* rejects */) {
  {
    Real x;
    arb_zero(x.v);
    arb_add_error_2exp_si(x.v, -2000);
    bool refused = false;
    try {
      rounded_binary64(x.v, 4096);
    } catch (const PrecErr&) {
      refused = true;
    }
    require(refused, "binary64 signed-zero encodings must be unique");
    arb_zero(x.v);
    require(std::bit_cast<Uint64>(rounded_binary64(x.v, 4096)) == 0,
            "exact zero rounds to positive zero");
    for (int sign : {-1, 1}) {
      arb_set_si(x.v, sign);
      arb_mul_2exp_si(x.v, x.v, -2000);
      require(std::bit_cast<Uint64>(rounded_binary64(x.v, 4096)) == (sign < 0 ? 1ull << 63 : 0),
              "underflow preserves a certified sign");
    }
    admit_ball_memory(1, 64, 1);
    refused = false;
    try {
      admit_ball_memory(8, 8192, 1024);
    } catch (const PrecErr&) {
      refused = true;
    }
    require(refused, "BALL admission includes full-width heap midpoint storage");
  }
}
}  // namespace sic_check::verifier::test
