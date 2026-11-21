// SPDX-License-Identifier: AGPL-3.0-or-later
#include "norm.hpp"

#include "../format/metadata.hpp"
#include "../runtime/diagnostics.hpp"
#include "embeddings.hpp"
namespace norm = sic_check::norm;
namespace sic_check::norm {
int certify(const std::string& path, unsigned threads, bool estimate, bool leaf_estimate,
            bool verify_input, const ReadOptions& input_options,
            const std::optional<size_t>& only_ray, slong max_bits) {
  const double t0 = now();
  norm::Reader reader(path, input_options);
  std::printf("%s\n", reader.summary().c_str());
  if (verify_input) return 0;
  const auto& meta = reader.meta();
  norm::resource(meta.d <= 4096 && meta.D <= 4096, "norm certificate d,D <= 4096");
  norm::resource(std::has_single_bit(meta.d), "norm certificate supports power-of-two d only");
  if (meta.D0 <= 1) refuse("only real quadratic K (D0 > 1) is supported");
  if (leaf_estimate) {
    norm::Tower T(meta);
    std::printf("leaf-estimate d %llu D0 %lld [L:K] %llu levels %u degrees",
                (unsigned long long)meta.d, (long long)meta.D0, (unsigned long long)meta.D, T.t);
    for (auto p : meta.degrees) std::printf(" %u", p);
    std::printf("\n");
    uint64_t maxbits = 0;
    for (const auto& e : meta.elements) maxbits = std::max<uint64_t>(maxbits, e.max_bits);
    for (slong hp = 256 + 2 * static_cast<slong>(maxbits);; hp *= 2) {
      LeafEmbeddings le(reader, T, hp);
      if (!le.build()) {
        if (hp > (slong(1) << 16)) refuse("leaf estimate: isolation failed");
        continue;
      }
      double hmax = -1e300;
      bool finite = true;
      for (const auto& [pos, id] : meta.representative) {
        const auto lam = norm::read_leaves(reader, id, T);
        const auto v = le.values(lam.data(), T.t);
        double hr = -1e300;
        for (const auto& z : v) {
          if (!acb_is_finite(z.v)) finite = false;
          hr = std::max(hr, log2_approx(z.v));
        }
        std::printf("  rep %lld: max_bits %llu, max log2 |phi(eta)| over %zu homs = %.2f\n",
                    (long long)pos, (unsigned long long)meta.elements[id].max_bits, v.size(), hr);
        hmax = std::max(hmax, hr);
      }
      if (!finite) {
        if (hp > (slong(1) << 16)) refuse("leaf estimate: nonfinite");
        continue;
      }
      double normW = 0;  // log2 |N_{A/Q}(prod f_i'(g_i))| = sum_i [A:F_{i+1}] sum_{homs of
                         // F_{i+1}} log2|f_i'(r)|
      for (unsigned i = 0; i < T.t; ++i) {
        const double mult = double(meta.D) / double(T.N[i + 1]);
        normW += mult * le.lvl_logabs_sum[i];
        std::printf("  level %u (p=%u): log2|phi(c_i)| in [%.1f, %.1f]\n", i, T.p[i], le.lvl_min[i],
                    le.lvl_max[i]);
      }
      std::printf(
          "leaf-estimate precision %ld: h_eta ~ %.2f, log2|N(W)| ~ %.1f, n = %llu, %.2f s\n",
          (long)hp, hmax, normW, (unsigned long long)(2 * meta.D), now() - t0);
      return 0;
    }
  }
  norm::BuildOptions bo;
  bo.numeric_check = false;
  norm::Builder B(reader, bo);
  std::printf("sic-certify-norm 1.0\nfile %s\nd %llu D0 %lld [L:K] %llu levels %u\n", path.c_str(),
              (unsigned long long)meta.d, (long long)meta.D0, (unsigned long long)meta.D, B.T.t);
  std::printf("degrees");
  for (auto p : meta.degrees) std::printf(" %u", p);
  std::printf("\nsmith");
  for (auto n : meta.smith) std::printf(" %llu", (unsigned long long)n);
  std::printf("\n");
  std::fflush(stdout);

  // (1) exact tower, action endomorphisms, certified roots, conjugation
  B.build_exact();
  const double t1 = now();
  std::printf("exact tower and representatives in power basis: %.2f s\n", t1 - t0);
  std::fflush(stdout);
  for (size_t l = 0; l < B.tau.size(); ++l)
    B.check_endomorphism(B.tau[l], false, "action record " + std::to_string(l));
  const double t2 = now();
  std::printf("action records are ring endomorphisms (exact): %.2f s\n", t2 - t1);
  B.certify_roots();
  B.find_conjugation();
  const double t3 = now();
  std::printf("complex conjugation word");
  for (int w : B.report.conjugation) std::printf(" %d", w);
  std::printf(" (exact endomorphism): %.2f s\n", t3 - t2);
  Ctx ctx(reader, B);
  const norm::Tower& T = B.T;
  const slong lp = bo.precision;
  {
    // Rigorous identification conj(iota(g_i)) = iota(C(g_i)): both are roots of conj(f_i^iota);
    // the image ball must overlap exactly one certified isolating ball, the one of conj(root_i).
    const auto num = B.conj_images_numeric(B.conj, B.conj_k);
    ComplexPolynomial P;
    for (unsigned i = 0; i < T.t; ++i) {
      ctx.level_poly(P, i, B.roots, B.omega.v, lp);
      auto iso = isolate(P, T.p[i], lp);
      if (iso.empty()) refuse("conjugation check: level roots not isolated");
      // hits: conjugated isolating balls meeting the image ball; own: those meeting conj(root_i).
      // Both must be exactly one and the same ball (each ball holds exactly one root).
      size_t hits = 0, own = 0, hit_index = SIZE_MAX, own_index = SIZE_MAX;
      acb_t cr;
      acb_init(cr);
      acb_conj(cr, B.roots[i].v);
      for (size_t j = 0; j < iso.size(); ++j) {
        acb_conj(iso[j].v, iso[j].v);
        if (acb_overlaps(iso[j].v, num[i].v)) {
          ++hits;
          hit_index = j;
        }
        if (acb_overlaps(iso[j].v, cr)) {
          ++own;
          own_index = j;
        }
      }
      acb_clear(cr);
      if (hits != 1 || own != 1 || hit_index != own_index)
        refuse("conjugation image of g_" + std::to_string(i) + " not isolated as conj(root)");
    }
    std::printf("conj o iota = iota o C on all %u generators (isolation)\n", T.t);
  }

  // (2) integrality. G_i = kappa_i g_i (kappa_i positive integers, chosen level by level) are
  // algebraic integers when every monic relation, rewritten in the G-monomials, has coefficients
  // in O_K = Z[omega]; then Z[omega, G] is an order and q * eta_r (q = lcm of the denominators of
  // the G-basis coefficients) lies in it.
  std::vector<Integer> kappa(T.t);
  for (auto& k : kappa) fmpz_set_ui(k.v, 1);
  // kappa^e for a monomial index e in F_n
  auto kappa_pow = [&](fmpz_t out, size_t e, unsigned n) {
    fmpz_one(out);
    for (unsigned i = 0; i < n; ++i) {
      const size_t digit = (e / T.N[i]) % T.p[i];
      fmpz_t t;
      fmpz_init(t);
      fmpz_pow_ui(t, kappa[i].v, digit);
      fmpz_mul(out, out, t);
      fmpz_clear(t);
    }
  };
  // denominator of x / kappa^e (x rational)
  auto g_den = [&](fmpz_t den, const fmpq_t x, size_t e, unsigned n) {
    fmpz_t kp, g;
    fmpz_init(kp);
    fmpz_init(g);
    kappa_pow(kp, e, n);
    // x = a/b; x/kp = a/(b kp); reduced denominator = b kp / gcd(a, b kp)
    fmpz_mul(kp, kp, fmpq_denref(x));
    fmpz_gcd(g, fmpq_numref(x), kp);
    if (fmpz_is_zero(fmpq_numref(x)))
      fmpz_one(den);
    else
      fmpz_divexact(den, kp, g);
    fmpz_clear(kp);
    fmpz_clear(g);
  };
  for (unsigned i = 0; i < T.t; ++i) {
    // need kappa_i^(p-k) * a_k (G-basis) integral for every k < p
    const unsigned p = T.p[i];
    fmpz_t need, den, f, pp;
    fmpz_init_set_ui(need, 1);
    fmpz_init(den);
    fmpz_init(f);
    fmpz_init(pp);
    for (unsigned k = 0; k < p; ++k) {
      fmpz_t lden;
      fmpz_init_set_ui(lden, 1);
      for (size_t e = 0; e < T.f[i][k].size(); ++e) {
        g_den(den, T.f[i][k][e].a, e, i);
        fmpz_lcm(lden, lden, den);
        g_den(den, T.f[i][k][e].b, e, i);
        fmpz_lcm(lden, lden, den);
      }
      // smallest kappa with kappa^(p-k) divisible by lden: prod over primes r^ceil(v_r/(p-k))
      fmpz_factor_t fac;
      fmpz_factor_init(fac);
      fmpz_factor(fac, lden);
      fmpz_one(f);
      for (slong u = 0; u < fac->num; ++u) {
        const ulong ex_ = (fac->exp[u] + (p - k) - 1) / (p - k);
        fmpz_pow_ui(pp, fac->p + u, ex_);
        fmpz_mul(f, f, pp);
      }
      fmpz_factor_clear(fac);
      fmpz_lcm(need, need, f);
      fmpz_clear(lden);
    }
    fmpz_set(kappa[i].v, need);
    fmpz_clear(need);
    fmpz_clear(den);
    fmpz_clear(f);
    fmpz_clear(pp);
  }
  slong kappa_bits = 0;
  for (auto& k : kappa) kappa_bits += fmpz_bits(k.v) - 1;
  norm::Integer q_storage;
  fmpz* q = q_storage.v;
  fmpz_one(q);
  {
    fmpz_t den;
    fmpz_init(den);
    for (const auto& r : B.reps)
      for (size_t e = 0; e < r.size(); ++e) {
        g_den(den, r[e].a, e, T.t);
        fmpz_lcm(q, q, den);
        g_den(den, r[e].b, e, T.t);
        fmpz_lcm(q, q, den);
      }
    fmpz_clear(den);
  }
  std::vector<Elt> qreps;
  slong coef_bits = 0;
  for (const auto& r : B.reps) {
    Elt y(r.size());
    for (size_t e = 0; e < r.size(); ++e) {
      fmpq_mul_fmpz(y[e].a, r[e].a, q);
      fmpq_mul_fmpz(y[e].b, r[e].b, q);
      coef_bits = std::max<slong>(
          coef_bits, std::max(fmpz_bits(fmpq_numref(y[e].a)), fmpz_bits(fmpq_numref(y[e].b))));
    }
    qreps.push_back(std::move(y));
  }
  kappa.clear();
  const slong qbits = fmpz_bits(q);
  std::printf(
      "embedding integral generators G_i = kappa_i g_i (sum of log2 kappa_i ~ %ld bits); q = lcm "
      "of "
      "G-basis denominators has %ld bits; "
      "max numerator of q*eta %ld bits\n",
      (long)kappa_bits, (long)qbits, (long)coef_bits);

  // (3) e_H over all algebra homomorphisms
  slong eH = LONG_MIN;
  double eH_approx = -1e300;
  {
    slong hp = 256 + coef_bits;
    for (int attempt = 0;; ++attempt, hp *= 2) {
      AllEmbeddings all(T, meta.D0, hp);
      if (!all.build()) {
        if (attempt >= 4) refuse("all-embedding root isolation failed");
        continue;
      }
      eH = LONG_MIN;
      eH_approx = -1e300;
      bool finite = true;
      for (const auto& y : qreps) {
        const auto v = all.values(y.data(), T.t);
        for (const auto& z : v) {
          if (!acb_is_finite(z.v)) finite = false;
          eH = std::max(eH, log2_upper(z.v));
          eH_approx = std::max(eH_approx, log2_approx(z.v));
        }
      }
      if (finite) {
        std::printf(
            "bound %zu algebra homomorphisms at %ld bits: max |phi(q eta_r)| < 2^%ld (approx "
            "2^%.2f)\n",
            all.count(T.t), (long)hp, (long)eH, eH_approx);
        break;
      }
      if (attempt >= 4) refuse("all-embedding values not finite");
    }
  }
  if (eH == LONG_MIN || eH > (slong(1) << 40))
    refuse("conjugate bound unusable (zero representatives or overflow)");
  if (eH < 0) eH = 0;
  const uint64_t d = meta.d;
  const uint64_t n = 2 * meta.D;
  const slong m = ceil_log2_u(3 * d * d + d) + 4 * eH;
  const slong target = static_cast<slong>(n - 1) * m;  // need |iota(R)| < 2^-target
  const slong psic = target + ceil_log2_u((d + 1) * d * d) + 4 * eH + 1;
  std::printf(
      "residual n = 2[L:K] = %llu, h = %ld, m = %ld, threshold |iota(R)| < 2^-%ld, P_SIC(overlap "
      "form) = %ld bits\n",
      (unsigned long long)n, (long)eH, (long)m, (long)target, (long)psic);
  std::fflush(stdout);
  if (estimate) {
    std::printf("estimate only: %.2f s\n", now() - t0);
    return 0;
  }

  // (4) transported embeddings at low precision (root identification)
  std::map<std::vector<uint64_t>, std::vector<ComplexBall>> paths;
  paths[std::vector<uint64_t>(meta.smith.size(), 0)] = B.roots;
  std::mutex mu;
  ComplexPolynomial P;
  std::function<const std::vector<ComplexBall>&(const std::vector<uint64_t>&)> path_of =
      [&](const std::vector<uint64_t>& w) -> const std::vector<ComplexBall>& {
    auto it = paths.find(w);
    if (it != paths.end()) return it->second;
    size_t l = w.size();
    while (l-- > 0)
      if (w[l]) break;
    auto prev = w;
    --prev[l];
    const std::vector<ComplexBall> V = path_of(prev);  // copy: the map may rehash
    std::vector<ComplexBall> cand(T.t), next(T.t);
    for (unsigned i = 0; i < T.t; ++i)
      horner(cand[i].v, T, B.tau[l][i].data(), i + 1, V, B.omega.v, lp);
    for (unsigned i = 0; i < T.t; ++i) {
      ctx.level_poly(P, i, next, B.omega.v, lp);
      auto iso = isolate(P, T.p[i], lp);
      if (iso.empty()) refuse("transport: level roots not isolated");
      int pick = -1, hits = 0;
      for (unsigned j = 0; j < iso.size(); ++j)
        if (acb_overlaps(iso[j].v, cand[i].v)) {
          pick = static_cast<int>(j);
          ++hits;
        }
      if (hits != 1)
        refuse("transport: candidate root overlaps " + std::to_string(hits) + " isolating balls");
      next[i] = iso[pick];
    }
    return paths.emplace(w, std::move(next)).first->second;
  };

  std::vector<RayResult> results;
  for (size_t ri = 0; ri < meta.rays.size(); ++ri) {
    if (only_ray && *only_ray != ri) continue;
    const double tr0 = now();
    RayResult res;
    res.label = meta.rays[ri].label;
    std::vector<std::vector<uint64_t>> words(d);
    std::vector<size_t> rep(d);
    for (uint64_t mm = 0; mm < d; ++mm) {
      const auto entry = meta.entry(mm);
      words[mm] = norm::words_add(meta.rays[ri].word, entry.word, meta.smith);
      rep[mm] = B.rep_index.at(entry.representative);
      path_of(words[mm]);
    }
    // magnitude of the evaluation sums at low precision -> working precision
    slong eA = LONG_MIN;
    for (uint64_t mm = 0; mm < d; ++mm) {
      const auto& V = paths.at(words[mm]);
      const auto mono = norm::monomials(T, T.t, V, lp);
      acb_t s, c;
      acb_init(s);
      acb_init(c);
      acb_zero(s);
      for (size_t e = 0; e < qreps[rep[mm]].size(); ++e) {
        if (qreps[rep[mm]][e].zero()) continue;
        norm::acb_from_ke(c, qreps[rep[mm]][e], B.omega.v, lp);
        acb_mul(c, c, mono[e].v, lp);
        arb_t ab;
        arb_init(ab);
        acb_abs(ab, c, lp);
        arb_add(acb_realref(s), acb_realref(s), ab, lp);
        arb_clear(ab);
      }
      eA = std::max(eA, log2_upper(s));
      acb_clear(s);
      acb_clear(c);
    }
    slong prec = target + 4 * std::max<slong>(eA, 0) + 2 * ceil_log2_u(d) + 256;
    for (int attempt = 0; attempt < 3; ++attempt, prec += prec / 4) {
      if (prec > max_bits)
        refuse("working precision " + std::to_string(prec) + " exceeds --max-bits");
      res.precision = prec;
      ComplexBall omegaP;
      omega_at(omegaP.v, T, meta.D0, false, prec);
      std::vector<ComplexBall> z(d);
      std::atomic<uint64_t> next_m{0};
      std::atomic<bool> bad{false};
      std::string why;
      auto worker = [&]() {
        acb_poly_t Q;
        acb_poly_init(Q);
        for (;;) {
          const uint64_t mm = next_m.fetch_add(1);
          if (mm >= d || bad) break;
          const auto& low = paths.at(words[mm]);
          std::vector<ComplexBall> hi(T.t);
          bool ok = true;
          for (unsigned i = 0; i < T.t && ok; ++i) {
            if (T.p[i] != 2) {
              ok = false;
              break;
            }  // odd levels: not implemented at high precision
            ctx.level_poly(Q, i, hi, omegaP.v, prec);
            auto iso = isolate(Q, 2, prec);
            if (iso.empty()) {
              ok = false;
              break;
            }
            int pick = -1, hits = 0;
            for (int j = 0; j < 2; ++j)
              if (acb_overlaps(iso[j].v, low[i].v)) {
                pick = j;
                ++hits;
              }
            if (hits != 1) {
              ok = false;
              break;
            }
            hi[i] = iso[pick];
          }
          if (ok) horner(z[mm].v, T, qreps[rep[mm]].data(), T.t, hi, omegaP.v, prec);
          if (!ok) {
            std::lock_guard<std::mutex> g(mu);
            bad = true;
            why = "high-precision root refinement failed (entry " + std::to_string(mm) + ")";
          }
        }
        acb_poly_clear(Q);
        flint_cleanup();
      };
      {
        std::vector<std::thread> pool;
        for (unsigned th = 0; th < threads; ++th) pool.emplace_back(worker);
        for (auto& th : pool) th.join();
      }
      if (bad) refuse(why);
      // N and canonical residuals
      ComplexBall Nn;
      {
        RealBall t;
        acb_zero(Nn);
        for (uint64_t j = 0; j < d; ++j) {
          acb_abs(t, z[j].v, prec);
          arb_sqr(t, t, prec);
          arb_add(acb_realref(Nn), acb_realref(Nn), t, prec);
        }
      }
      if (!arb_is_positive(acb_realref(Nn))) refuse("norm not certified positive");
      const uint64_t half = d / 2;
      std::vector<slong> upper(half + 1, LONG_MIN);
      std::vector<double> approx(half + 1, -1e300);
      std::atomic<uint64_t> next_a{0};
      auto rworker = [&]() {
        std::vector<ComplexBall> f(d), fc(d);
        acb_t S, N2;
        acb_init(S);
        acb_init(N2);
        acb_sqr(N2, Nn, prec);
        for (;;) {
          const uint64_t a = next_a.fetch_add(1);
          if (a > half) break;
          for (uint64_t j = 0; j < d; ++j) {
            acb_conj(fc[j].v, z[(j + a) % d].v);
            acb_mul(f[j].v, z[j].v, fc[j].v, prec);
            acb_conj(fc[j].v, f[j].v);
          }
          slong up = LONG_MIN;
          double ap = -1e300;
          for (uint64_t k = a; k <= half; ++k) {
            acb_zero(S);
            for (uint64_t j = 0; j < d; ++j) acb_addmul(S, f[j].v, fc[(j + k) % d].v, prec);
            acb_mul_ui(S, S, d + 1, prec);
            const int deltas = (a == 0) + (k == 0);
            for (int u = 0; u < deltas; ++u) acb_sub(S, S, N2, prec);
            up = std::max(up, log2_upper(S));
            ap = std::max(ap, log2_approx(S));
          }
          upper[a] = up;
          approx[a] = ap;
        }
        acb_clear(S);
        acb_clear(N2);
        flint_cleanup();
      };
      {
        std::vector<std::thread> pool;
        for (unsigned th = 0; th < threads; ++th) pool.emplace_back(rworker);
        for (auto& th : pool) th.join();
      }
      res.worst_upper = *std::max_element(upper.begin(), upper.end());
      res.worst_log2_R = *std::max_element(approx.begin(), approx.end());
      res.pairs = (half + 1) * (half + 2) / 2;
      res.pass = res.worst_upper <= -target;  // |R| < 2^upper <= 2^-target
      // BALL consistency (not part of the proof)
      {
        RealBall nrm, t;
        arb_sqrt(nrm, acb_realref(Nn), prec);
        ComplexBall gauge, w, pub;
        const uint64_t anc = meta.rays[ri].anchor;
        acb_conj(gauge.v, z[anc].v);
        acb_abs(t, z[anc].v, prec);
        acb_div_arb(gauge.v, gauge.v, t, prec);
        const auto parts = reader.ball_parts(ri);
        res.ball_ok = true;
        for (uint64_t j = 0; j < d; ++j) {
          acb_mul(w.v, z[j].v, gauge.v, prec);
          acb_div_arb(w.v, w.v, nrm, prec);
          norm::ball_part_arb(acb_realref(pub.v), parts[2 * j]);
          norm::ball_part_arb(acb_imagref(pub.v), parts[2 * j + 1]);
          if (!acb_overlaps(w.v, pub.v)) res.ball_ok = false;
        }
      }
      if (res.pass) break;
      std::printf(
          "  ray %s attempt %d at %ld bits: worst |R| < 2^%ld, need <= 2^-%ld; raising "
          "precision\n",
          res.label.c_str(), attempt, (long)prec, (long)res.worst_upper, (long)target);
    }
    res.seconds = now() - tr0;
    std::printf(
        "ray %zu %s: %s; canonical pairs %zu; worst |iota(R)| < 2^%ld (approx 2^%.0f) vs "
        "threshold 2^-%ld; "
        "working precision %ld bits; BALL %s; %.2f s\n",
        ri, res.label.c_str(), res.pass ? "EXACT SIC (Theorem B)" : "INCONCLUSIVE", res.pairs,
        (long)res.worst_upper, res.worst_log2_R, (long)target, (long)res.precision,
        res.ball_ok ? "consistent" : "MISMATCH", res.seconds);
    std::fflush(stdout);
    results.push_back(res);
  }
  bool all = !results.empty();
  for (const auto& r : results) all = all && r.pass && r.ball_ok;
  std::printf("summary: %zu rays, %s, total %.2f s\n", results.size(),
              all ? "ALL CERTIFIED" : "NOT ALL CERTIFIED", now() - t0);
  return all ? 0 : 1;
}
}  // namespace sic_check::norm
