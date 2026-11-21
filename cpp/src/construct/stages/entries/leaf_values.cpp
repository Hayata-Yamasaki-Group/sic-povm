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
void Construction::leaves_fft_real(sic::support::RealBallVector& a, int n, const StepData& sd,
                                   slong wp, int nthr, sic::support::RealBallVector* buf) {
  bool quadratic = true;
  for (int i = 0; i < n; ++i) quadratic = quadratic && ps[i] == 2;
  if (quadratic) {
    if (buf) *buf = sic::support::RealBallVector();
    std::function<void(int, bool)> residency;
    const bool backed =
        std::any_of(sd.g.begin(), sd.g.begin() + n,
                    [](const AnalyticGenerator& generator) { return bool(generator.backing); });
    const bool windowed = backed && std::any_of(sd.g.begin(), sd.g.begin() + n,
                                                [](const AnalyticGenerator& generator) {
                                                  return generator.pieces.size() > 1;
                                                });
    if (windowed) {
      quadratic_real_forward_windowed(
          a.v, n, wp, nthr, generator_block, [&](int i, long c) { return sd.g[i].real_even(c); },
          [&](int i, size_t first_label, size_t size, bool load) {
            if (load) {
              if (sd.g[i].backing) {
                budget.require_additional(
                    "analytic generator window",
                    sic::support::resources::mul(size, sic::support::resources::real_bytes(wp)));
                sd.g[i].prepare_piece(first_label, size);
              } else
                sd.g[i].prepare();
            } else
              sd.g[i].park();
          });
      return;
    }
    if (backed)
      residency = [&](int i, bool load) {
        if (load) {
          if (sd.g[i].backing && sd.g[i].scalar.size() == 0)
            budget.require_additional(
                "analytic generator reload",
                sic::support::resources::mul(Ncos[i], sic::support::resources::real_bytes(wp)));
          sd.g[i].prepare();
        } else
          sd.g[i].park();
      };
    quadratic_real_forward(
        a.v, n, wp, nthr, true, [&](int i, long c) { return sd.g[i].real_even(c); }, residency);
    return;
  }
  sic::support::RealBallVector bloc;
  if (buf && buf->size() != a.size()) *buf = sic::support::RealBallVector(a.size());
  if (!buf) bloc = sic::support::RealBallVector(a.size());
  sic::support::RealBallVector& b = buf ? *buf : bloc;
  long pmax = 2;
  for (int i = 0; i < n; ++i) pmax = std::max(pmax, ps[i]);
  for (int i = n - 1; i >= 0; --i) {
    // Two quadratic levels in one pass: same Arb operations and enclosures,
    // with no full-size intermediate vector or extra thread launch.
    auto load = [&](int level) {
      if (sd.g[level].backing)
        budget.require_additional(
            "mixed transform generator reload",
            sic::support::resources::mul(Ncos[level], sic::support::resources::real_bytes(wp)));
      sd.g[level].prepare();
    };
    load(i);
    if (i > 0 && ps[i] == 2 && ps[i - 1] == 2) {
      load(i - 1);
      const long ni = Ncos[i - 1], block4 = 4 * ni;
      const long total = (long)a.size() / 4;
      const long chunks = nthr > 1 ? std::min<long>(total, 16L * nthr) : 1;
      parallel_for(chunks, nthr, [&](long ch) {
        sic::support::RealBallVector work(6);
        for (long u = ch * total / chunks; u < (ch + 1) * total / chunks; ++u) {
          const long base = (u / ni) * block4, c = u % ni, x = base + 4 * c;
          arb_add(work[0], a[x], a[x + 1], wp);
          arb_sub(work[1], a[x], a[x + 1], wp);
          arb_mul(work[1], work[1], sd.g[i].real_even(2 * c), wp);
          arb_add(work[2], a[x + 2], a[x + 3], wp);
          arb_sub(work[3], a[x + 2], a[x + 3], wp);
          arb_mul(work[3], work[3], sd.g[i].real_even(2 * c + 1), wp);
          arb_add(b[base + c], work[0], work[2], wp);
          arb_sub(work[4], work[0], work[2], wp);
          arb_mul(b[base + ni + c], work[4], sd.g[i - 1].real_even(c), wp);
          arb_add(b[base + 2 * ni + c], work[1], work[3], wp);
          arb_sub(work[5], work[1], work[3], wp);
          arb_mul(b[base + 3 * ni + c], work[5], sd.g[i - 1].real_even(c), wp);
        }
      });
      std::swap(a.v, b.v);
      sd.g[i].park();
      sd.g[i - 1].park();
      --i;
      continue;
    }
    const long p = ps[i], Ni = Ncos[i], block = Ncos[i + 1], nblk = (long)a.size() / block,
               tot = nblk * Ni;
    const auto& gv = sd.g[i];
    const long nchunk = nthr > 1 ? std::min<long>(tot, 16L * nthr) : 1;
    parallel_for(nchunk, nthr, [&](long ch) {
      sic::support::flint::Arb df;
      sic::support::RealBallVector acc(pmax);
      for (long u = ch * tot / nchunk; u < (ch + 1) * tot / nchunk; ++u) {
        const long b0 = (u / Ni) * block, c = u % Ni;
        if (p == 2) {
          arb_add(b[b0 + c], a[b0 + 2 * c], a[b0 + 2 * c + 1], wp);
          arb_sub(df, a[b0 + 2 * c], a[b0 + 2 * c + 1], wp);
          arb_mul(b[b0 + Ni + c], df, gv.real_even(c), wp);
        } else {
          for (long j = 0; j < p; ++j) arb_set(acc[j], a[b0 + p * c + j]);
          for (long k = 0; k < p; ++k) {
            arb_ptr o = b[b0 + k * Ni + c];
            arb_set(o, acc[0]);
            for (long j = 1; j < p; ++j) arb_add(o, o, acc[j], wp);
            if (k + 1 < p)
              for (long j = 0; j < p; ++j)
                arb_mul(acc[j], acc[j], acb_realref(gv.full[p * c + j]), wp);
          }
        }
      }
    });
    std::swap(a.v, b.v);
    sd.g[i].park();
  }
}

