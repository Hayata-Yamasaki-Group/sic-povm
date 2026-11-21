// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/representative_leaves.hpp"

#ifdef SIC_ENABLE_TEST_HOOKS
#include <sys/resource.h>

#include "construct/runtime/resources/resident_memory.hpp"
#endif

namespace sic::construct {
#ifdef SIC_ENABLE_TEST_HOOKS
void Construction::trace_representative_owners(const char* stage, int r,
                                               const RepresentativeLeaves& state) {
  // Explicit progress enables this expensive test-build observation. Ordinary
  // conformance runs keep their original scan cost; measurements use --progress.
  if (!producer_options.progress || !producer_options.memory_owners) return;
  const auto emit = [&](const char* name, const auto& inspect) {
    sic::construct::resources::ResidentCensus census;
    inspect(census);
    sic::support::progressf(
        "output owner: stage=%s eta=%ld owner=%s payload=%llu allowance=%llu bytes=%llu\n", stage,
        rep_entry(r), name, (unsigned long long)census.storage.payload,
        (unsigned long long)census.storage.allowance, (unsigned long long)census.storage.bytes());
  };
  emit("forward-generators", [&](auto& c) { c.dynamic(sdt.g); });
  emit("step-exact", [&](auto& c) {
    c.dynamic(ilv);
    for (const auto& source : item_sources) source.resident_metadata(c);
  });
  emit("channels", [&](auto& c) { c.dynamic(state.y); });
  emit("channel-scratch", [&](auto& c) { c.dynamic(state.yb); });
  emit("current-exact", [&](auto& c) { c.dynamic(state.lv); });
  emit("kept-exact", [&](auto& c) {
    for (const auto& item : state.kept) {
      c.dynamic(item.lv);
      item.source.resident_metadata(c);
      if (item.spool) item.spool->resident_metadata(c);
    }
  });
  emit("physical-duals", [&](auto& c) {
    c.dynamic(state.dec.h2w);
    c.dynamic(state.dec.Bo);
    c.dynamic(state.dec.top_dual);
    c.dynamic(state.dec.labels);
    c.dynamic(state.dec.odd_labels);
  });
  emit("transported", [&](auto& c) {
    c.dynamic(state.v);
    c.dynamic(state.vb);
  });
  emit("valuations", [&](auto& c) { c.allocation(state.collector.resident_valuation_bytes()); });
  const auto table = T ? sic::construct::resources::inspect_retained_table(*T).owned : 0;
  struct rusage usage {};
  getrusage(RUSAGE_SELF, &usage);
  sic::support::progressf(
      "output census: stage=%s eta=%ld table=%llu owned=%llu rss=%llu hwm_kib=%ld\n", stage,
      rep_entry(r), (unsigned long long)table, (unsigned long long)inspect_final_owners(),
      (unsigned long long)sic::support::resources::inspect().rss, usage.ru_maxrss);
}
#endif
}  // namespace sic::construct
