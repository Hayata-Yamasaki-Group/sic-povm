// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/runtime/resources/resource_report.hpp"

namespace sic::construct::resources {
void report(const Model& model, const Budget& budget, sic::support::resources::BitCount precision,
            int workers, const char* output) try {
  sic::support::progressf("resource preflight: metadata only; no Group/Table constructed\n");
  sic::support::progressf(
      "expected degree=%llu points=%llu reduced states=%llu maximum cycle=%llu\n",
      static_cast<unsigned long long>(model.degree), static_cast<unsigned long long>(model.points),
      static_cast<unsigned long long>(model.states),
      static_cast<unsigned long long>(model.max_cycle));
  sic::support::progressf(
      "sizeof acb=%zu arb=%zu int=%zu long=%zu; effective CPUs=%llu workers=%d\n",
      sizeof(acb_struct), sizeof(arb_struct), sizeof(Index), sizeof(long),
      static_cast<unsigned long long>(budget.initial.cpus), workers);
  sic::support::progressf(
      "memory headroom=%llu process budget=%llu bytes (20%% headroom reserve)\n",
      static_cast<unsigned long long>(budget.initial.available),
      static_cast<unsigned long long>(budget.initial_limit));
  for (const auto& constraint : budget.initial.constraints)
    sic::support::progressf("  constraint: %s\n", constraint.c_str());
  sic::support::progressf(
      "forecast bits=%llu: group setup=%llu table/fallback=%llu support=%llu bytes\n",
      static_cast<unsigned long long>(precision),
      static_cast<unsigned long long>(model.group_initial()),
      static_cast<unsigned long long>(model.quadrature(precision, workers)),
      static_cast<unsigned long long>(model.support(precision)));
  sic::support::progressf("group closure requires measured base count; current placeholder B=1\n");
  sic::support::progressf(
      "one Newton class forecast=%llu bytes; "
      "final buffers at equal precisions=%llu bytes\n",
      static_cast<unsigned long long>(model.newton_worker(precision)),
      static_cast<unsigned long long>(model.final_buffers(precision, precision)));
  sic::support::progressf(
      "full-support representatives: %llu integer-pair rows; text lower bound=%llu bytes\n",
      static_cast<unsigned long long>(model.representative_rows()),
      static_cast<unsigned long long>(mul(4, model.representative_rows())));
  if (output) {
    auto parent = std::filesystem::path(output).parent_path();
    if (parent.empty()) parent = ".";
    const Bytes free = std::filesystem::space(parent).available;
    sic::support::progressf("export filesystem available=%llu bytes at %s\n",
                            static_cast<unsigned long long>(free), parent.c_str());
    if (free < mul(4, model.representative_rows()))
      throw sic::Error(sic::ErrorKind::resource,
                       "export filesystem cannot hold even the representative text lower bound");
  } else {
    sic::support::progressf("no SIC_OUT: durable exact export is not configured\n");
  }
  sic::support::progressf(
      "forecasts are not allocation upper bounds; "
      "final precision/support, odd-step storage,\n"
      "quadrature kernel sizes, action spools and final integer lengths "
      "need stage admission.\n"
      "A successful metadata preflight does not authorize a full construction.\n");
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

}  // namespace sic::construct::resources
