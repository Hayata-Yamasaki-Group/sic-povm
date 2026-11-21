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
namespace sic::construct {
int Construction::precision() {  // ---------------- choice of the working precision
                                 // ----------------
  if (model_->compact_tables) {
    StepData metadata;
    metadata.certified_Cg = selected_bounds;
    long height = 0;
    for (int r = 0; r <= s; ++r) height = std::max(height, representative_height(metadata, r));
    (*model_).decoder_hint = height + (slong)std::ceil(std::log2((double)Ncos[t])) + 224;
    (*model_).phased_final = phased_storage;
    (*model_).symbolic_top = true;
    sic::support::progressf(
        "phased storage verified; initial transported-decoding precision hint %llu bits\n",
        (unsigned long long)(*model_).decoder_hint);
  }
  P = first_pass_bits;
  if (prec_user == 0) {
    // leaf radii scale like 2^{-P}: run the leaf computation at first_pass_bits and extrapolate
    const slong wp = first_pass_bits + 16;
    budget.require("coarse precision estimation", (*model_).support(wp));
    const double tpre = stamp();
    StepData estimated_steps = generator_evaluator->evaluate_all(generator_choices, wp, false);
    double need = -1e300;

    for (auto& it : items) {
      if (constant_item(estimated_steps, it)) continue;  // exact -2 has no analytic leaf error
      if (producer_options.progress)
        sic::support::progressf("precision probe: step=%d rows=%ld [%.1fs]\n", it.i, Ncos[it.i],
                                stamp());
      sic::support::RealBallVector y;
      item_leaves(estimated_steps, it, wp, y, nth);
      need = std::max(
          need,
          max_log2rad(y) + log2_bp_of(estimated_steps, it.i, item_height(estimated_steps, it)) + 8);
    }
    sic::support::flint::Arb sqd1;
    arb_sqrt_ui(sqd1, d + 1, wp);
    sic::support::ComplexBallVector z2d = roots_2d(wp);
    // a sample of entries (the radii are of the same size for all entries; a shortfall is caught by
    // recognition) (transported decoding recognizes only the representatives: two of them, eta_1
    // and eta_{d/2})
    const std::vector<long> sample{1, d / 2};
    std::vector<double> nd(sample.size(), -1e300);
    const double tp0 = stamp();
    EntryCtx E;
    make_entry_ctx(*T, E);
    // only the representatives used by the sample
    std::vector<char> want(s + 1, 0);
    for (long mm : sample) {
      long sg;
      want[rep_of(mm, sg)] = 1;
    }
    double reps_time = 0, leaves_time = 0;
    for (int r = 0; r <= s; ++r)
      if (want[r]) {
        double started = stamp();
        if (producer_options.progress)
          sic::support::progressf("precision probe: representative=%ld values [%.1fs]\n",
                                  rep_entry(r), stamp());
        sic::support::RealBallVector channels;
        representative_channels(E, r, wp, z2d, sqd1, estimated_steps, channels);
        reps_time += stamp() - started;
        started = stamp();
        if (producer_options.progress)
          sic::support::progressf(
              "precision probe: representative=%ld transform rows=%zu [%.1fs]\n", rep_entry(r),
              channels.size(), stamp());
        transform_channels(channels, r, estimated_steps, wp);
        const double estimate =
            max_log2rad(channels) + representative_height(estimated_steps, r) + 8;
        for (size_t q = 0; q < sample.size(); ++q)
          if (sample[q] == rep_entry(r)) nd[q] = estimate;
        leaves_time += stamp() - started;
      }
    sic::support::progressf(
        "  (pass-1 leaves: step data %.1fs, %d representatives %.1fs, %zu sample entries %.1fs)\n",
        tp0 - tpre, (int)std::count(want.begin(), want.end(), 1), reps_time, sample.size(),
        leaves_time);
    for (double v : nd) need = std::max(need, v);

    sic::support::progressf(
        "  representative height forecasts (log2 of the certified conjugate bound):");
    for (int r = 0; r <= s; ++r)
      sic::support::progressf(" eta_%ld=%ld", rep_entry(r),
                              (long)representative_height(estimated_steps, r));
    sic::support::progressf("\n");
    if (!std::isfinite(need) || need >= producer_options.policy.estimated_shortfall_bits) {
      sic::support::progressf("precision estimate failed (non-finite leaf radius at pass 1)\n");
      throw sic::Error(sic::ErrorKind::precision,
                       "pass-1 precision estimate exceeds computational policy");
    }
    P = std::max(first_pass_bits, (slong)std::ceil(first_pass_bits + need + 16));
    sic::support::progressf(
        "precision: %s need %.0f more bits -> working precision %ld   [%.1fs]\n", "pass-1 leaves",
        need, (long)P, stamp());
  }

  return 0;
}

}  // namespace sic::construct
