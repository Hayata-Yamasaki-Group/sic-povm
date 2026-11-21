// SPDX-License-Identifier: AGPL-3.0-or-later
#include "crt_certificate.hpp"

#include "../algebra/modular_field.hpp"
#include "../algebra/ntt.hpp"
#include "../format/canonical_json.hpp"
#include "../format/container.hpp"
#include "../format/integer_codec.hpp"
#include "../runtime/diagnostics.hpp"
#include "../runtime/parallel_for.hpp"
#include "archimedean.hpp"
#include "residual.hpp"
namespace sic_check::crt {
using namespace sic_check::crt::sicx;

int certify(const string& path, const Trust& input_trust, const string& inject, Uint64 seed,
            bool bound_only, bool no_hints, Int64 int_max_dim, Int64 force_primes, slong prec0) {
  if (prec0 < 0) refuse("initial precision must be positive or zero for automatic precision");
  const double t0 = now();
  Ctx C;
  C.load(path, input_trust);
  // fault injection (tests only): the certificate must then fail
  if (!inject.empty()) {
    size_t id = SIZE_MAX;
    if (inject == "rep")
      id = C.repid[1];
    else if (inject == "act") {
      for (int j = C.t - 1; j >= 0 && id == SIZE_MAX; --j)
        for (size_t l = 0; l < C.naxes && id == SIZE_MAX; ++l)
          if (C.act[j][l] != SIZE_MAX) id = C.act[j][l];
    } else if (inject != "conj")
      refuse("unknown --inject (rep, act, conj)");
    if (id != SIZE_MAX) {
      auto& X = C.co[id]->X;
      fmpz_add_ui(&X[X.size() / 2], &X[X.size() / 2], 1);
    }
    say("FAULT INJECTION %s: the result below is a negative control, not a certificate\n",
        inject.c_str());
  }
  say("sic-certify-crt 1.0 (CRT norm-lemma certificate; independent SICX input)\n");
  say("file %s\nsha256 %s\nexact root %s\n", path.c_str(), hex(C.in.file_sha).c_str(),
      hex(C.in.exact_root).c_str());
  say("file root %s\ninput %s\n", hex(C.in.merkle).c_str(), C.in.scope.describe().c_str());
  say("d %llu D0 %lld [L:K] %llu levels %d degrees", (unsigned long long)C.d, (long long)C.D0,
      (unsigned long long)C.D, C.t);
  for (auto p : C.deg) say(" %llu", (unsigned long long)p);
  say(" smith");
  for (auto n : C.smith) say(" %llu", (unsigned long long)n);
  say("\nrays %zu%s; threads %u; seed %llu%s\n", C.in.rays.size(),
      C.in.rays.empty() ? " (exact data only)" : "", g_threads, (unsigned long long)seed, "");
  say("load %.2f s, RSS %.2f GB\n", now() - t0, rss_gb());
  const Int64 n = 2 * (Int64)C.D;

  say("canonical unit-generator words satisfy all order relations (checked by independent "
      "input)\n");

  // ---- prime search machinery
  Witnesses Wit;
  Wit.prime.assign(C.t, 0);
  const Uint64 pmod = 2 * C.d;  // p = 1 mod 2d (NTT of length d)
  std::atomic<Uint64> candidates{0};
  auto search = [&](std::mt19937_64& rng, ModP& M) -> Uint64 {
    for (;;) {
      Uint64 p = (1ull << 61) + (rng() % (1ull << 61));
      p -= (p - 1) % pmod;
      if (p < (1ull << 61) + 1) continue;
      if (!n_is_prime(p)) continue;
      candidates++;
      if (try_prime(M, p, Wit) == C.t) return p;
    }
  };

  // ---- (G) first completely split prime: point permutations, group relations, Galois structure
  std::mt19937_64 rng0(seed * 1000003ull);
  ModP M0(C);
  double tg = now();
  const Uint64 p0 = search(rng0, M0);
  say("first completely split prime p = %llu after %llu candidates (%.2f s)\n",
      (unsigned long long)p0, (unsigned long long)candidates.load(), now() - tg);

  // permutations of the points of L under tau_l; relation checks at every point
  auto build_perms = [&](ModP& M, vector<vector<uint32_t>>& perm, string& fail) -> bool {
    perm.assign(C.naxes, {});
    for (size_t l = 0; l < C.naxes; ++l) {
      vector<uint32_t> pr = {0, 1};
      for (int j = 0; j < C.t; ++j) {
        const Uint64 P = 2 * C.N[j], pj = C.deg[j];
        vector<uint32_t> nx(P * pj);
        vector<Uint64> A;
        vector<vector<Uint64>> At;
        vector<Uint64> dl;
        if (pj == 2 && C.act[j][l] != SIZE_MAX) A = M.transform(C.act[j][l]);
        if (pj != 2 && !C.actodd[j][l].empty()) {
          for (size_t id : C.actodd[j][l]) At.push_back(M.transform(id));
          dl = M.transform(C.actdelta[j]);
        }
        for (Uint64 psi = 0; psi < P; ++psi) {
          const Uint64 img = pr[psi];
          Uint64 idl = 0;
          if (!dl.empty()) {
            if (dl[psi] == 0) {
              fail = "actdelta vanishes at a point";
              return false;
            }
            idl = M.inv(dl[psi]);
          }
          for (Uint64 rho = 0; rho < pj; ++rho) {
            const Uint64 r = M.R[j][psi + P * rho];
            Uint64 tv = r;
            if (!A.empty())
              tv = M.mul(A[psi], M.mul(2, M.B[j][1][psi + P * rho]));  // A / r
            else if (!At.empty()) {
              Uint64 s = 0;
              for (Uint64 k = pj; k-- > 0;) s = M.add(M.mul(s, r), At[k][psi]);
              tv = M.mul(s, idl);
            }
            Uint64 hit = UINT64_MAX;
            for (Uint64 rr = 0; rr < pj; ++rr)
              if (M.R[j][img + P * rr] == tv) {
                hit = rr;
                break;
              }
            if (hit == UINT64_MAX) {
              fail = "action record " + std::to_string(l) + " at level " + std::to_string(j) +
                     " is not a root mod p (relation fails)";
              return false;
            }
            nx[psi + P * rho] = (uint32_t)(img + P * hit);
          }
        }
        pr.swap(nx);
      }
      perm[l].swap(pr);
    }
    return true;
  };
  vector<vector<uint32_t>> perm0;
  {
    string fail;
    if (!build_perms(M0, perm0, fail)) refuse(fail);
    const Uint64 NP = 2 * C.D;
    for (size_t l = 0; l < C.naxes; ++l)
      for (size_t m = l + 1; m < C.naxes; ++m)
        for (Uint64 x = 0; x < NP; ++x)
          if (perm0[l][perm0[m][x]] != perm0[m][perm0[l][x]])
            refuse("action records do not commute");
    for (size_t l = 0; l < C.naxes; ++l) {
      vector<uint32_t> cur(NP);
      for (Uint64 x = 0; x < NP; ++x) cur[x] = (uint32_t)x;
      for (Uint64 k = 0; k < C.smith[l]; ++k) {
        for (Uint64 x = 0; x < NP; ++x) cur[x] = perm0[l][cur[x]];
        if (k + 1 < C.smith[l]) {
          bool id = true;
          for (Uint64 x = 0; x < NP && id; ++x) id = cur[x] == x;
          if (id) refuse("tau_l has order smaller than its Smith factor");
        }
      }
      for (Uint64 x = 0; x < NP; ++x)
        if (cur[x] != x) refuse("tau_l^{n_l} != id");
    }
  }
  // word -> point index from base point kappa (0/1)
  auto make_idx = [&](const vector<vector<uint32_t>>& perm, int kappa, vector<uint32_t>& idx) {
    idx.assign(C.D, 0);
    idx[0] = (uint32_t)kappa;
    for (Uint64 c = 1; c < C.D; ++c) {
      size_t l = 0;
      while ((c / C.Ms[l]) % C.smith[l] == 0) ++l;
      idx[c] = perm[l][idx[c - C.Ms[l]]];
    }
    vector<char> seen(2 * C.D, 0);
    for (Uint64 c = 0; c < C.D; ++c) {
      const uint32_t x = idx[c];
      if ((x & 1u) != (uint32_t)kappa || seen[x]) return false;
      seen[x] = 1;
    }
    return true;
  };
  {
    vector<uint32_t> i0, i1;
    if (!make_idx(perm0, 0, i0) || !make_idx(perm0, 1, i1))
      refuse("Gamma does not act simply transitively on the points over a place of K");
    say("(G) at p0: %zu action records give commuting permutations of the %llu points with "
        "orders = Smith factors; the word map is a bijection onto the points over each place of "
        "K: L/K Galois with group (+) Z/n_l (decomposition groups trivial at split primes)\n",
        C.naxes, (unsigned long long)(2 * C.D));
  }

  // ---- (C) archimedean phase: all embeddings, bounds, conjugation
  const double ta = now();
  Uint64 maxbits = 0;
  for (auto& e : C.in.el) maxbits = std::max<Uint64>(maxbits, e.max_bits);
  slong prec = prec0 > 0 ? prec0 : (slong)maxbits + 192;
  std::unique_ptr<Arch> A;
  for (int attempt = 0;; ++attempt, prec *= 2) {
    A = std::make_unique<Arch>(C, prec);
    if (A->build()) break;
    if (attempt >= 3) refuse("root isolation at all embeddings failed");
  }
  say("(C) all %lld embeddings at %ld bits: roots isolated (%.2f s, RSS %.2f GB)\n", (long long)n,
      (long)prec, now() - ta, rss_gb());
  // element bounds
  vector<slong> Bel(C.in.el.size(), kNegLog);
  for (size_t id = 0; id < C.in.el.size(); ++id) {
    AcbVec v = A->transform(id);
    bool fin;
    Arch::stats(v, Bel[id], fin);
    if (!fin) refuse("nonfinite ball at an embedding");
  }
  // base embedding iota (branch rectangles) and conj o iota, as point indices
  vector<vector<uint32_t>> permC(C.naxes);
  uint32_t iota_pt = 0, conj_pt = 0;
  {
    Uint64 psi_i = 0, psi_c = 0;  // kappa 0 = physical embedding of K; K real so conj fixes it
    for (int j = 0; j < C.t; ++j) {
      const Uint64 P = 2 * C.N[j], pj = C.deg[j];
      sicx::A re, im;
      parse_arbdump(re, C.in.branches[j][0]);
      parse_arbdump(im, C.in.branches[j][1]);
      B rect;
      acb_set_arb_arb(rect, re, im);
      Uint64 hit = UINT64_MAX;
      int nh = 0;
      for (Uint64 rho = 0; rho < pj; ++rho)
        if (acb_overlaps(A->R[j][psi_i + P * rho], rect)) {
          ++nh;
          hit = rho;
        }
      if (nh != 1) refuse("branch rectangle does not isolate a root at level " + std::to_string(j));
      B cr;
      acb_conj(cr, A->R[j][psi_i + P * hit]);
      Uint64 hc = UINT64_MAX;
      int nc = 0;
      for (Uint64 rho = 0; rho < pj; ++rho)
        if (acb_overlaps(A->R[j][psi_c + P * rho], cr)) {
          ++nc;
          hc = rho;
        }
      if (nc != 1) refuse("conj(iota(g_j)) not isolated at level " + std::to_string(j));
      psi_i += P * hit;
      psi_c += P * hc;
    }
    iota_pt = (uint32_t)psi_i;
    conj_pt = (uint32_t)psi_c;
    // complex point permutations (certified by unique overlap with isolating balls)
    for (size_t l = 0; l < C.naxes; ++l) {
      vector<uint32_t> pr = {0, 1};
      for (int j = 0; j < C.t; ++j) {
        const Uint64 P = 2 * C.N[j], pj = C.deg[j];
        vector<uint32_t> nx(P * pj);
        AcbVec Av;
        vector<AcbVec> At;
        AcbVec dl;
        if (pj == 2 && C.act[j][l] != SIZE_MAX) Av = A->transform(C.act[j][l]);
        if (pj != 2 && !C.actodd[j][l].empty()) {
          for (size_t id : C.actodd[j][l]) At.push_back(A->transform(id));
          dl = A->transform(C.actdelta[j]);
        }
        std::atomic<bool> ok{true};
        parallel_for(P, [&](size_t lo, size_t hi) {
          acb_t tv, s;
          acb_init(tv);
          acb_init(s);
          for (size_t psi = lo; psi < hi && ok; ++psi) {
            const Uint64 img = pr[psi];
            if (!At.empty() && acb_contains_zero(dl[psi])) {
              ok = false;
              break;
            }
            for (Uint64 rho = 0; rho < pj; ++rho) {
              acb_srcptr r = A->R[j][psi + P * rho];
              if (Av.n) {
                acb_div(tv, Av[psi], r, A->prec);
              } else if (!At.empty()) {
                acb_zero(s);
                for (Uint64 k = pj; k-- > 0;) {
                  acb_mul(s, s, r, A->prec);
                  acb_add(s, s, At[k][psi], A->prec);
                }
                acb_div(tv, s, dl[psi], A->prec);
              } else
                acb_set(tv, r);
              int nh = 0;
              Uint64 hit = 0;
              for (Uint64 rr = 0; rr < pj; ++rr)
                if (acb_overlaps(A->R[j][img + P * rr], tv)) {
                  ++nh;
                  hit = rr;
                }
              if (nh != 1) {
                ok = false;
                break;
              }
              nx[psi + P * rho] = (uint32_t)(img + P * hit);
            }
          }
          acb_clear(tv);
          acb_clear(s);
        });
        if (!ok) refuse("complex action transport not isolated (precision)");
        pr.swap(nx);
      }
      permC[l].swap(pr);
    }
    vector<uint32_t> idxC(C.D);
    idxC[0] = iota_pt;
    for (Uint64 c = 1; c < C.D; ++c) {
      size_t l = 0;
      while ((c / C.Ms[l]) % C.smith[l] == 0) ++l;
      idxC[c] = permC[l][idxC[c - C.Ms[l]]];
    }
    Uint64 cs = UINT64_MAX;
    int ncs = 0;
    for (Uint64 c = 0; c < C.D; ++c)
      if (idxC[c] == conj_pt) {
        cs = c;
        ++ncs;
      }
    if (ncs != 1) refuse("complex conjugation is not a unique Galois word");
    C.cstar = C.digits(cs);
    if (inject == "conj") C.cstar.assign(C.naxes, 0);
    for (size_t l = 0; l < C.naxes; ++l)
      if ((2 * C.cstar[l]) % C.smith[l]) refuse("conjugation word not an involution");
  }
  say("(C) complex conjugation: iota o sigma_c* = conj o iota with c* =");
  for (auto w : C.cstar) say(" %llu", (unsigned long long)w);
  say(" (certified isolation on every generator; 2 c* = 0) (%.2f s total archimedean, RSS %.2f "
      "GB)\n",
      now() - ta, rss_gb());

  // ---- bounds (lemma D) and checks
  Bounds BD;
  BD.n = n;
  BD.TB0.assign(C.t, 0);
  BD.TBmax.assign(C.t, 0);
  BD.quad_before.assign(C.t + 1, 0);
  for (int j = 0; j < C.t; ++j) BD.quad_before[j + 1] = BD.quad_before[j] + (C.deg[j] == 2);
  vector<Int64> TA(C.t + 1,
                   0);  // generic bound for all leaf-integral elements of F_j (no scale credit)
  // Lemma I (README): prove that y = 2^{e2} x (x an element of F_j with integral leaves) is an
  // algebraic integer. Its characteristic polynomial over Q has coefficients e_k with
  // N_{F_j/Q}(a) e_k in Z (a = denominator bound of x, log2 N_{L/Q}(a O_L) <= TL) and |e_k| <=
  // 2^{n(1+B)}. Stage 1 reconstructs the integer candidate P~ (CRT with M > 2A); stage 2 checks
  // P~ = charpoly mod further primes until sum log2 p > log2(2A) + log2 N(a): then e_k = P~_k. A
  // mismatch proves that y is not integral. Returns 1 integral, 0 not integral.
  vector<Uint64> Jh = C.in.integral_shift_hints();
  if (no_hints || Jh.size() != (size_t)C.t) Jh.assign(C.t, 0);
  std::mutex ipmu;
  std::set<Uint64> iprimes;
  auto prove_integral = [&](int j, size_t id, Int64 e2, Int64 TL, string& note) -> int {
    const Uint64 nj = 2 * C.N[j];
    const Int64 By = std::max<Int64>(0, Bel[id] + e2);
    const Int64 log2A = (Int64)nj * (1 + By) + 1;  // = log2(2A), A = 2^{n(1+B)} >= |e_k|
    const Int64 Tj = std::max<Int64>(0, cdiv(TL, (Int64)(C.D / C.N[j])));
    const Int64 k1 = cdiv(log2A + 2, 61), k2 = std::max<Int64>(k1, cdiv(log2A + 2 + Tj + 1, 61));
    vector<Uint64> P(k2);
    vector<vector<Uint64>> res1(k1);
    std::atomic<Int64> nxt{0};
    std::atomic<bool> bad{false};
    std::unique_ptr<fmpz_poly_struct, void (*)(fmpz_poly_struct*)> Pt(new fmpz_poly_struct,
                                                                      [](fmpz_poly_struct* q) {
                                                                        fmpz_poly_clear(q);
                                                                        delete q;
                                                                      });
    fmpz_poly_init(Pt.get());
    auto charpoly = [&](ModP& M) {
      vector<Uint64> v = M.transform(id);
      const Uint64 s = n_powmod2_ui_preinv(2, (Uint64)e2, M.p, M.mod.ninv);
      for (auto& x : v) x = M.mul(x, s);
      vector<Uint64> cp(nj + 1);
      _nmod_poly_product_roots_nmod_vec(cp.data(), v.data(), (slong)nj, M.mod);
      return cp;
    };
    auto gen = [&](unsigned wid, int stage) {
      ThreadCleanup cleanup;
      std::mt19937_64 rng(seed * 1299709ull + (Uint64)j * 7777ull + (Uint64)id * 31ull +
                          wid * 1000003ull + (Uint64)stage);
      ModP M(C);
      for (;;) {
        const Int64 i = nxt++;
        if (i >= (stage == 1 ? k1 : k2) || bad) return;
        if (stage == 2 && i < k1) continue;
        Uint64 p;
        for (;;) {
          p = (1ull << 61) + (rng() % (1ull << 61));
          if (!n_is_prime(p) || try_prime(M, p, Wit, false, j) != j) continue;
          std::lock_guard<std::mutex> g(ipmu);
          if (iprimes.insert(p).second) break;
        }
        P[i] = p;
        vector<Uint64> cp = charpoly(M);
        if (stage == 1)
          res1[i] = std::move(cp);
        else {
          nmod_poly_t r;
          nmod_poly_init(r, p);
          fmpz_poly_get_nmod_poly(r, Pt.get());
          bool eq = nmod_poly_length(r) == (slong)(nj + 1);
          for (Uint64 k = 0; k <= nj && eq; ++k) eq = nmod_poly_get_coeff_ui(r, k) == cp[k];
          nmod_poly_clear(r);
          if (!eq) bad = true;
        }
      }
    };
    auto run = [&](int stage) {
      nxt = 0;
      vector<std::thread> th;
      for (unsigned w = 0; w < g_threads; ++w) th.emplace_back(gen, w, stage);
      for (auto& x : th) x.join();
    };
    iprimes.clear();
    run(1);
    {  // CRT of the stage-1 residues (symmetric representatives)
      struct Residues {
        vector<fmpz> mods, in;
        fmpz_multi_CRT_t plan;
        explicit Residues(size_t count) : mods(count), in(count) {
          for (size_t i = 0; i < count; ++i) {
            fmpz_init(&mods[i]);
            fmpz_init(&in[i]);
          }
          fmpz_multi_CRT_init(plan);
        }
        ~Residues() {
          fmpz_multi_CRT_clear(plan);
          for (size_t i = 0; i < mods.size(); ++i) {
            fmpz_clear(&mods[i]);
            fmpz_clear(&in[i]);
          }
        }
        Residues(const Residues&) = delete;
        Residues& operator=(const Residues&) = delete;
      } residues(k1);
      auto& mods = residues.mods;
      auto& in = residues.in;
      for (Int64 i = 0; i < k1; ++i) {
        fmpz_set_ui(&mods[i], P[i]);
      }
      auto* CR = residues.plan;
      if (!fmpz_multi_CRT_precompute(CR, mods.data(), k1)) refuse("CRT precompute failed");
      fmpz_t out;
      fmpz_init(out);
      for (Uint64 k = 0; k <= nj; ++k) {
        for (Int64 i = 0; i < k1; ++i) fmpz_set_ui(&in[i], res1[i][k]);
        fmpz_multi_CRT_precomp(out, CR, in.data(), 1);
        if ((Int64)fmpz_bits(out) > log2A - 1)
          bad = true;  // an integral charpoly would have |e_k| <= A = 2^{log2A-1}
        fmpz_poly_set_coeff_fmpz(Pt.get(), k, out);
      }
      fmpz_clear(out);

      res1.clear();
    }
    if (!bad) run(2);
    note = std::to_string(k2) + " primes (" + std::to_string(k1) + " reconstruction + " +
           std::to_string(k2 - k1) + " verification), log2 A = " + std::to_string(log2A) +
           ", log2 N(den) <= " + std::to_string(Tj);
    return bad ? 0 : 1;
  };
  vector<int> integral(C.t, 0);
  const double tint = now();
  for (int j = 0; j < C.t; ++j) {
    const Int64 Lmult = (Int64)(C.D / C.N[j]);  // [L:F_j]
    const bool try_int = 2 * (Int64)C.N[j] <= int_max_dim;
    string note;
    const double tj = now();
    if (C.deg[j] == 2) {
      Int64 Tabs;
      if (__builtin_mul_overflow((Int64)A->Tc[j], Lmult, &Tabs))
        refuse("bound overflow");  // log2 |N_{L/Q}(c_j)| upper
      const Int64 Tc_el = BD.elem(C, C.wsq[j]);
      const Int64 Tnum_c = std::max<Int64>(0, Tabs + Tc_el);
      BD.TB0[j] = n;                      // b_{j,0} = 1/2
      BD.TBmax[j] = n + cdiv(Tnum_c, 2);  // b_{j,1} = 1/(2 w_j): (2) * num(w_j)
      if (try_int) {
        // J_j: EXMT hint (or 0); if 2^J g_j is proved non-integral, retry with J+1 .. J+4 (Jh[j]
        // is updated)
        for (int tries = 0; tries < 5; ++tries) {
          string nt;
          integral[j] = prove_integral(j, C.wsq[j], 2 * (Int64)Jh[j], Tc_el, nt) ? 1 : -1;
          note += nt;
          if (integral[j] == 1) break;
          note += " [J=" + std::to_string(Jh[j]) + " not integral]; ";
          ++Jh[j];
        }
        // G_j = 2^{J_j} g_j integral: b_{j,1} = 2^{J_j}/(2 G_j) is bounded by (2 G_j), log2
        // N((2G_j)) = n + (log2|N(c_j)| + 2 J_j n)/2
        if (integral[j] == 1)
          BD.TBmax[j] =
              std::min(BD.TBmax[j], n + cdiv(std::max<Int64>(0, Tabs + 2 * (Int64)Jh[j] * n), 2));
      }
    } else {
      const Int64 pj = (Int64)C.deg[j];
      Int64 Tf;
      if (__builtin_mul_overflow((Int64)A->Tfp[j], (Int64)(C.D / C.N[j + 1]), &Tf))
        refuse("bound overflow");  // log2 |N_{L/Q}(f_j'(g_j))| upper
      const Int64 Tnum_f = std::max<Int64>(0, Tf + pj * TA[j]);
      BD.TB0[j] = BD.TBmax[j] = pj * TA[j] + Tnum_f;  // q_0 bound (p TA) and num(f')
      if (try_int) {
        int ok = 1;
        string nk;
        for (Int64 k = 0; k < pj && ok == 1; ++k)
          ok = prove_integral(j, C.fcoef[j][k], (pj - k) * (Int64)Jh[j], BD.elem(C, C.fcoef[j][k]),
                              nk),
          note += nk + "; ";
        integral[j] = ok ? 1 : -1;
        // G_j = 2^{J_j} g_j root of a monic integral polynomial: b_{j,k} = 2^{J_j k}
        // q^G_k(G_j)/f^G'(G_j), q^G integral
        if (integral[j] == 1)
          BD.TB0[j] = BD.TBmax[j] =
              std::min(BD.TBmax[j], std::max<Int64>(0, Tf + (Int64)Jh[j] * (pj - 1) * n));
      }
    }
    TA[j + 1] = TA[j] + BD.TBmax[j];
    if (try_int)
      say("  integral structure level %d: G_%d = 2^%llu g_%d %s (%s; %.1f s)\n", j, j,
          (unsigned long long)Jh[j], j,
          integral[j] == 1
              ? "PROVED to be an algebraic integer"
              : (C.deg[j] == 2 ? "not proved: 4^J c_j proved NOT integral"
                               : "not proved: a scaled coefficient proved NOT integral"),
          note.c_str(), now() - tj);
  }
  say("integral structure (Lemma I): %.1f s; levels proved integral:", now() - tint);
  for (int j = 0; j < C.t; ++j)
    if (integral[j] == 1) say(" %d", j);
  say(" (others: Lemma D without integrality)\n");
  vector<Check> checks;
  auto Tel = [&](size_t id) { return BD.elem(C, id); };
  for (int j = 0; j < C.t; ++j)
    for (size_t l = 0; l < C.naxes; ++l) {
      Check ck;
      ck.what = "action relation level " + std::to_string(j) + " axis " + std::to_string(l);
      if (C.deg[j] == 2) {
        const size_t c = C.wsq[j];
        if (C.act[j][l] != SIZE_MAX) {
          const size_t a = C.act[j][l];
          ck.B = log2sum(2 * Bel[a], 2 * Bel[c]);
          ck.T = 2 * Tel(a) + 2 * Tel(c);
        } else {
          ck.B = Bel[c] + 1;
          ck.T = 2 * Tel(c);
        }
      } else {
        const Int64 pj = (Int64)C.deg[j];
        if (!C.actodd[j][l].empty()) {
          const Int64 Bg = A->Bg[j], Tg = TA[j];
          Int64 BY = kNegLog, TY = 0;
          for (Int64 k = 0; k < pj; ++k) {
            BY = log2sum(BY, Bel[C.actodd[j][l][k]] + k * Bg) + 0;
            TY += Tel(C.actodd[j][l][k]) + k * Tg;
          }
          BY += 3;  // sum of p <= 31 terms: + log2(32) - 1 margin above the pairwise rule
          const Int64 Bdl = Bel[C.actdelta[j]], Tdl = Tel(C.actdelta[j]);
          Int64 BE = pj * BY, TE = pj * TY;
          for (Int64 k = 0; k < pj; ++k) {
            BE = log2sum(BE, Bel[C.fcoef[j][k]] + k * BY + (pj - k) * Bdl);
            TE += Tel(C.fcoef[j][k]) + k * TY + (pj - k) * Tdl;
          }
          ck.B = BE + 5;
          ck.T = TE;
        } else {
          for (Int64 k = 0; k < pj; ++k) {
            ck.B = std::max<Int64>(ck.B, Bel[C.fcoef[j][k]] + 1);
            ck.T = std::max<Int64>(ck.T, 2 * Tel(C.fcoef[j][k]));
          }
        }
      }
      checks.push_back(ck);
    }
  Int64 hmax = kNegLog, Tsum = 0;
  for (size_t r = 0; r < C.repid.size(); ++r) {
    hmax = std::max<Int64>(hmax, Bel[C.repid[r]]);
    checks.push_back({"stabilizer of eta_" + std::to_string(C.reppos[r]), Bel[C.repid[r]] + 1,
                      2 * Tel(C.repid[r])});
  }
  for (Uint64 m = 0; m < C.d; ++m) Tsum += Tel(C.repid[C.rep_of_m[m]]);
  {
    Int64 lg = 0;
    while ((1ll << lg) < (Int64)(3 * C.d * C.d + C.d)) ++lg;
    checks.push_back({"residuals R_{a,k}", lg + 4 * hmax, 4 * Tsum});
  }
  Int64 need = 0;
  string worst;
  for (auto& ck : checks) {
    const Int64 b = ck.B + cdiv(ck.T, n) + 1;
    if (b > need) {
      need = b;
      worst = ck.what;
    }
  }
  say("bounds (lemma D, totals over L divided by n = %lld):\n", (long long)n);
  for (int j = 0; j < C.t; ++j)
    say("  level %d (p=%llu): log2|N(c or f')|/n <= %.2f, dual bound digit0 %.2f, any digit %.2f "
        "bits per conjugate\n",
        j, (unsigned long long)C.deg[j],
        (C.deg[j] == 2 ? (double)A->Tc[j] / (double)(2 * C.N[j])
                       : (double)A->Tfp[j] / (double)(2 * C.N[j + 1])),
        (double)BD.TB0[j] / n, (double)BD.TBmax[j] / n);
  for (size_t r = 0; r < C.repid.size(); ++r)
    say("  eta_%lld: h <= %ld, denominator bound %.2f bits per conjugate (scale 2-adic credit "
        "%ld)\n",
        (long long)C.reppos[r], (long)Bel[C.repid[r]], (double)Tel(C.repid[r]) / n,
        (long)C.co[C.repid[r]]->v2);
  say("  residual: B = %lld (h = %lld), T/n = %.1f bits; required sum log2 p_i > %lld (worst: "
      "%s)\n",
      (long long)checks.back().B, (long long)hmax, (double)checks.back().T / n, (long long)need,
      worst.c_str());
  Int64 kprimes = cdiv(need, 61);
  if (force_primes >= 0) kprimes = force_primes;
  say("certificate primes needed: %lld (each > 2^61)%s\n", (long long)kprimes,
      force_primes >= 0 ? " [overridden by --primes: diagnostic run, NOT a certificate]" : "");
  if (bound_only) {
    say("bound-only mode: split-prime structure checks and bound reconstruction completed; "
        "residual certificate primes skipped; total %.2f s, peak RSS %.2f GB\n",
        now() - t0, rss_gb());
    return 0;
  }

  // ---- coset representatives of W = {word(u)} and stabilizer generators
  vector<Uint64> Wset;
  {
    std::set<Uint64> s;
    for (Uint64 u = 1; u < C.d; u += 2) s.insert(C.lin(C.word_of_odd(u)));
    Wset.assign(s.begin(), s.end());
  }
  vector<Uint64> reps;
  {
    vector<char> mark(C.D, 0);
    for (Uint64 c = 0; c < C.D; ++c)
      if (!mark[c]) {
        reps.push_back(c);
        auto dc = C.digits(c);
        (void)dc;
        for (Uint64 w : Wset) mark[C.addw(c, C.digits(w))] = 1;
      }
  }
  // stabilizer generators: (rep index, word)
  vector<std::pair<size_t, vector<Uint64>>> stabs;
  {
    int s = 0;
    while ((1ull << s) < C.d) ++s;
    for (size_t r = 0; r < C.reppos.size(); ++r) {
      const Int64 pos = C.reppos[r];
      int v = pos == 0 ? s : 0;
      if (pos)
        while ((1ll << v) < pos) ++v;
      if (pos == 0 || v == s - 1) {
        stabs.push_back({r, C.word_of_odd(5)});
        stabs.push_back({r, C.word_of_odd(C.d - 1)});
      } else if (v >= 1 && s - v >= 2) {
        Uint64 u = 1;
        for (int i = 0; i < (1 << (s - v - 2)); ++i) u = u * 5 % C.d;
        stabs.push_back({r, C.word_of_odd(u)});
      }
    }
  }
  say("orbit reduction: |W| = %zu, %zu coset representatives x 2 places of K; %zu stabilizer "
      "identities\n",
      Wset.size(), reps.size(), stabs.size());

  // ---- per-prime certificate
  struct PrimeResult {
    Uint64 p = 0;
    bool ok = false;
    string fail;
    double sec = 0, search_sec = 0;
    std::array<double, 4> phases{};
    bool nonzero = false;
  };
  vector<PrimeResult> results(kprimes);
  std::atomic<Int64> next{0};
  std::mutex outmu;
  const double tc = now();
  auto certify_prime = [&](ModP& M, PrimeResult& res) {
    const double ts = now();
    vector<vector<uint32_t>> perm;
    if (!build_perms(M, perm, res.fail)) return;
    vector<uint32_t> idx[2];
    if (!make_idx(perm, 0, idx[0]) || !make_idx(perm, 1, idx[1])) {
      res.fail = "word map not bijective";
      return;
    }
    const double t_perm = now();
    vector<vector<Uint64>> val(C.repid.size());
    for (size_t r = 0; r < C.repid.size(); ++r) val[r] = M.transform(C.repid[r]);
    for (auto& v : val)
      for (Uint64 x : v)
        if (x) {
          res.nonzero = true;
          break;
        }
    const double t_val = now();
    // stabilizers
    for (auto& [r, w] : stabs)
      for (int kap = 0; kap < 2; ++kap)
        for (Uint64 c = 0; c < C.D; ++c) {
          const Uint64 c2 = C.addw(c, w);
          if (val[r][idx[kap][c]] != val[r][idx[kap][c2]]) {
            res.fail = "stabilizer identity fails for eta_" + std::to_string(C.reppos[r]);
            return;
          }
        }
    const double t_stab = now();
    // residual tables at the coset representatives (both places of K). For each a, the k-row of R
    // vanishes iff its DFT does (d | p - 1, zeta primitive): sum_k R_{a,k} zeta^{kl}
    //   = (d+1) U_a(-l) zeta^{-al} U_{-a}(l) - N^2 (1 + d [a = 0][l = 0]),  U_a(l) = sum_j V_j
    //   W_{j+a} zeta^{jl}.
    // Rows 0 <= a <= d/2 suffice: S_{a,k} = S_{k,a} = S_{-a,-k} cover the other pairs at the same
    // point.
    NTT T;
    T.init(M.p, C.d);
    const Uint64 d = C.d;
    vector<Uint64> V(d), W(d), U(d * d);
    for (int kap = 0; kap < 2; ++kap)
      for (Uint64 c : reps) {
        const Uint64 ccs = C.addw(c, C.cstar);
        for (Uint64 m = 0; m < d; ++m) {
          const size_t r = C.rep_of_m[m];
          V[m] = val[r][idx[kap][C.addw(c, C.wordm[m])]];
          W[m] = val[r][idx[kap][C.addw(ccs, C.wordm[m])]];
        }
        long bad_a = -1, bad_l = -1;
        if (!residual_rows_vanish(T, V, W, U, bad_a, bad_l)) {
          res.fail = "residual row a=" + std::to_string(bad_a) + " (Fourier index " +
                     std::to_string(bad_l) + ") != 0 mod p";
          return;
        }
      }
    res.phases = {t_perm - ts, t_val - t_perm, t_stab - t_val, now() - t_stab};
    res.ok = true;
    res.sec = now() - ts;
  };
  // self-test of the Fourier-domain residual identity against the definition of R_{a,k} (random
  // data)
  {
    NTT T;
    T.init(p0, C.d);
    std::mt19937_64 r(7);
    const Uint64 d = C.d, msk = d - 1;
    vector<Uint64> V(d), W(d), U(d * d);
    for (Uint64 j = 0; j < d; ++j) {
      V[j] = r() % p0;
      W[j] = r() % p0;
    }
    vector<Uint64> F;
    residual_fourier(T, V, W, U, F);
    Uint64 Nn = 0;
    for (Uint64 j = 0; j < d; ++j) Nn = M0.add(Nn, M0.mul(V[j], W[j]));
    const Uint64 N2 = M0.mul(Nn, Nn);
    for (Uint64 a = 0; a <= d / 2; a += std::max<Uint64>(1, d / 8))
      for (Uint64 l = 0; l < d; l += std::max<Uint64>(1, d / 8)) {
        Uint64 s = 0;
        for (Uint64 k = 0; k < d; ++k) {
          Uint64 S = 0;
          for (Uint64 j = 0; j < d; ++j)
            S = M0.add(S, M0.mul(M0.mul(V[j], V[(j + a + k) & msk]),
                                 M0.mul(W[(j + a) & msk], W[(j + k) & msk])));
          Uint64 Rk = M0.mul((d + 1) % p0, S);
          Rk = M0.sub(Rk, M0.mul(N2, (a == 0) + (k == 0)));
          s = M0.add(s, M0.mul(Rk, T.w[(k * l) & msk]));
        }
        if (s != F[a * d + l]) refuse("residual Fourier self-test failed");
      }
  }
  std::mutex primes_mu;
  std::set<Uint64> used;
  auto worker = [&](unsigned wid) {
    ThreadCleanup cleanup;
    std::mt19937_64 rng(seed * 7919ull + wid * 104729ull + 17);
    ModP M(C);
    for (;;) {
      const Int64 i = next++;
      if (i >= kprimes) return;
      const double tq = now();
      Uint64 p;
      if (i == 0) {
        p = p0;
        bool ok0 = M.init(p0);
        for (int j = 0; j < C.t && ok0; ++j) ok0 = M.build_level(j) == 1;
        if (!ok0) {
          results[i].p = p0;
          results[i].fail = "p0 does not split (internal error)";
          continue;
        }
      } else {
        for (;;) {
          p = search(rng, M);
          std::lock_guard<std::mutex> g(primes_mu);
          if (p != p0 && used.insert(p).second) break;
        }
      }
      results[i].p = p;
      if (i > 0) results[i].search_sec = now() - tq;
      certify_prime(M, results[i]);
      std::lock_guard<std::mutex> g(outmu);
      if (!results[i].ok)
        say("  prime %lld p=%llu FAILED: %s\n", (long long)i, (unsigned long long)p,
            results[i].fail.c_str());
      else if (i < 4 || i % 100 == 0)
        say("  prime %lld p=%llu ok (search %.2f s; checks %.2f s = perms %.2f + values %.2f + "
            "stabilizers %.2f + residuals %.2f)\n",
            (long long)i, (unsigned long long)p, results[i].search_sec, results[i].sec,
            results[i].phases[0], results[i].phases[1], results[i].phases[2], results[i].phases[3]);
    }
  };
  {
    const unsigned T = g_threads;
    const unsigned save = g_threads;
    g_threads = 1;  // inner loops serial; parallel over primes
    vector<std::thread> th;
    for (unsigned w = 0; w < std::min<Int64>(T, std::max<Int64>(kprimes, 1)); ++w)
      th.emplace_back(worker, w);
    for (auto& x : th) x.join();
    g_threads = save;
  }
  // ---- witnesses for (W): keep searching until every level has one
  {
    std::mt19937_64 rng(seed * 31337ull);
    ModP M(C);
    Uint64 extra = 0;
    auto missing = [&] {
      for (int j = 0; j < C.t; ++j)
        if (!Wit.prime[j]) return true;
      return false;
    };
    while (missing() && extra < 50000000ull) {
      Uint64 p = (1ull << 61) + (rng() % (1ull << 61));
      if (!n_is_prime(p)) continue;
      ++extra;
      try_prime(M, p, Wit, true);
      if (extra % 1000000 == 0) {
        say("  witness search: %llu candidates; missing levels:", (unsigned long long)extra);
        for (int j = 0; j < C.t; ++j)
          if (!Wit.prime[j]) say(" %d", j);
        say("\n");
      }
    }
    if (missing()) refuse("no irreducibility witness for some level (A may not be a field)");
  }
  bool all_ok = true, nonzero = false;
  long double logM = 0;
  std::set<Uint64> distinct;
  for (auto& r : results) {
    if (!r.ok) all_ok = false;
    nonzero |= r.nonzero;
    logM += std::log2((long double)r.p);
    distinct.insert(r.p);
  }
  if ((Int64)distinct.size() != kprimes) all_ok = false;
  say("(W) irreducibility witnesses (prime per level):");
  for (int j = 0; j < C.t; ++j) say(" %llu", (unsigned long long)Wit.prime[j]);
  say("\nsearch statistics (candidates reaching level j):");
  for (int j = 0; j < C.t; ++j) say(" %llu", (unsigned long long)g_reach[j].load());
  say("\nprimes: %lld distinct, sum log2 p = %.1f (required > %lld); candidates tested %llu; "
      "modular phase %.2f s\n",
      (long long)distinct.size(), (double)logM, (long long)need,
      (unsigned long long)candidates.load(), now() - tc);
  const bool enough = logM > (long double)need && force_primes < 0;
  if (!all_ok) {
    say("RESULT: FAILED (a modular check failed; see above)\n");
    return 1;
  }
  if (!nonzero) refuse("all representative values vanish (zero vector?)");
  if (!inject.empty()) {
    say("RESULT: NEGATIVE-CONTROL (fault injection %s was not detected)\n", inject.c_str());
    return 4;
  }
  if (C.in.whole_file_sha() != C.in.file_sha) refuse("input file changed during the run");
  const char* status = "PROVED";
  if (!enough) {
    say("RESULT: INCOMPLETE (modular checks passed at %lld primes; --primes runs are "
        "diagnostics, not certificates)\n",
        (long long)kprimes);
    return 3;
  }
  for (size_t r = 0; r < C.in.rays.size(); ++r)
    say(
        "ray %s word %s: EXACT SIC [%s]\n", C.in.rays[r].label.c_str(),
        [&] {
          string s;
          for (auto w : C.in.rays[r].word) s += (s.empty() ? "" : ",") + std::to_string(w);
          return s;
        }()
            .c_str(),
        status);
  say("RESULT: EXACT SIC for every Galois word of the multiplet (%llu rays sigma_c, c in G) "
      "[%s]; total %.2f s, peak RSS %.2f GB\n",
      (unsigned long long)C.D, status, now() - t0, rss_gb());
  return 0;
}
}  // namespace sic_check::crt
