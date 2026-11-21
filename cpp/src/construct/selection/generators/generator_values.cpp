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
void Construction::apply_generator_choice(int level, AnalyticGenerator& generator,
                                          const GeneratorChoice& choice, slong wp) {
  const auto* combination = std::get_if<GaloisCombination>(&choice.form);
  if (!combination) return;
  if (level >= t - 1 || ps[level] != 2 || !generator.paired || generator.imaginary ||
      generator.character || generator.periodic.size() || generator.backing ||
      generator.resident_first || generator.scalar.size() != static_cast<size_t>(Ncos[level]) ||
      combination->terms.size() != 1 || combination->terms[0].a0 != 1 ||
      combination->terms[0].a1 != 0)
    throw std::logic_error("unsupported native Galois combination");
  const auto& word = combination->terms[0].word;
  if (word.size() != group_->invf.size()) throw std::logic_error("alignment word rank");
  long tau = 0;
  for (size_t axis = 0; axis < word.size(); ++axis) {
    if (word[axis] < 0 || word[axis] >= group_->invf[axis])
      throw std::logic_error("alignment word residue");
    tau += word[axis] * group_->stride[axis];
  }
  // The signed permutation is exact on Arb balls. Certified Cg, log2U and separation
  // remain those of the certified base generator: every conjugate occurs exactly once.
  if (!choice.forecast_at) throw std::logic_error("missing Galois-combination resource forecast");
  budget.require_additional("aligned generator signed-permutation scratch",
                            choice.forecast_at(wp).scratch_bytes);
  sic::support::RealBallVector moved(Ncos[level]);
  parallel_for(Ncos[level], nth, [&](long label) {
    const long source = coset(level + 1, group_->mul(rep[level + 1][2 * label], tau));
    arb_set(moved[label], generator.scalar[source >> 1]);
    if (source & 1) arb_neg(moved[label], moved[label]);
  });
  generator.assign_scalar(std::move(moved), false);
}
}  // namespace sic::construct