void Construction::item_vec(const StepData& sd, const Item& it, slong wp,
                            sic::support::ComplexBallVector& a) {
  const long nc = Ncos[it.i];
  a = sic::support::ComplexBallVector(nc);
  if (it.k < 0) {
    if (sd.g[it.i].backing)
      budget.require_additional(
          "step generator reload",
          sic::support::resources::mul(nc, sic::support::resources::real_bytes(wp)));
    sd.g[it.i].prepare();
    parallel_for(nc, nth, [&](long c) {
      if (sd.g[it.i].character)
        acb_set_si(a[c], -2);
      else {
        sd.g[it.i].get(a[c], 2 * c);
        acb_sqr(a[c], a[c], wp);
      }
    });
    sd.g[it.i].park();
  } else
    for (long c = 0; c < nc; ++c) acb_set(a[c], sd.fc[it.i][it.k][c]);
}

void Construction::item_leaves(const StepData& sd, const Item& it, slong wp,
                               sic::support::RealBallVector& y, int nthr) {
  sic::support::ComplexBallVector a;
  item_vec(sd, it, wp, a);
  y = sic::support::RealBallVector(a.size());
  for (size_t e = 0; e < a.size(); ++e) arb_swap(y[e], acb_realref(a[e]));
  a = sic::support::ComplexBallVector();
  leaves_fft_real(y, it.i, sd, wp, nthr);
}

double Construction::max_log2rad(
    const sic::support::RealBallVector& a) {  // in log space: radii can exceed the double range
  double r = -1e300;
  for (size_t e = 0; e < a.size(); ++e) {
    mag_srcptr mg = arb_radref(a[e]);
    if (!mag_is_zero(mg)) r = std::max(r, mag_is_inf(mg) ? 1e300 : mag_get_d_log2_approx(mg));
  }
  return r;
}

sic::support::ComplexBallVector Construction::roots_2d(slong wp) {
  sic::support::ComplexBallVector z2d(2 * d);
  for (long k = 0; k < 2 * d; ++k) {
    arb_t x;
    arb_init(x);
    arb_set_si(x, k);
    arb_div_si(x, x, 2 * d, wp);
    acb_e(z2d[k], x, wp);
    arb_clear(x);
  }
  return z2d;
}
}  // namespace sic::construct
