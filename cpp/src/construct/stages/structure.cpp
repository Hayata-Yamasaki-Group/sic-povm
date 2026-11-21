// SPDX-License-Identifier: AGPL-3.0-or-later
#include <flint/acb_dft.h>
#include <flint/acb_poly.h>
#include <malloc.h>
#include <sys/resource.h>
#include <sys/statvfs.h>

#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string_view>

#include "construct/components/components.hpp"
#include "construct/model/session_state.hpp"
#include "construct/runtime/resources/construction_model.hpp"
#include "construct/runtime/resources/resident_memory.hpp"
#include "construct/runtime/resources/table_memory.hpp"
#include "construct/runtime/session/field_values.inl.hpp"
#include "construct/stages/table/analytic_table.hpp"
#include "exact/analytic/cyclotomic_trace.hpp"
#include "exact/analytic/direct_eta.hpp"
#include "exact/analytic/product_candidate.hpp"
#include "exact/analytic/step_selection.hpp"
#include "exact/analytic/table.hpp"
#include "exact/arithmetic/maximal_order.hpp"
#include "exact/certification/bounds/height_bounds.hpp"
#include "exact/certification/reconstruction/ghost.hpp"
#include "exact/group/group.hpp"
#include "exact/group/group_checkpoint.hpp"
#include "exact/group/group_fourier.hpp"
#include "exact/runtime/newton_class_memory.hpp"
#include "exact/runtime/parallel.hpp"
#include "exact/storage/analytic_generator.hpp"
#include "exact/storage/completed_class_cache.hpp"
#include "exact/storage/ghost_class_backing.hpp"
#include "exact/storage/leaf_source.hpp"  // exact leaf spools (sic::exact::storage::Source, Private)
#include "exact/storage/leaf_store.hpp"
#include "exact/storage/table_checkpoint.hpp"
#include "exact/transform/fixed_field_tower_general.hpp"
#include "exact/transform/leaf_transform.hpp"
#include "exact/transform/quadratic_transform.hpp"
#include "exact/transform/relative_projection.hpp"
#include "exact/transform/representative_lattice.hpp"
#include "exact/transform/trace_band.hpp"
#include "sic/support/flint_raii.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/container/producer_writer.hpp"
#include "support/resources.hpp"
namespace sic::construct {
int Construction::structure() {  // ---------------- helpers that depend on the working precision
                                 // ----------------

  // Conjugate classes. Complex conjugation is an element cc of G (it fixes K and sqrt(eps), and L/K
  // is abelian), so by Galois equivariance T[cc(x)] = chi(cc) conj(T[x]) = conj(T[x]) for every
  // table point x. cc maps the class c to the class s c, s the narrow class of a principal ideal
  // with a generator of negative norm. Only one class of each pair {c, s c} is computed (the one
  // with the shorter cycle); the other ("derived") is filled by conjugation.
  cc_pre = -1;
  derived = std::vector<char>((*ar_).h(), 0);

  completed_precision = 0;

  // conjugate vector of F(y), y = (i eps + j)/d in the principal class: g -> chi_g T[g(0, i, j)]

  // Form just one support generator at a time. Root values use the same
  // precision and operations as retained generator vectors.

  // Pass-1 table with conjugate class pairs: the cheap class of each pair at precision P, the other
  // at a low precision only to identify cc (the unique g with nn = -1, chi = +1 and x_j(g) =
  // conj(x_j(1)) for all generators; see the identification below), after which the derived classes
  // are filled by conjugation at precision P.

  // ---------------- structure, decided at a first (low) precision ----------------
  first_pass_bits = initial_precision;

  inSupp = std::vector<char>(N, 0);
  inA = std::vector<char>(N, 0);
  B1 = 0;
  for (;;) {
    try {
      T = build_pass1(first_pass_bits);
    } catch (const TablePrecisionError& error) {
      sic::support::progressf("%s; no checkpoint published\n", error.what());
      if (prec_user > 0) {
        sic::support::progressf("analytic table needs more precision\n");
        throw sic::Error(sic::ErrorKind::precision, "analytic table needs more precision");
      }
      T.reset();
      first_pass_bits *= 2;
      if (first_pass_bits > WORD_MAX / 16)
        throw sic::Error(sic::ErrorKind::precision,
                         "coarse precision retry exceeds supported range");
      sic::support::progressf(
          "nonfinite analytic enclosures; retrying pass 1 at %ld bits before support transforms\n",
          (long)first_pass_bits);
      continue;
    }
    budget.require("support retry transforms", (*model_).support(first_pass_bits + 16));
    const slong wp = first_pass_bits + 16;
    if (T->cached_enclosures)
      sic::support::progressf(
          "table (pass 1): cached certified enclosures at %ld bits; no quadrature was rerun "
          "[%.1fs]\n",
          (long)T->prec, stamp());
    else {
      sic::support::progressf(
          "table (pass 1): %ld states x %ld points, %ld nodes in the quadratures at %ld bits   "
          "[%.1fs]\n",
          (long)T->nstates(), d * d, T->n_nodes_total, (long)first_pass_bits + 16, stamp());
      sic::support::progressf(
          "quadrature kernel: maximum %ld nodes, workspace forecast %llu bytes\n",
          T->maximum_nodes.load(), (unsigned long long)T->kernel_peak.load());
    }
    const double ts0 = stamp();
    B1 = 0;
    {
      std::vector<double> b1c((*ar_).h(), 0);
      parallel_for((*ar_).h(), nth, [&](long c) {
        arb_t a;
        arb_init(a);
        const auto& values = T->compact() ? T->packed[c].v : T->val[c];
        for (const auto& value : values) {
          if (acb_is_zero(&value)) continue;
          acb_abs(a, &value, 64);
          arb_log(a, a, 64);
          b1c[c] = std::max(b1c[c], std::fabs(arf_get_d(arb_midref(a), ARF_RND_NEAR)));
        }
        arb_clear(a);
      });
      for (double v : b1c) B1 = std::max(B1, v);
    }
    const double ts1 = stamp();
    // Fourier transforms over G by axis-wise DFTs: fh(chi_m) = (1/N) sum_g conj(chi_m(g)) f(g)
    // one DFT precomputation per axis (shared, read-only), used by all lines of all vectors
    std::vector<acb_dft_pre_struct> pre((*group_).invf.size());
    for (size_t l = 0; l < (*group_).invf.size(); ++l)
      acb_dft_precomp_init(&pre[l], (*group_).invf[l], wp);
    auto dft = [&](sic::support::ComplexBallVector& f) {
      for (size_t l = 0; l < (*group_).invf.size(); ++l) {
        const long n = (*group_).invf[l], st = (*group_).stride[l];
        const long nlines = N / n, nchunk = std::min<long>(nlines, 16L * nth);
        parallel_for(nchunk, nth, [&](long ch) {
          acb_ptr in = _acb_vec_init(n), out = _acb_vec_init(n);
          for (long line = ch * nlines / nchunk; line < (ch + 1) * nlines / nchunk; ++line) {
            long lo = line % st, hi = line / st;
            long b0 = hi * st * n + lo;
            // Lines on one axis are disjoint. Gather the whole line before
            // overwriting it; the other lines retain their original inputs.
            // out_m = sum_g e(-m g / n) f_g
            for (long gg = 0; gg < n; ++gg) acb_set(in + gg, f[b0 + gg * st]);
            acb_dft_precomp(out, in, &pre[l], wp);
            for (long mm = 0; mm < n; ++mm) acb_swap(f[b0 + mm * st], out + mm);
          }
          _acb_vec_clear(in, n);
          _acb_vec_clear(out, n);
        });
      }
      parallel_for(N, nth, [&](long c) { acb_div_si(f[c], f[c], N, wp); });
    };
    // The support cutoff is the exact rational N^{-2}. Arb comparisons certify
    // each side; unresolved characters are retried unless the certified ones span.
    sic::support::flint::Arb support_cutoff;
    arb_one(support_cutoff);
    arb_div_si(support_cutoff, support_cutoff, N, wp);
    arb_div_si(support_cutoff, support_cutoff, N, wp);
    std::vector<std::vector<signed char>> support_state(m, std::vector<signed char>(N));
    double generator_seconds = 0;
    for (int j = 0; j < m; ++j) {
      const double tg = stamp();
      const sic::support::Index point = j < s ? (*group_).ghost_point(0, d >> (j + 1)) : 0;
      auto periods = sic::support::exact_axis_periods((*group_).invf, [&](size_t l, long p) {
        const long h = (*group_).pow((*group_).stride[l], p);
        if (j < s)
          return (*group_).chi[h] > 0 && (*group_).act(h, point) == (*group_).act(0, point);
        if (j == s) return (*group_).nn[h] == 1;
        return (*group_).chi[h] > 0;
      });
      const sic::support::PeriodicGrid grid((*group_).invf, std::move(periods));
      sic::support::ComplexBallVector xh = gen_vector(*T, j, wp, &grid);
      generator_seconds += stamp() - tg;
      rectangular_fourier(grid.periods, xh, wp, nth);
      if (producer_options.check_support_periods) {
        budget.require_additional(
            "reference support-period comparison",
            sic::support::resources::mul(N, sic::support::resources::complex_bytes(wp)));
        sic::support::ComplexBallVector reference = gen_vector(*T, j, wp);
        dft(reference);
        std::vector<char> allowed(N, 0);
        for (long q = 0; q < grid.count; ++q) {
          const long c = grid.character(q);
          allowed[c] = 1;
          if (!acb_overlaps(xh[q], reference[c]))
            throw sic::Error(sic::ErrorKind::construction, "reduced/full support spectra disagree");
        }
        for (long c = 0; c < N; ++c)
          if (!allowed[c] && !acb_contains_zero(reference[c]))
            throw sic::Error(sic::ErrorKind::construction,
                             "excluded full support frequency does not enclose zero");
        sic::support::progressf("support-period reference comparison passed for generator %d\n", j);
      }
      std::fill(support_state[j].begin(), support_state[j].end(),
                -1);  // excluded frequencies are exactly zero
      parallel_for(grid.count, nth, [&](long q) {
        arb_t a;
        arb_init(a);
        acb_abs(a, xh[q], wp);
        const long label = grid.character(q);
        support_state[j][label] =
            arb_gt(a, support_cutoff) ? 1 : (arb_lt(a, support_cutoff) ? -1 : 0);
        // Only literal finite nonzero coefficients transfer: do not import
        // the subsequent Galois-orbit completion into nonlinear support.

        arb_clear(a);
      });
      if (d >= 128 || producer_options.progress)
        sic::support::progressf(
            "support generator %d: rectangular quotient %ld of %ld elements [%.1fs]\n", j,
            grid.count, N, stamp());
    }
    for (auto& pr : pre) acb_dft_precomp_clear(&pr);
    const double ts2 = stamp();
    // Galois orbits {chi^b : b in (Z/o)^*} (o = order of chi) enumerated once; each orbit is tested
    // once, in parallel
    std::vector<std::vector<long>> units(Lc + 1);
    std::vector<sic::support::Index> orb(N, -1);
    std::vector<long> ostart{0}, omem;
    omem.reserve(N);
    for (long c = 0; c < N; ++c) {
      if (orb[c] >= 0) continue;
      const long o = (*group_).order(c);
      if (units[o].empty()) {
        for (long b = 1; b <= std::max(1L, o - 1); ++b)
          if (std::gcd(b, o) == 1) units[o].push_back(b);
      }
      const sic::support::Index id = sic::support::checked_index(ostart.size() - 1);
      for (long b : units[o]) {
        const long cb = (*group_).pow(c, b);
        if (orb[cb] < 0) {
          orb[cb] = id;
          omem.push_back(cb);
        }
      }
      ostart.push_back((long)omem.size());
    }
    std::atomic<bool> undecided_any(false);
    std::fill(inSupp.begin(), inSupp.end(), 0);
    parallel_for((long)ostart.size() - 1, nth, [&](long id) {
      bool insupp = false;
      for (int j = 0; j < m && !insupp; ++j) {
        bool in = false, undecided = false;
        for (long q = ostart[id]; q < ostart[id + 1] && !in; ++q) {
          const long cb = omem[q];
          if (support_state[j][cb] > 0)
            in = true;
          else if (support_state[j][cb] == 0)
            undecided = true;
        }
        if (in)
          insupp = true;
        else if (undecided)
          undecided_any = true;
      }
      if (insupp)
        for (long q = ostart[id]; q < ostart[id + 1]; ++q) inSupp[omem[q]] = 1;
    });
    sic::support::progressf(
        "  (B1 and generator vectors %.1fs, Fourier transforms and bounds %.1fs, support tests "
        "%.1fs)\n",
        ts1 - ts0 + generator_seconds, ts2 - ts1 - generator_seconds, stamp() - ts2);
    // A*^ = subgroup generated by the certified support characters (extend by one generator at a
    // time). Undecided characters do not matter once the certified ones generate the whole dual
    // group.
    inA.assign(N, 0);
    inA[0] = 1;
    {
      std::vector<long> Asub{0};
      for (long c = 0; c < N; ++c)
        if (inSupp[c] && !inA[c]) {
          std::vector<long> cur = Asub;  // Asub <- Asub <c>
          long pw = c;
          while (!inA[pw]) {
            for (long a : cur) {
              long v = (*group_).mul(a, pw);
              if (!inA[v]) {
                inA[v] = 1;
                Asub.push_back(v);
              }
            }
            pw = (*group_).mul(pw, c);
          }
        }
    }
    long Dst = 0;
    for (long c = 0; c < N; ++c) Dst += inA[c];
    if (!undecided_any || Dst == N) {
      if (undecided_any)
        sic::support::progressf(
            "  (support test: some characters undecided at %ld bits; the certified ones generate "
            "the dual group)\n",
            (long)first_pass_bits + 16);
      break;
    }
    if (prec_user > 0) {
      sic::support::progressf("support test undecided (increase precision)\n");
      throw sic::Error(sic::ErrorKind::precision, "support test needs more precision");
    }
    first_pass_bits *= 2;
    sic::support::progressf("support test undecided; pass 1 at %ld bits\n", (long)first_pass_bits);
  }
  supp_count = 0;
  for (long c = 0; c < N; ++c) supp_count += inSupp[c];
  Dstar = 0;
  for (long c = 0; c < N; ++c) Dstar += inA[c];
  Ssz = N / Dstar;
  sic::support::progressf(
      "B1 = max |log|E|| = %.3f; supports: %ld characters; |A*| = %ld, |S| = %ld   [%.1fs]\n", B1,
      supp_count, Dstar, Ssz, stamp());
  // Complex conjugation (w.r.t. the embedding nu) as an element cc of G: nu(cc x) = conj(nu(x)) for
  // all x in L. The generators x_j determine the elements of G (their conjugate tuples are pairwise
  // distinct, since they generate L*), so if exactly one g has all balls x_j(g) overlapping
  // conj(x_j(1)), that g is complex conjugation. Candidates act on zeta_{2d} by inversion and fix
  // sqrt(eps) (real).
  cc = cc_pre;
  if (cc < 0) {
    std::vector<long> cand;
    for (long g = 0; g < N; ++g)
      if ((*group_).nn[g] == 2 * d - 1 && (*group_).chi[g] > 0) cand.push_back(g);
    std::vector<char> hit(cand.size(), 0);
    parallel_for((long)cand.size(), nth, [&](long q) {
      sic::support::flint::Acb cj, value;
      bool all = true;
      for (int j = 0; j < m && all; ++j) {
        generator_at(*T, j, 0, first_pass_bits + 16, cj);
        acb_conj(cj, cj);
        generator_at(*T, j, cand[q], first_pass_bits + 16, value);
        all = acb_overlaps(cj, value);
      }
      hit[q] = all;
    });
    int nh = 0;
    for (size_t q = 0; q < cand.size(); ++q)
      if (hit[q]) {
        ++nh;
        cc = cand[q];
      }
    if (nh != 1) cc = -1;
  }
  if (cc >= 0 && Ssz == 1)
    sic::support::progressf("complex conjugation: element %ld of G (order %ld)\n", cc,
                            (*group_).order(cc));
  else {
    sic::support::progressf("complex conjugation not identified; complex arithmetic throughout\n");
    cc = -1;
  }

  // Installed later, once the tower/retry owners exist. Evaluate by reference
  // on every entry, after retry cleanup; a first-pass snapshot is insufficient.

  return 0;
}

}  // namespace sic::construct
