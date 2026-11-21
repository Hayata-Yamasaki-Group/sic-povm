// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/runtime/resources/direct_rows.hpp"

#include <algorithm>

#include "construct/model/session_state.hpp"
#include "support/resources.hpp"

namespace sic::construct {
void Construction::prepare_direct_rows(sic::support::resources::Bytes workspace, slong precision) {
  using sic::support::resources::add;
  using sic::support::resources::Bytes;
  if (!direct_eta_rows_planned) {
    direct_eta_rows_planned = true;
    const Bytes mandatory = std::max(memory.reserved(), add(budget.initial.rss, workspace));
    const Bytes current = add(sic::support::resources::inspect().rss, workspace);
    const Bytes protected_bytes = std::max(mandatory, current);
    const Bytes cap = budget.limit();
    // A bounded optional window leaves three quarters of current uncommitted
    // headroom for subsequent exact leaves. It is charged once for this attempt,
    // even if pressure later evicts all rows. No release can undercharge a phase.
    const size_t domains = std::count_if(table_backing.begin(), table_backing.end(),
                                         [](const auto& backing) { return bool(backing.ghost); });
    // Small constructors otherwise retain rows past the peak refinement RSS.
    // Class quotas preserve useful returning rows even under this smaller cap.
    const Bytes ceiling =
        d <= 256 ? 256 * sic::support::resources::kMib : sic::support::resources::kGib;
    const Bytes desired = std::min(ceiling, DirectEtaRows::window_bytes(d, domains, precision));
    const Bytes window = std::min(desired, cap > protected_bytes ? (cap - protected_bytes) / 4 : 0);
    if (window && memory.decide("direct extraction row window", {window, false, true},
                                protected_bytes, {}, true) == Retention::keep)
      direct_eta_rows = std::make_unique<DirectEtaRows>(window, domains);
    sic::support::progressf("direct row window: forecast=%llu admitted=%llu precision=%ld\n",
                            (unsigned long long)desired,
                            (unsigned long long)(direct_eta_rows ? direct_eta_rows->capacity() : 0),
                            (long)precision);
  }
  resources::admit_direct_rows(direct_eta_rows, budget, workspace);
}
}  // namespace sic::construct

namespace sic::construct::resources {
void admit_direct_rows(std::unique_ptr<sic::exact::DirectEtaRows>& rows,
                       const sic::support::resources::Budget& budget,
                       sic::support::resources::Bytes workspace) {
  try {
    // Charge the entire window afresh. Row charges include padding and cannot
    // be used as actual resident credit when eviction replaces small owners by
    // larger ones. The deliberate RSS overlap also protects pressure refills.
    budget.require_additional("direct representative entries and row window",
                              sic::support::resources::add(workspace, rows ? rows->capacity() : 0));
  } catch (const sic::support::resources::AdmissionError&) {
    // Reclaim and disable optional rows for this attempt before refusing direct
    // extraction. Refill must never use a stale admission after pressure.
    rows.reset();
    sic::support::resources::release_free_heap();
    budget.require_additional("direct representative entries after row eviction", workspace);
  }
}
}  // namespace sic::construct::resources
