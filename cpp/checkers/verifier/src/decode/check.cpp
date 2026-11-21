// SPDX-License-Identifier: AGPL-3.0-or-later
#include "check.hpp"

#include "../format/ball_codec.hpp"
#include "../format/canonical_json.hpp"
#include "../format/container.hpp"
#include "../format/integer_codec.hpp"
#include "../format/metadata.hpp"
#include "../format/sha256.hpp"
#include "../runtime/diagnostics.hpp"
#include "decoder.hpp"
#ifdef SIC_CHECKER_FAULT_SEAMS
#include "../runtime/fault_seams.hpp"
#define acb_is_finite sic_check::verifier::test::test_finite
#endif
namespace sic_check::verifier {
bool same_binary64(double a, double b) {
  return std::bit_cast<Uint64>(a) == std::bit_cast<Uint64>(b);
}

double rounded_binary64(const arb_t x, slong prec) {
  if (!arb_is_finite(x)) throw PrecErr("binary64 rounding of nonfinite enclosure");
  arf_t lo, hi;
  arf_init(lo);
  arf_init(hi);
  arb_get_interval_arf(lo, hi, x, prec);
  double a = arf_get_d(lo, ARF_RND_NEAR), b = arf_get_d(hi, ARF_RND_NEAR);
  arf_clear(lo);
  arf_clear(hi);
  if (!same_binary64(a, b)) throw PrecErr("binary64 rounding not uniquely certified");
  return a;
}

string acbstr(const acb_t x) {
  std::unique_ptr<char, decltype(&flint_free)> a(arb_get_str(acb_realref(x), 25, 0), flint_free);
  std::unique_ptr<char, decltype(&flint_free)> b(arb_get_str(acb_imagref(x), 25, 0), flint_free);
  string r = string(a.get()) + " + i*" + b.get();
  return r;
}

bool rad_small(const acb_t x, slong bits) {
  mag_t r;
  mag_init(r);
  acb_get_mag(r, x);
  arb_t re;
  mag_t a, b;
  mag_init(a);
  mag_init(b);
  mag_set(a, arb_radref(acb_realref(x)));
  mag_set(b, arb_radref(acb_imagref(x)));
  mag_max(a, a, b);
  bool ok = mag_cmp_2exp_si(a, -bits) <= 0;
  (void)re;
  mag_clear(r);
  mag_clear(a);
  mag_clear(b);
  return ok;
}

void decode_check(Container& C, const Opts& O) {
  Meta& M = C.M;
  if (!M.has_rays) {
    say("decode check: file has no RAYS/BALL (exact data only): nothing to compare, skipped\n");
    return;
  }
  Decoder D(C);
  Uint64 d = M.d;
  Uint64 maxb = 0;
  for (auto& e : M.el)
    if (e->kind != "rep") maxb = std::max(maxb, e->max_bits);
  auto rep_bits = [&](Uint64 r) { return M.el[M.idx[ekey("rep", r, -1, -1)]]->max_bits; };
  // Mode per ray: "full" decodes all d coordinates (normalized, gauged, compared with every BALL
  // record); "ratio" is the spec 5.2a spot check at zero and divisor positions, with entry word0,
  // so it needs only the ray word. A ray is decoded only if every entry it needs takes at most
  // --max-steps generator applications. auto: d <= 64 -> full per ray, falling back to ratio when
  // full exceeds the step budget; d > 64 -> ratio on the single cheapest ray (smallest ray-word
  // digit sum). full / ratio: that mode for every ray within the budget.
  say("decode premises: field, group, actions and divisor-kernel fixity are not algebraically "
      "certified\n");
  say("decode check (--decode %s, step budget %d)\n", O.decode.c_str(), O.max_steps);
  auto steps_of = [&](const RayRec& R, Uint64 m, vector<Uint64>* wout) {
    Uint64 r;
    vector<Uint64> w;
    D.entry_word(m, r, w);
    Uint64 st = 0;
    for (size_t l = 0; l < w.size(); l++) {
      w[l] = (w[l] + R.word[l]) % M.smith[l];
      st += w[l];
    }
    if (wout) *wout = w;
    return st;
  };
  vector<size_t> rays;
  for (size_t r = 0; r < M.rays.size(); r++) {
    if (!O.only_ray.empty() && M.rays[r].label != O.only_ray) continue;
    rays.push_back(r);
  }
  if (!O.only_ray.empty() && rays.empty())
    throw UsageErr("--ray " + O.only_ray + ": no such ray label");
  std::map<size_t, bool> plan;  // ray -> full?
  size_t skipped = 0;
  Uint64 required_steps = 0;
  for (size_t ri : rays) {
    const RayRec& R = M.rays[ri];
    Uint64 sfull = 0;
    for (Uint64 m = 0; m < d; m++) sfull = std::max(sfull, steps_of(R, m, nullptr));
    Uint64 sratio =
        std::max(steps_of(R, 0, nullptr), steps_of(R, M.representatives.back(), nullptr));
    bool want_full = O.decode == "full" || (O.decode == "auto" && d <= 64);
    if (want_full && sfull <= (Uint64)O.max_steps)
      plan[ri] = true;
    else if ((O.decode != "full") && sratio <= (Uint64)O.max_steps)
      plan[ri] = false;
    else {
      ++skipped;
      required_steps = std::max(required_steps, O.decode == "full" ? sfull : sratio);
      say("  ray %-4s: skipped (needs %llu generator applications for %s > budget %d; use "
          "--max-steps)\n",
          R.label.c_str(), (unsigned long long)(O.decode == "full" ? sfull : sratio),
          O.decode == "full" ? "some entry" : "the ratio entries", O.max_steps);
    }
    if (want_full && sfull > (Uint64)O.max_steps && plan.count(ri))
      say("  ray %-4s: full decode needs %llu applications > budget; using the 5.2a ratio check\n",
          R.label.c_str(), (unsigned long long)sfull);
  }
  if (O.decode == "auto" && d > 64 && plan.size() > 1) {  // keep the single cheapest ray
    size_t best = plan.begin()->first;
    Uint64 bs = UINT64_MAX;
    for (auto& kv : plan) {
      Uint64 s = steps_of(M.rays[kv.first], 0, nullptr);
      if (s < bs) {
        bs = s;
        best = kv.first;
      }
    }
    for (auto& kv : plan)
      if (kv.first != best)
        say("  ray %-4s: not decoded (auto mode at d > 64 checks one ray; use --decode ratio or "
            "--ray LABEL)\n",
            M.rays[kv.first].label.c_str());
    bool f = plan[best];
    plan.clear();
    plan[best] = f;
  }
  if (plan.empty() && O.release && O.decode == "auto" && !rays.empty()) {
    // --release must not pass without any ray decoded against the exact data: raise the step budget
    // automatically for the ray whose ratio check is cheapest.
    size_t best = rays[0];
    Uint64 bs = UINT64_MAX;
    bool f = O.decode == "full";
    for (size_t ri : rays) {
      Uint64 s = 0;
      if (f) {
        for (Uint64 m = 0; m < d; m++) s = std::max(s, steps_of(M.rays[ri], m, nullptr));
      } else
        s = steps_of(M.rays[ri], 0, nullptr);
      if (s < bs) {
        bs = s;
        best = ri;
      }
    }
    say("  --release: no ray within the step budget %d; budget raised automatically to %llu for "
        "ray %s (%s)\n",
        O.max_steps, (unsigned long long)bs, M.rays[best].label.c_str(),
        f ? "full decode" : "5.2a ratio check");
    plan[best] = f;
  }
  C.dec_total = rays.size();
  // Decode the entries `ents` of ray R as certified complex balls; insufficient precision is a
  // refusal (spec 3.6): PrecErr, reported as DECODE-INCONCLUSIVE (exit 3), never as REJECT.
  auto decode_set = [&](const RayRec& R, const vector<Uint64>& ents, vector<ACB>& v,
                        slong& prec_used) {
    vector<vector<Uint64>> words(ents.size());
    for (size_t j = 0; j < ents.size(); j++) steps_of(R, ents[j], &words[j]);
    Uint64 bits0 = maxb;
    for (Uint64 m : ents) {
      Uint64 r;
      vector<Uint64> w;
      D.entry_word(m, r, w);
      bits0 = std::max(bits0, rep_bits(r));
    }
    slong prec = std::max<slong>(700, (slong)bits0 + O.extra_bits + 64);
    v.assign(ents.size(), ACB());
    bool done = false;
    for (int attempt = 0; attempt < 6 && !done; attempt++, prec += prec / 2) {
      try {
        D.set_prec(prec);
        RootSet b0 = D.base();
        std::map<vector<Uint64>, RootSet> cache;
        for (size_t j = 0; j < ents.size(); j++) {
          Uint64 r;
          vector<Uint64> w;
          D.entry_word(ents[j], r, w);
          auto it = cache.find(words[j]);
          if (it == cache.end()) it = cache.emplace(words[j], D.roots_for_word(b0, words[j])).first;
          D.eval_elem(M.idx[ekey("rep", r, -1, -1)], it->second, v[j].v);
          if (!acb_is_finite(v[j].v) || !rad_small(v[j].v, 140))
            throw PrecErr("entry radius too large");
        }
        done = true;
        prec_used = prec;
      } catch (PrecErr& e) {
        say("  [prec %ld insufficient: %s; retrying]\n", (long)prec, e.what());
      }
    }
    if (!done)
      throw PrecErr(
          "ray " + R.label +
          ": could not reach a certified decode up to the precision limit (refusal, spec 3.6)");
  };
  for (auto& kv : plan) {
    size_t ri = kv.first;
    bool full_mode = kv.second;
    const RayRec& R = M.rays[ri];
    BallRay& BR = C.balls[ri];
    if (full_mode) {
      vector<Uint64> ents;
      for (Uint64 m = 0; m < d; m++) ents.push_back(m);
      vector<ACB> v;
      slong prec = 0;
      decode_set(R, ents, v, prec);
      // the gauge needs v_a != 0 and the norm needs ||v|| != 0 (spec 2); a ball containing 0 is a
      // refusal
      if (acb_contains_zero(v[R.anchor].v))
        throw PrecErr("ray " + R.label + ": decoded anchor coordinate v_" +
                      std::to_string(R.anchor) + " contains 0 (gauge undefined)");
      ACB nrm, ph, w;
      Real s, t2;
      for (Uint64 j = 0; j < d; j++) {
        acb_abs(t2.v, v[j].v, prec);
        arb_mul(t2.v, t2.v, t2.v, prec);
        arb_add(s.v, s.v, t2.v, prec);
      }
      if (arb_contains_zero(s.v)) throw PrecErr("ray " + R.label + ": decoded norm contains 0");
      arb_sqrt(s.v, s.v, prec);
      acb_set_arb(nrm.v, s.v);
      acb_conj(ph.v, v[R.anchor].v);
      acb_abs(t2.v, v[R.anchor].v, prec);
      acb_div_arb(ph.v, ph.v, t2.v, prec);
      Int64 minacc = INT64_MAX;
      for (Uint64 j = 0; j < d; j++) {
        acb_mul(w.v, v[j].v, ph.v, prec);
        acb_div(w.v, w.v, nrm.v, prec);
        if (!acb_is_finite(w.v))
          throw PrecErr("ray " + R.label + ": normalized coordinate " + std::to_string(j) +
                        " not finite");
        if (j == R.anchor) arb_zero(acb_imagref(w.v));
        if (!acb_overlaps(w.v, BR.w[j].v))
          reject("ray " + R.label + ": decoded coordinate disagrees with BALL");
        for (int part = 0; part < 2; ++part) {
          arb_srcptr decoded = part ? acb_imagref(w.v) : acb_realref(w.v);
          arb_srcptr stored = part ? acb_imagref(BR.w[j].v) : acb_realref(BR.w[j].v);
          double a = rounded_binary64(decoded, prec), b = rounded_binary64(stored, prec);
          if (!same_binary64(a, b)) reject("decoded and BALL binary64 roundings disagree");
        }
        minacc = std::min<Int64>(minacc, acb_rel_accuracy_bits(w.v));
      }
      say("  ray %-4s: all %llu coordinates decoded from TOWR+BODY+actions (prec %ld), certified "
          "binary64 agreement and overlap "
          "BALL  [min decoded rel. accuracy %lld bits]\n",
          R.label.c_str(), (unsigned long long)d, (long)prec, (long long)minacc);
      C.dec_full++;
    } else {
      // Section 5.2a: try positive proper divisor positions in descending order.
      // Their entry words are zero; denominators must exclude zero. If none works, refuse
      // (DECODE-INCONCLUSIVE), never a vacuous pass.
      bool ok = false;
      for (auto it = M.representatives.rbegin(); it != M.representatives.rend() && *it && !ok;
           ++it) {
        Uint64 k = *it;
        if (acb_contains_zero(BR.w[k].v) || !acb_is_finite(BR.w[k].v)) {
          say("  ray %-4s: BALL w_%llu contains 0; trying the next divisor\n", R.label.c_str(),
              (unsigned long long)k);
          continue;
        }
        vector<Uint64> ents = {0, k};
        vector<ACB> v;
        slong prec = 0;
        decode_set(R, ents, v, prec);
        if (acb_contains_zero(v[1].v)) {
          say("  ray %-4s: decoded v_%llu contains 0; trying the next divisor\n", R.label.c_str(),
              (unsigned long long)k);
          continue;
        }
        ACB q, qb;
        acb_div(q.v, v[0].v, v[1].v, prec);
        acb_div(qb.v, BR.w[0].v, BR.w[k].v, prec);
        if (!acb_is_finite(q.v) || !acb_is_finite(qb.v)) {
          throw PrecErr("ray " + R.label + ": ratio not finite");
        }
        if (!acb_overlaps(q.v, qb.v))
          reject("ray " + R.label + ": decoded v_0/v_" + std::to_string(k) +
                 " disagrees with BALL ratio (spec 5.2a)");
        string s1 = acbstr(q.v), s2 = acbstr(qb.v);
        {
          mag_t a, b;
          mag_init(a);
          mag_init(b);
          mag_set(a, arb_radref(acb_realref(q.v)));
          mag_set(b, arb_radref(acb_imagref(q.v)));
          mag_max(a, a, b);
          s1 += "  [decoded radius ~2^" + std::to_string((long)mag_get_d_log2_approx(a)) + "]";
          mag_set(a, arb_radref(acb_realref(qb.v)));
          mag_set(b, arb_radref(acb_imagref(qb.v)));
          mag_max(a, a, b);
          s2 += "  [BALL ratio radius ~2^" + std::to_string((long)mag_get_d_log2_approx(a)) + "]";
          mag_clear(a);
          mag_clear(b);
        }
        say("  ray %-4s: entries 0 and %llu decoded at prec %ld; ratio v_0/v_%llu = %s\n           "
            "    BALL ratio                                = %s   -> overlap OK\n",
            R.label.c_str(), (unsigned long long)k, (long)prec, (unsigned long long)k, s1.c_str(),
            s2.c_str());
        ok = true;
      }
      if (!ok)
        throw PrecErr("ray " + R.label +
                      ": spec 5.2a ratio check impossible (every proper divisor k has w_k or v_k "
                      "containing 0)");
      C.dec_ratio++;
    }
  }
  if (skipped && (O.decode == "full" || O.decode == "ratio"))
    throw PrecErr(std::to_string(skipped) + " requested rays were not checked; use --max-steps " +
                  std::to_string(required_steps) + " to cover them");
}
}  // namespace sic_check::verifier
