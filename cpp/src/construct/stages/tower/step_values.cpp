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
Construction::StepData Construction::step_data_multi(const std::vector<Cand>& cands, slong wp,
                                                     bool verbose,
                                                     const std::vector<sic::support::Index>* sel_in,
                                                     bool back_generators) {
  StepData steps;
  steps.g.resize(t);
  steps.fc.resize(t);
  steps.Cg.resize(t);
  steps.log2U.resize(t);
  steps.certified_Cg.resize(t);
  steps.certified_Cf.resize(t);
  steps.sel.assign(t, -1);
  std::vector<double> best(t, 1e300), mxs(t, 0);
  std::vector<char> okv(t, 0);
  for (size_t q = 0; q < cands.size(); ++q) {
    bool used = !sel_in;
    if (sel_in)
      for (int i = 0; i < t; ++i)
        if ((*sel_in)[i] == sic::support::checked_index(q)) used = true;
    if (!used) continue;
    if (producer_options.progress)
      sic::support::progressf(
          "candidate start: index=%zu name=%s precision=%ld highest_needed=%d [%.1fs]\n", q,
          cands[q].name.c_str(), (long)wp, cands[q].highest_needed, stamp());
    // coset sums level by level from the top: step i uses the sums over the cosets of H_{i+1}
    int lowest = 0;
    if (sel_in) {
      lowest = t;
      for (int i = 0; i < t; ++i)
        if ((*sel_in)[i] == sic::support::checked_index(q)) lowest = std::min(lowest, i);
    }

    {  // conjugation real tail and Ssz == 1 (both required by the adapted tower)
      // Conjugate pairs give H_{t-1}=2 Re(x) and w_top=2i Im(x).
      // Accumulate directly to the highest needed real level; no D-ball
      // complex Hn is formed, and lower levels retain real sums only.
      int highest = t - 1;
      if (sel_in) {
        highest = -1;
        for (int i = 0; i < t; ++i)
          if ((*sel_in)[i] == sic::support::checked_index(q)) highest = i;
      }
      const auto& candidate = cands[q];
      const bool pure_top =
          !candidate.tb && !candidate.sq && !candidate.product && candidate.zpow == d / 4;
      if (!sel_in && !pure_top) highest = t - 2;
      if (!sel_in && candidate.highest_needed >= 0)
        highest = std::min(highest, candidate.highest_needed);
      sic::support::ComplexBallVector roots;
      sic::support::flint::Arb sqe;
      if (!candidate.tb) {
        roots = sic::support::ComplexBallVector(2 * d);
        for (long k = 0; k < 2 * d; ++k) root_2d(k, wp, roots[k]);
        if (candidate.sq || candidate.product) {
          to_arb(sqe, (*ar_).kc, (*ar_).eps, 1, wp);
          arb_sqrt(sqe, sqe, wp);
        }
      }
      auto value = [&](long g, acb_ptr out) {
        if (candidate.product)
          product_at(*T, g, wp, roots, sqe, out);
        else if (candidate.tb)
          conj_f_at(*candidate.tb, 0, d >> (candidate.tj + 1), g, out);
        else {
          acb_set(out, roots[(candidate.zpow * (*group_).nn[g]) % (2 * d)]);
          if (candidate.sq) {
            acb_mul_arb(out, out, sqe, wp);
            if ((*group_).chi[g] < 0) acb_neg(out, out);
          }
        }
      };
      auto consider = [&](int i, AnalyticGenerator&& generated, bool ok, double l2U, double mxd) {
        if (i == t - 1 && !pure_top) return;
        const long per = N / Ncos[i + 1];
        const double Cg = (ps[i] == 2 ? 2.0 : 1.0) * per * candidate.C;
        HeightBound certified_Cg = HeightBound((ulong)per) * candidate.certified_C;
        if (ps[i] == 2) certified_Cg = HeightBound(2) * certified_Cg;
        const double score = l2U + std::log2(Cg);
        if (sel_in || (ok && score < best[i])) {
          best[i] = score;
          steps.sel[i] = sic::support::checked_index(q);
          okv[i] = ok;
          mxs[i] = mxd;
          if (evaluation_choices)
            apply_generator_choice(i, generated, evaluation_choices->at(i), wp);
          steps.g[i] = std::move(generated);
          steps.Cg[i] = Cg;
          steps.certified_Cg[i] = certified_Cg;
          steps.log2U[i] = l2U;
          if (back_generators && sel_in && i != t - 1 && ps[i] == 2 &&
              !steps.g[i].periodic.size()) {
            const auto bytes =
                sic::support::resources::mul(Ncos[i], sic::support::resources::real_bytes(wp));
            if (memory.decide_reserved("generator/" + std::to_string(i),
                                       {bytes, true, false, 0, (unsigned)(s + 3)}) ==
                Retention::spill)
              steps.g[i].spill(generator_block);
          }
        }
      };
      bool symbolic_top_done = false;
      if (pure_top && (!sel_in || highest == t - 1) && rep[t][0] == 0) {
        sic::support::ComplexBallVector root(1), check(1);
        acb_set(root[0], roots[d / 4]);
        arb_zero(acb_realref(root[0]));
        arb_mul_2exp_si(acb_imagref(root[0]), acb_imagref(root[0]), 1);
        acb_sqr(check[0], root[0], wp);
        acb_add_si(check[0], check[0], 2, wp);
        if (acb_is_finite(root[0]) && arb_is_positive(acb_imagref(root[0])) &&
            acb_contains_zero(check[0])) {
          mag_t bound;
          mag_init(bound);
          acb_get_mag(bound, root[0]);
          const double upper = mag_get_d_log2_approx(bound);
          acb_get_mag_lower(bound, root[0]);
          const double lower = mag_get_d(bound);
          mag_clear(bound);
          AnalyticGenerator generated;
          generated.count = Ncos[t];
          generated.assign_character(root[0], [&, level = t](long label) {
            return eighth_character((*group_).nn[rep[level][label]]);
          });
          consider(t - 1, std::move(generated), true, upper, lower);
          // zeta_8 itself has the nonzero lower real trace sqrt(2).
          // Only its top difference is purely imaginary; preserve lower
          // candidate selection without materializing the top array.
          symbolic_top_done = true;
        }
      }
      if (highest == t - 1 && !symbolic_top_done) {
        sic::support::RealBallVector top(Ncos[t - 1]);
        const long chunks = std::min<long>(top.size(), 16L * nth);
        std::vector<char> nz(chunks, 0);
        std::vector<double> lo(chunks, 0), hi(chunks, -1e300);
        parallel_for(chunks, nth, [&](long chunk) {
          sic::support::ComplexBallVector v(1);
          mag_t magnitude;
          mag_init(magnitude);
          for (long c = chunk * (long)top.size() / chunks;
               c < (chunk + 1) * (long)top.size() / chunks; ++c) {
            value(rep[t][2 * c], v[0]);
            arb_mul_2exp_si(top[c], acb_imagref(v[0]), 1);
            arb_get_mag_lower(magnitude, top[c]);
            nz[chunk] |= !mag_is_zero(magnitude);
            lo[chunk] = std::max(lo[chunk], mag_get_d(magnitude));
            arb_get_mag(magnitude, top[c]);
            hi[chunk] = std::max(hi[chunk], mag_get_d_log2_approx(magnitude));
          }
          mag_clear(magnitude);
        });
        AnalyticGenerator generated;
        generated.assign_scalar(std::move(top), true);
        consider(t - 1, std::move(generated),
                 std::any_of(nz.begin(), nz.end(), [](char x) { return x; }),
                 *std::max_element(hi.begin(), hi.end()), *std::max_element(lo.begin(), lo.end()));
      }
      const int real_highest = std::min(highest, t - 2);
      const bool periodic_candidate = !candidate.tb && !candidate.sq && !candidate.product;
      if (periodic_candidate) {
        for (int i = real_highest; i >= lowest; --i) {
          if (sel_in && (*sel_in)[i] != sic::support::checked_index(q)) continue;
          if (ps[i] != 2)
            continue;  // odd steps (adapted, at the bottom) use the generic generators
          const long tau = rep[i + 1][1];
          if (coset(i, tau) != 0 || coset(i + 1, tau) != 1 || coset(i + 1, cc) != 0)
            throw std::logic_error("invalid real cyclotomic quadratic trace level");
          const auto kernel =
              prime_annihilator_generators((*group_), std::span<const long>(phis.data(), i + 1),
                                           std::span<const long>(ps.data(), i + 1));
          sic::support::RealBallVector values = cyclotomic_generator_values(
              (*group_), candidate.zpow, kernel, N / Ncos[i + 1], tau, cc, roots, wp);
          mag_t magnitude;
          mag_init(magnitude);
          double upper = -1e300, lower = 0;
          bool nonzero = false;
          const auto reachable = norm_image((*group_), (*group_).stride, values.size());
          for (long n : reachable) {
            arb_get_mag(magnitude, values[n]);
            upper = std::max(upper, mag_get_d_log2_approx(magnitude));
            arb_get_mag_lower(magnitude, values[n]);
            nonzero |= !mag_is_zero(magnitude);
            lower = std::max(lower, mag_get_d(magnitude));
          }
          mag_clear(magnitude);
          const long modulus = values.size();
          AnalyticGenerator generated;
          generated.assign_periodic(std::move(values), Ncos[i + 1],
                                    [&, level = i + 1, modulus](long pair) {
                                      return (size_t)((*group_).nn[rep[level][2 * pair]] % modulus);
                                    });
          consider(i, std::move(generated), nonzero, upper, lower);
        }
        continue;
      }
      if (real_highest >= lowest) {
        const long size = Ncos[real_highest + 1];
        const long pairs = Ncos[t] / (2 * size);
        sic::support::RealBallVector Hn(size);
        const long chunks = std::min<long>(size, 16L * nth);
        if (back_generators && table_parked && (candidate.tb || candidate.product)) {
          if (candidate.tb && candidate.tb != T.get())
            throw std::logic_error("unexpected backed candidate table");
          if (analytic_by_class.empty()) {
            analytic_by_class.resize((*ar_).h());
            for (long label = 0; label < Ncos[t] / 2; ++label)
              analytic_by_class.at(source_class(rep[t][2 * label]))
                  .push_back(sic::support::checked_index(label));
          }
          for (sic::support::Index source = 0; source < (*ar_).h(); ++source) {
            const auto& indices = analytic_by_class[source];
            if (indices.empty()) continue;
            load_table_class(source);
            const long index_chunks = std::min<long>(indices.size(), 16L * nth);
            parallel_for(index_chunks, nth, [&](long chunk) {
              sic::support::ComplexBallVector v(1);
              sic::support::RealBallVector term(1);
              for (long position = chunk * indices.size() / index_chunks;
                   position < (long)((chunk + 1) * indices.size() / index_chunks); ++position) {
                const long c = indices[position] / pairs;
                if (position && indices[position - 1] / pairs == c) continue;
                // Sorted labels give one run per c in each class. Only its
                // first label owns this sum, so workers never share Hn[c].
                for (size_t k = position; k < indices.size() && indices[k] / pairs == c; ++k) {
                  value(rep[t][2L * indices[k]], v[0]);
                  arb_mul_2exp_si(term[0], acb_realref(v[0]), 1);
                  arb_add(Hn[c], Hn[c], term[0], wp);
                }
              }
            });
            drop_table_class(source);
          }
        } else
          parallel_for(chunks, nth, [&](long chunk) {
            sic::support::ComplexBallVector v(1);
            sic::support::RealBallVector term(1);
            for (long c = chunk * size / chunks; c < (chunk + 1) * size / chunks; ++c)
              for (long k = 0; k < pairs; ++k) {
                value(rep[t][2 * (c * pairs + k)], v[0]);
                arb_mul_2exp_si(term[0], acb_realref(v[0]), 1);
                arb_add(Hn[c], Hn[c], term[0], wp);
              }
          });
        for (int i = real_highest; i >= lowest; --i) {
          const long p = ps[i], size_i = Ncos[i];
          if (p == 2 && back_generators) {
            const bool selected = !sel_in || (*sel_in)[i] == sic::support::checked_index(q);
            AnalyticGenerator generated;
            if (selected) generated.assign_scalar(sic::support::RealBallVector(size_i), false);
            sic::support::RealBallVector lower(i > lowest ? size_i : 0);
            const long chunks_i = std::min<long>(size_i, 16L * nth);
            std::vector<char> nz(chunks_i, 0);
            std::vector<double> lo(chunks_i, 0), hi(chunks_i, -1e300);
            parallel_for(chunks_i, nth, [&](long chunk) {
              sic::support::RealBallVector sum(1);
              mag_t magnitude;
              mag_init(magnitude);
              for (long c = chunk * size_i / chunks_i; c < (chunk + 1) * size_i / chunks_i; ++c) {
                if (i > lowest) arb_add(sum[0], Hn[2 * c], Hn[2 * c + 1], wp);
                if (selected) {
                  arb_sub(Hn[2 * c + 1], Hn[2 * c], Hn[2 * c + 1], wp);
                  arb_swap(generated.scalar[c], Hn[2 * c + 1]);
                  arb_get_mag_lower(magnitude, generated.scalar[c]);
                  nz[chunk] |= !mag_is_zero(magnitude);
                  lo[chunk] = std::max(lo[chunk], mag_get_d(magnitude));
                  arb_get_mag(magnitude, generated.scalar[c]);
                  hi[chunk] = std::max(hi[chunk], mag_get_d_log2_approx(magnitude));
                } else {
                  arb_clear(Hn[2 * c + 1]);
                  arb_init(Hn[2 * c + 1]);
                }
                if (i > lowest) {
                  arb_swap(Hn[2 * c], sum[0]);
                  arb_swap(lower[c], Hn[2 * c]);
                } else {
                  arb_clear(Hn[2 * c]);
                  arb_init(Hn[2 * c]);
                }
              }
              mag_clear(magnitude);
            });
            if (selected)
              consider(i, std::move(generated),
                       std::any_of(nz.begin(), nz.end(), [](char present) { return present; }),
                       *std::max_element(hi.begin(), hi.end()),
                       *std::max_element(lo.begin(), lo.end()));
            if (i > lowest) Hn = std::move(lower);
            continue;
          }
          if (!sel_in || (*sel_in)[i] == sic::support::checked_index(q)) {
            AnalyticGenerator generated;
            if (p == 2)
              generated.assign_scalar(sic::support::RealBallVector(size_i), false);
            else
              generated.assign(sic::support::ComplexBallVector(Ncos[i + 1]), false, true, false);
            const long chunks_i = std::min<long>(size_i, 16L * nth);
            std::vector<char> nz(chunks_i, 0);
            std::vector<double> lo(chunks_i, 0), hi(chunks_i, -1e300);
            parallel_for(chunks_i, nth, [&](long chunk) {
              sic::support::RealBallVector difference(1);
              mag_t magnitude;
              mag_init(magnitude);
              for (long c = chunk * size_i / chunks_i; c < (chunk + 1) * size_i / chunks_i; ++c) {
                if (p == 2) {
                  arb_sub(generated.scalar[c], Hn[2 * c], Hn[2 * c + 1], wp);
                  arb_get_mag_lower(magnitude, generated.scalar[c]);
                  nz[chunk] |= !mag_is_zero(magnitude);
                  lo[chunk] = std::max(lo[chunk], mag_get_d(magnitude));
                  arb_get_mag(magnitude, generated.scalar[c]);
                  hi[chunk] = std::max(hi[chunk], mag_get_d_log2_approx(magnitude));
                } else {
                  for (long j = 0; j < p; ++j) {
                    acb_set_arb(generated.full[p * c + j], Hn[p * c + j]);
                    arb_get_mag(magnitude, Hn[p * c + j]);
                    hi[chunk] = std::max(hi[chunk], mag_get_d_log2_approx(magnitude));
                    if (j) {
                      arb_sub(difference[0], Hn[p * c + j], Hn[p * c], wp);
                      arb_get_mag_lower(magnitude, difference[0]);
                      nz[chunk] |= !mag_is_zero(magnitude);
                      lo[chunk] = std::max(lo[chunk], mag_get_d(magnitude));
                    }
                  }
                }
              }
              mag_clear(magnitude);
            });
            consider(i, std::move(generated),
                     std::any_of(nz.begin(), nz.end(), [](char x) { return x; }),
                     *std::max_element(hi.begin(), hi.end()),
                     *std::max_element(lo.begin(), lo.end()));
          }
          if (i > lowest) {
            sic::support::RealBallVector lower(size_i);
            parallel_for(size_i, nth, [&](long c) {
              for (long j = 0; j < p; ++j) arb_add(lower[c], lower[c], Hn[p * c + j], wp);
            });
            Hn = std::move(lower);
          }
        }
      }
    }
  }
  for (int i = 0; i < t; ++i) {
    const long p = ps[i], nc = Ncos[i + 1];
    if (steps.sel[i] < 0 || !okv[i]) steps.ok = false;
    if (i == t - 1 && p == 2 && d % 4 == 0 && steps.sel[i] >= 0) {
      const auto& chosen = cands[steps.sel[i]];
      if (!chosen.tb && !chosen.sq && !chosen.product && chosen.zpow == d / 4 && rep[t][0] == 0) {
        sic::support::ComplexBallVector root(1), check(1);
        steps.g[i].get(root[0], 0);
        acb_sqr(check[0], root[0], wp);
        acb_add_si(check[0], check[0], 2, wp);
        if (arb_is_zero(acb_realref(root[0])) && arb_is_positive(acb_imagref(root[0])) &&
            acb_is_finite(root[0]) && acb_contains_zero(check[0])) {
          steps.g[i].assign_character(root[0], [&, level = t](long label) {
            return eighth_character((*group_).nn[rep[level][label]]);
          });
        }
      }
    }
    if (verbose)
      sic::support::progressf(
          " step %d (degree %ld): |Gal(L/F_{i+1})| = %ld, generator from %s, max|g - sigma g| = "
          "%.3e, log2 max|g| = %.1f, certified nonzero: %s\n",
          i, p, N / nc, steps.sel[i] >= 0 ? cands[steps.sel[i]].name.c_str() : "-", mxs[i],
          steps.log2U[i], okv[i] ? "yes" : "NO");
    if (p != 2 && steps.sel[i] >= 0) {
      // coefficients of f_i on G/H_i; |a_k| <= binom(p, k) Cg^{p-k}
      steps.fc[i].assign(p, sic::support::ComplexBallVector(nc / p));
      steps.certified_Cf[i].resize(p);
      for (long k = 0; k < p; ++k)
        steps.certified_Cf[i][k] = HeightBound::binomial(p, k) * steps.certified_Cg[i].pow(p - k);
      const sic::support::ComplexBallVector& gv = steps.g[i].full;
      parallel_for(nc / p, nth, [&](long c) {
        acb_poly_t f;
        acb_poly_init(f);
        acb_poly_product_roots(f, gv[p * c], p, wp);
        for (long k = 0; k < p; ++k) acb_poly_get_coeff_acb(steps.fc[i][k][c], f, k);
        acb_poly_clear(f);
      });
    }
  }
  return steps;
}

