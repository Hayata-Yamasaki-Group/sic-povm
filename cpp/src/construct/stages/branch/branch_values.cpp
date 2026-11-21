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

#include "construct/components/branch_admission.hpp"
#include "construct/components/components.hpp"
#include "construct/model/session_state.hpp"
#include "construct/runtime/observations/progress.hpp"
#include "construct/runtime/resources/resident_memory.hpp"
#include "construct/runtime/resources/table_memory.hpp"
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
#include "sicx/codec/integer.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/container/producer_writer.hpp"
#include "support/resources.hpp"
namespace sic::construct {
void Construction::fold(acb_t r, const std::vector<Leaf>& lf, int n,
                        const std::vector<sic::support::ComplexBallVector>& B, slong wpd,
                        const arb_t omc, ExactSource source) {
  sic::support::flint::Arb a;
  std::unique_ptr<ExactSource::Cursor> cursor;
  if (source) cursor = std::make_unique<ExactSource::Cursor>(source);
  sic::support::ComplexBallVector v = inverse_leaf_tree(
      ps, n, {0},
      [&](long e, acb_ptr out) {
        const auto& leaf = cursor ? cursor->next() : lf[e];
        arb_set_fmpz(a, leaf.y);
        arb_mul(a, a, omc, wpd);
        arb_add_fmpz(a, a, leaf.x, wpd);
        acb_set_arb(out, a);
      },
      [&](acb_ptr out, acb_srcptr child, int i, long, long k) {
        if (k == 0)
          acb_mul(out, child, B[i][0], wpd);
        else
          acb_addmul(out, child, B[i][k], wpd);
      });
  acb_set(r, v[0]);
}

bool Construction::candidates(int i, const Path& pt, const arb_t omc,
                              sic::support::ComplexBallVector& cand, std::vector<double>& gs,
                              std::vector<sic::support::ComplexBallVector>& Bc) {
  const long p = ps[i];
  const slong wpd = pt.wpd;
  sic::support::flint::Arb ab;
  bool ok = true;
  if (p == 2) {
    sic::support::flint::Acb s2;
    const size_t q = item_idx[i][0];
    if (item_constant[q])
      acb_set_si(s2, -2);
    else
      fold(s2, ilv[q], i, pt.B, wpd, omc, item_sources[q]);
    cand = sic::support::ComplexBallVector(2);
    // a square root away from the branch cut (s2 is often a negative real number, e.g. for w = zeta
    // - zeta^-1)
    if (arb_is_negative(acb_realref(s2))) {
      acb_neg(s2, s2);
      acb_sqrt(cand[0], s2, wpd);
      acb_mul_onei(cand[0], cand[0]);
    } else
      acb_sqrt(cand[0], s2, wpd);
    acb_neg(cand[1], cand[0]);
    if (!acb_is_finite(cand[0]) || acb_contains_zero(cand[0])) ok = false;
    gs.assign(2, -1e300);
    Bc.assign(2, sic::support::ComplexBallVector(2));
    for (int j = 0; j < 2 && ok; ++j) {
      acb_one(Bc[j][0]);
      acb_mul_2exp_si(Bc[j][0], Bc[j][0], -1);
      acb_mul_2exp_si(Bc[j][1], cand[j], 1);
      acb_inv(Bc[j][1], Bc[j][1], wpd);
      acb_abs(ab, cand[j], wpd);
      arb_log(ab, ab, 64);
      gs[j] = arf_get_d(arb_midref(ab), ARF_RND_NEAR);
    }
  } else {
    sic::support::flint::AcbPoly f;
    sic::support::flint::Acb ak;
    for (long k = 0; k < p; ++k) {
      const size_t q = item_idx[i][k];
      fold(ak, ilv[q], i, pt.B, wpd, omc, item_sources[q]);
      acb_poly_set_coeff_acb(f, k, ak);
    }
    acb_one(ak);
    acb_poly_set_coeff_acb(f, p, ak);
    cand = sic::support::ComplexBallVector(p);
    slong niso = acb_poly_find_roots(cand.v.data(), f, NULL, 0, wpd);
    if (niso < p) ok = false;
    gs.assign(p, -1e300);
    Bc.assign(p, sic::support::ComplexBallVector(p));
    sic::support::ComplexBallVector cf(p);
    sic::support::flint::Acb fp;
    for (long j = 0; j < p && ok; ++j) {
      // synthetic division f / (X - r): c_{p-1} = 1, c_{k-1} = a_k + r c_k; f'(r) = sum_k c_k r^k
      acb_one(cf[p - 1]);
      for (long k = p - 1; k >= 1; --k) {
        acb_poly_get_coeff_acb(ak, f, k);
        acb_mul(cf[k - 1], cand[j], cf[k], wpd);
        acb_add(cf[k - 1], cf[k - 1], ak, wpd);
      }
      acb_zero(fp);
      for (long k = p - 1; k >= 0; --k) {
        acb_mul(fp, fp, cand[j], wpd);
        acb_add(fp, fp, cf[k], wpd);
      }
      if (acb_contains_zero(fp) || !acb_is_finite(fp)) continue;
      for (long k = 0; k < p; ++k) acb_div(Bc[j][k], cf[k], fp, wpd);
      acb_abs(ab, fp, wpd);
      arb_log(ab, ab, 64);
      gs[j] = arf_get_d(arb_midref(ab), ARF_RND_NEAR);
    }
  }
  return ok;
}

bool Construction::compute_path(slong wpd, const Path* prev, Path& pt) {
  slong bits = 0;
  sic::support::resources::Bytes source_reader = 0;
  for (size_t q = 0; q < items.size(); ++q) {
    bits = std::max(bits, item_sources[q].max_bits());
    source_reader = std::max(source_reader, item_sources[q].reader_forecast());
    for (const auto& leaf : ilv[q])
      bits = std::max(bits, (slong)std::max(fmpz_bits(leaf.x), fmpz_bits(leaf.y)));
  }
  const auto scratch = branch_scratch_bytes(ps, wpd, bits, source_reader);
  budget.require_additional("branch precision/retry bounded inverse-tree scratch", scratch);
  pt.B.assign(t, sic::support::ComplexBallVector());
  pt.root = sic::support::ComplexBallVector(t);
  pt.wpd = wpd;
  pt.score = 0;
  sic::support::flint::Arb sq0, omc;
  arb_sqrt_ui(sq0, D0, wpd);
  arb_sub_ui(omc, sq0, 1, wpd);
  arb_neg(omc, omc);
  arb_mul_2exp_si(omc, omc, -1);
  bool ok = true;
  for (int i = 0; i < t && ok; ++i) {
    sic::support::ComplexBallVector cand;
    std::vector<double> gs;
    std::vector<sic::support::ComplexBallVector> Bc;
    if (!candidates(i, pt, omc, cand, gs, Bc)) {
      if (producer_options.path_debug) {
        sic::support::progressf("  path: step %d (degree %ld) at %ld bits: candidates failed:", i,
                                ps[i], (long)wpd);
        for (size_t j = 0; j < cand.size(); ++j) {
          sic::support::progressf(" ");
          print_acb(cand[j], 10, 0);
        }
        sic::support::progressf("\n");
      }
      ok = false;
      break;
    }
    long best = -1, matches = 0;
    double bsc = -1e300;
    for (long j = 0; j < (long)cand.size(); ++j) {
      if (prev) {
        if (acb_overlaps(cand[j], prev->root[i])) {
          ++matches;
          if (gs[j] > -1e299) best = j;
        }
        continue;
      }
      if (gs[j] <= -1e299) continue;
      double sc = gs[j];
      if (i + 1 < t) {
        pt.B[i] = Bc[j];
        sic::support::ComplexBallVector c2;
        std::vector<double> g2;
        std::vector<sic::support::ComplexBallVector> B2;
        if (candidates(i + 1, pt, omc, c2, g2, B2))
          sc += *std::max_element(g2.begin(), g2.end());
        else
          sc = -1e299;
      }
      if (sc > bsc) {
        bsc = sc;
        best = j;
      }
    }
    if (best < 0 || (prev && matches != 1)) {
      ok = false;
      break;
    }
    pt.B[i] = Bc[best];
    acb_set(pt.root[i], cand[best]);
    pt.score += gs[best];
  }
  return ok;
}
}  // namespace sic::construct
