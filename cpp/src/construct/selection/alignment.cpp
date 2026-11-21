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
#include "construct/selection/alignment_selector.hpp"
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
#include "sic/error.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/container/producer_writer.hpp"
namespace sic::construct {
namespace {
float logarithm(arb_srcptr value) {
  const double midpoint = std::fabs(arf_get_d(arb_midref(value), ARF_RND_NEAR));
  return midpoint > 0 && std::isfinite(midpoint) ? static_cast<float>(std::log2(midpoint))
                                                 : -1000.0f;
}
}  // namespace

int Construction::alignment_selection() {
  if (producer_options.alignment.selector == "step-selection" ||
      components.selector->name() != kStepSelection)
    return 0;
  model_->symbolic_top = true;
  tower_shape.D0 = D0;
  if (!component_context.progress)
    component_context.progress = [](const std::string& text) {
      std::fputs(text.c_str(), stdout);
      std::fputc('\n', stdout);
    };
  tower_context.coset_counts = Ncos;
  tower_context.step_degrees = ps;
  tower_context.representative_levels.clear();
  tower_context.representative_names.clear();
  for (int r = 0; r <= s; ++r) {
    const auto& field = scalar_fields->fields[scalar_fields->representative_field[r]];
    if (field.level >= t) throw std::logic_error("alignment field lies above real tail");
    for (int channel = 0; channel < 2; ++channel) {
      tower_context.representative_levels.push_back(field.level);
      tower_context.representative_names.push_back("eta_" + std::to_string(rep_entry(r)) + "/" +
                                                   std::to_string(channel));
    }
  }
  tower_context.permutation = [this](int level, long label, std::vector<sic::support::Index>& out) {
    out.resize(Ncos[level]);
    const long tau = rep[level][label];
    for (long i = 0; i < Ncos[level]; ++i) out[i] = coset(level, group_->mul(rep[level][i], tau));
  };
  tower_context.conjugate_word = [this](unsigned level, long label) {
    return group_->coord(rep[level][label]);
  };
  tower_context.selection_live_bytes = [this](slong bits, unsigned workers) {
    auto model = *model_;
    model.kernel_workers = workers;
    return model.final_buffers(bits, bits);
  };
  tower_context.logarithms = [this](slong wp, unsigned workers, std::vector<std::vector<float>>& lg,
                                    std::vector<char>& searchable,
                                    std::vector<std::vector<float>>& representatives) {
    nth = std::min<int>(nth, workers);
    component_context.threads = nth;
    model_->kernel_workers = nth;
    T->nthreads = nth;
    flint_set_num_threads(nth);
    // Coarse inputs use the resident evaluator for both memory strategies. No final table has
    // been parked yet. This keeps storage-only flags out of candidate eligibility and scoring.
    auto sd = generator_evaluator->evaluate_all(generator_choices, wp, false);
    if (!sd.ok)
      throw sic::Error(sic::ErrorKind::construction, "alignment input generators did not certify");
    for (int i = 0; i < t - 1; ++i) {
      const auto& generator = sd.g[i];
      lg[i].resize(Ncos[i + 1]);
      sic::support::ComplexBallVector value(1);
      for (long j = 0; j < Ncos[i + 1]; ++j) {
        if (ps[i] == 2)
          lg[i][j] = logarithm(generator.real_even(j >> 1));
        else {
          generator.get(value[0], j);
          acb_abs(acb_realref(value[0]), value[0], wp);
          lg[i][j] = logarithm(acb_realref(value[0]));
        }
      }
      searchable[i] = ps[i] == 2 && generator.paired && !generator.imaginary &&
                      !generator.character && generator.periodic.size() == 0;
    }
    sic::support::RealBallVector scalar(1);
    arb_sqrt_ui(scalar[0], d + 1, wp);
    sic::support::ComplexBallVector roots = roots_2d(wp);
    EntryCtx entries;
    make_entry_ctx(*T, entries);
    for (int r = 0; r <= s; ++r) {
      const auto& field = scalar_fields->fields[scalar_fields->representative_field[r]];
      sic::support::RealBallVector channels;
      representative_channels(entries, r, wp, roots, scalar[0], sd, channels);
      for (int channel = 0; channel < 2; ++channel) {
        auto& values = representatives[2 * r + channel];
        values.resize(field.degree);
        for (long label = 0; label < field.degree; ++label)
          values[label] = logarithm(channels[channel * field.degree + label]);
      }
    }
    sd = StepData();
    entries = EntryCtx();
    roots = sic::support::ComplexBallVector();
    scalar = sic::support::RealBallVector();
    trim_heap();
  };
  components.selector =
      std::make_unique<ConjugateAlignmentSelector>(generator_choices, producer_options.alignment);
  generator_choices = components.selector->select(tower_shape, tower_context, component_context,
                                                  component_precision, component_checkpoints);
  return 0;
}
}  // namespace sic::construct
