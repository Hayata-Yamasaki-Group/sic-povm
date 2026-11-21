// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/model/session_state.hpp"
#include "construct/runtime/resources/resident_memory.hpp"
#include "construct/stages/table/newton_admission.hpp"
namespace sic::construct {
NewtonCachePlan Construction::newton_cache_plan(slong precision, bool stream) {
  if (require_completed_cache.enabled) {
    if (!completed_cache || !stream || completed_precision != precision + 16)
      throw std::runtime_error(
          "cache-only consumer requires a validated current-precision inventory before Newton "
          "allocation");
    require_completed_cache.enforce(precision + 16, completed_inventory.hits,
                                    completed_inventory.missing,
                                    std::count(derived.begin(), derived.end(), 0));
  }
  initialize_pending(stream, precision + 16);
  if (stream) {
    if (!analytic_layout)
      throw sic::Error(sic::ErrorKind::invalid_argument,
                       "streamed Newton requires compact Arb tables");
  }
  const size_t reused =
      std::count_if(pending_table_backing.begin(), pending_table_backing.end(),
                    [](const TableClassBacking& backing) { return bool(backing); });
  const size_t physical = std::count(derived.begin(), derived.end(), 0);
  if (completed_cache && completed_precision == precision + 16 && reused == physical) {
    budget.require_additional(
        "completed-only Newton table controls",
        sic::construct::resources::newton_future_controls(d, (*ar_).h(), T->nstates()));
    auto table = std::make_unique<Table>((*ar_), precision + 16, nth);
    configure_table(*table);
    table->alloc_values();
    if (std::count(derived.begin(), derived.end(), 1)) derive_classes(*table);
    sic::support::progressf(
        "table: reused %zu completed classes to %ld bits; no Newton worker admitted\n", reused,
        (long)precision + 16);
    return {reused, physical, std::move(table)};
  }
  return {reused, physical, {}};
}
}  // namespace sic::construct