std::vector<Construction::Cand> Construction::cand_list() {
  std::vector<Cand> c;
  for (int j = 0; j < s; ++j) {
    Cand x;
    x.tb = T.get();
    x.tj = j;
    x.name = "x" + std::to_string(j);
    c.push_back(x);
  }
  {
    Cand x;
    x.zpow = 1;
    x.name = "x" + std::to_string(s);
    c.push_back(x);
  }  // zeta_{2d}
  {
    Cand x;
    x.zpow = 0;
    x.sq = true;
    x.name = "x" + std::to_string(s + 1);
    c.push_back(x);
  }  // sqrt(eps)
  if (relative_highest >= 0) {
    Cand x;
    x.product = true;
    x.C = (*product_).height;
    x.certified_C = (*product_).certified_height;
    x.name = "nonlinear-product";
    x.highest_needed = relative_highest;
    c.push_back(x);
  }
  for (int j = 1; j <= s; ++j) {
    Cand x;
    x.zpow = 1L << j;
    x.name = "zeta^" + std::to_string(1L << j);
    c.push_back(x);
  }
  for (int j = 0; j <= s; ++j) {
    Cand x;
    x.zpow = 1L << j;
    x.sq = true;
    x.name = "sqrt(eps) zeta^" + std::to_string(1L << j);
    c.push_back(x);
  }
  return c;
}
}  // namespace sic::construct
