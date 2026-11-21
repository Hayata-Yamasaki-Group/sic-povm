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
#include "sic/error.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/container/producer_writer.hpp"
#include "support/resources.hpp"
namespace sic::construct {
void Construction::adjust_memory(const char* phase, slong precision) {
  const auto per_worker = sic::support::resources::add(
      8 * sic::support::resources::kMib,
      sic::support::resources::mul(32 * d,
                                   sic::support::resources::complex_bytes(
                                       sic::support::resources::newton_matrix_bits(precision))));
  nth = memory.adjust(phase, nth, per_worker, [&] {
    direct_eta_roots.reset();
    if (direct_eta_rows) direct_eta_rows->clear();
    for (auto& generator : sdt.g) generator.park();
    if (T) {
      for (auto& entry : T->gl_cache)
        for (auto& node : entry.second) {
          arb_clear(&node.abscissa);
          arb_clear(&node.weight);
        }
      T->gl_cache.clear();
      immutable_table_owned.reset();
    }
  });
  component_context.threads = nth;
  model_->kernel_workers = nth;
  if (T) T->nthreads = nth;
  flint_set_num_threads(nth);
}

sic::support::resources::Bytes Construction::inspect_final_owners() const {
  sic::construct::resources::ResidentCensus caller;
  if (direct_eta_rows) caller.allocation(direct_eta_rows->bytes());
  caller.dynamic(inSupp);
  caller.dynamic(inA);
  caller.dynamic(sig_odd);
  caller.dynamic(product_->factors);
  caller.dynamic(product_->certified_height);
  caller.dynamic(derived);
  caller.dynamic(completed_elements);
  caller.dynamic(pending_table_backing);
  caller.dynamic(completed_inventory.backing);
  caller.allocation(
      sic::support::resources::mul(relative_certificates.capacity(), sizeof(RelativeCertificate)));
  for (const auto& certificate : relative_certificates) {
    caller.dynamic(certificate.kernel_generators);
    caller.dynamic(certificate.target_generators);
    caller.dynamic(certificate.real_ball);
    caller.dynamic(certificate.imaginary_ball);
  }
  if (inspect_newton_caller) inspect_newton_caller(caller);
  if (inspect_final_local) inspect_final_local(caller);
  const auto group = sic::construct::resources::inspect_group(*group_);
  const auto arithmetic = sic::construct::resources::inspect_arithmetic(*ar_);
  const auto layouts = sic::construct::resources::inspect_layouts(
      *group_, {analytic_layout.get(), T ? T->layout.get() : nullptr});
  sic::support::resources::Bytes table_owned = 0;
  if (T) {
    // Dense final tables are read-only after refinement. Scanning every ball at each
    // exact-item boundary repeats identical work. Backed tables keep their fresh census:
    // their physical classes can be loaded/dropped between boundaries.
    if (final_table_immutable && immutable_table_owned)
      table_owned = *immutable_table_owned;
    else {
      table_owned = sic::construct::resources::inspect_retained_table(*T).owned;
      if (final_table_immutable) {
        immutable_table_owned = table_owned;
        sic::support::progressf("memory census: immutable table captured bytes=%llu\n",
                                (unsigned long long)table_owned);
      }
    }
  }
  return sic::support::resources::add(
      caller.storage.bytes(),
      sic::support::resources::add(
          group.bytes(),
          sic::support::resources::add(
              arithmetic.bytes(), sic::support::resources::add(layouts.bytes(), table_owned))));
}

std::array<MemoryPlanner::PhaseReservation, 5> Construction::project_final(
    const sic::construct::resources::Model& model, slong precision, slong decoding,
    sic::support::resources::Bytes rss, sic::support::resources::Bytes owned) const {
  using sic::support::resources::add;
  using sic::support::resources::Bytes;
  using sic::support::resources::mul;
  std::array<Bytes, 5> structural;
  if (model.compact_tables)
    structural = model.tower_phase_forecasts(precision, decoding);
  else
    structural.fill(model.final_buffers(precision, decoding));
  const Bytes real = sic::support::resources::real_bytes(precision),
              complex = sic::support::resources::complex_bytes(precision);
  // Credits match only owners already present in this particular projected phase.
  // Everything else, including native worker scratch, remains a future obligation.
  Bytes generators = 0, descriptors = 0, coefficients = 0;
  if (final_generator_precision >= precision) {
    for (size_t i = 0; i < sdt.g.size(); ++i) {
      const auto& g = sdt.g[i];
      sic::construct::resources::ResidentCensus census;
      if (i + 1 == ps.size() && model.symbolic_top && g.character) {
        census.dynamic(g.root);
        generators = add(generators, std::min(complex, census.storage.payload));
      } else if (g.periodic.size()) {
        census.dynamic(g.periodic);
        descriptors =
            add(descriptors, std::min(mul(g.periodic.size(), real), census.storage.payload));
      } else if (ps[i] != 2 || !model.phased_final) {
        census.dynamic(g.full);
        census.dynamic(g.scalar);
        const Bytes expected =
            ps[i] == 2 ? mul(Ncos[i], real) : mul(Ncos[i + 1], add(real, sizeof(arb_struct)));
        generators = add(generators, std::min(expected, census.storage.payload));
      }
    }
    for (size_t i = 0; i < sdt.fc.size(); ++i)
      if (ps[i] != 2) {
        sic::construct::resources::ResidentCensus census;
        census.dynamic(sdt.fc[i]);
        coefficients =
            add(coefficients, std::min(mul(Ncos[i + 1], complex), census.storage.payload));
      }
  }
  descriptors = std::min(descriptors, mul(model.descriptor_rows, real));
  Bytes table = 0;
  if (T && !table_parked && !model.phased_final && T->prec >= precision && T->layout &&
      T->packed.size() == static_cast<size_t>(ar_->h()) &&
      std::all_of(T->packed.begin(), T->packed.end(),
                  [&](const auto& v) { return v.size() == static_cast<size_t>(T->layout->slots); }))
    table = model.table(precision);  // One complete immutable copy, never the upcoming second copy.
  std::array<MemoryPlanner::PhaseReservation, 5> result;
  for (unsigned i = 0; i < result.size(); ++i) {
    Bytes present = add(model.group_resident(), add(generators, descriptors));
    if (!model.phased_final) present = add(present, table);
    if (i == 1) present = add(present, coefficients);
    if (final_local_credit) present = add(present, final_local_credit(i, decoding));
    result[i] = {structural[i], present, owned, rss, memory.retained()};
  }
  return result;
}

void Construction::reserve_final(const char* phase, unsigned first_phase, slong precision,
                                 slong decoding) {
  adjust_memory(phase, std::max(precision, decoding));
  const auto rss = sic::support::resources::inspect().rss, owned = inspect_final_owners();
  std::array<MemoryPlanner::PhaseReservation, 5> projections;
  sic::support::resources::Bytes total = 0;
  for (;;) {
    projections = project_final(*model_, precision, decoding, rss, owned);
    total = 0;
    for (unsigned i = first_phase; i < projections.size(); ++i)
      total = std::max(total, projections[i].total());
    if (total <= budget.limit() || nth == 1) break;
    nth = std::max(1, nth / 2);
    model_->kernel_workers = nth;
  }
  component_context.threads = nth;
  if (T) T->nthreads = nth;
  flint_set_num_threads(nth);
  for (unsigned i = first_phase; i < projections.size(); ++i) {
    const auto& p = projections[i];
    sic::support::progressf(
        "memory phase admission: boundary=%s phase=%u structural=%llu present=%llu owned=%llu "
        "rss=%llu unknown=%llu future=%llu committed=%llu total=%llu workers=%d\n",
        phase, i, (unsigned long long)p.structural, (unsigned long long)p.present,
        (unsigned long long)p.owned, (unsigned long long)p.rss, (unsigned long long)p.unknown(),
        (unsigned long long)p.future(), (unsigned long long)p.committed,
        (unsigned long long)p.total(), nth);
  }
  memory.reserve(total);
}

namespace {
std::pair<int, int> representative_band_range(const ProducerOptions& options, bool phased) {
#ifdef SIC_ENABLE_TEST_HOOKS
  if (const char* configured = options.raw("SIC_TEST_REP_BAND_BITS")) {
    const auto bits = sic::support::resources::integer(configured);
    if (bits > 8 || (bits && !phased))
      throw sic::Error(sic::ErrorKind::invalid_argument,
                       "SIC_TEST_REP_BAND_BITS requires phased channels and 0..8");
    return {static_cast<int>(bits), static_cast<int>(bits)};
  }
#else
  (void)options;
  (void)phased;
#endif
  return {0, 8};
}
}  // namespace

void Construction::plan_final(slong precision) {
  memory.reset();
  model_->phased_representative_rows = 0;
  model_->band_generator_rows = 0;
  model_->phased_final = false;
  const auto decoding = model_->decoder_hint ? model_->decoder_hint : precision;
  const auto rss = sic::support::resources::inspect().rss, owned = inspect_final_owners();
  const auto forecast = [&](const sic::construct::resources::Model& model) {
    sic::support::resources::Bytes total = 0;
    for (const auto& phase : project_final(model, precision, decoding, rss, owned))
      total = std::max(total, phase.total());
    return total;
  };
  const auto dense = forecast(*model_);
  // Keeping the complete analytic set avoids serialization on small runs. At larger sizes
  // the compact backend may park classes and real generators independently.
  phased_storage = false;
  const auto capabilities = components.registry.capabilities(generator_choices);
  if (model_->compact_tables && capabilities.bounded && capabilities.replayable) {
    model_->phased_final = true;
    const auto bounded = forecast(*model_);
    // Reserve a further quarter of the cap for future recognized leaves when considering
    // a complete analytic table. If the dense whole-phase forecast fits, retaining it is faster.
    const auto mandatory = std::min(dense, bounded);
    const auto extra = dense > mandatory ? dense - mandatory : 0;
    const auto available =
        budget.limit() / 4 * 3 > mandatory ? budget.limit() / 4 * 3 - mandatory : 0;
    const auto choice = MemoryPlanner::choose(memory.control(), {extra, true, false}, available, 1);
    phased_storage = choice == Retention::spill;
    sic::support::progressf("memory item: analytic table plan=%s bytes=%llu mandatory=%llu\n",
                            MemoryPlanner::label(choice), (unsigned long long)extra,
                            (unsigned long long)mandatory);
  }
  if (completed_cache_root) {
    if (!capabilities.bounded || !capabilities.replayable)
      throw sic::Error(sic::ErrorKind::unsupported,
                       "components cannot use completed-class backing");
    phased_storage = true;
  }
  stream_tables = phased_storage;
  model_->phased_final = phased_storage;
  requested_band_bits = 0;
  model_->phased_representative_rows = 0;
  const auto [first_band, last_band] = representative_band_range(producer_options, phased_storage);
  if (phased_storage) {
    for (int bits = first_band; bits <= last_band; ++bits) {
      requested_band_bits = bits;
      sic::support::resources::Count rows = 0;
      model_->band_generator_rows = 0;
      for (int r = 0; r <= s; ++r) {
        const auto& field = scalar_fields->fields[scalar_fields->representative_field[r]];
        const int actual_bits = representative_band_bits(r);
        rows = std::max<sic::support::resources::Count>(rows, (2 * field.degree) >> actual_bits);
        sic::support::resources::Count reload_rows = 0;
        for (int i = field.level - actual_bits; i < field.level; ++i)
          reload_rows = sic::support::resources::add(reload_rows, Ncos[i]);
        model_->band_generator_rows = std::max(model_->band_generator_rows, reload_rows);
      }
      model_->phased_representative_rows = rows;
      if (forecast(*model_) <= budget.limit() / 10 * 8) break;
    }
  }
  nth = memory.phase_workers(nth, [&](int workers) {
    auto proposed = *model_;
    proposed.kernel_workers = workers;
    return forecast(proposed);
  });
  model_->kernel_workers = nth;
  component_context.threads = nth;
  if (T) T->nthreads = nth;
  flint_set_num_threads(nth);
  if (model_->compact_tables) {
    static constexpr const char* kPhaseNames[] = {"generators", "step recognition", "actions",
                                                  "physical decoder", "representatives"};
    const auto phases = project_final(*model_, precision, decoding, rss, owned);
    for (size_t i = 0; i < phases.size(); ++i)
      sic::support::progressf("memory phase forecast: %s bytes=%llu workers=%d\n", kPhaseNames[i],
                              (unsigned long long)phases[i].total(), nth);
  }
  sic::support::progressf(
      "memory final plan: bounded=%d precision=%ld decoding=%llu dense=%llu forecast=%llu "
      "budget=%llu band_bits=%d\n",
      phased_storage, (long)precision, (unsigned long long)decoding, (unsigned long long)dense,
      (unsigned long long)forecast(*model_), (unsigned long long)budget.limit(),
      requested_band_bits);
}
}  // namespace sic::construct

namespace sic::construct {
std::function<void(sic::construct::resources::ResidentCensus&)> Construction::with_local(
    std::function<void(sic::construct::resources::ResidentCensus&)> extra) {
  auto previous = inspect_final_local;
  return std::function<void(sic::construct::resources::ResidentCensus&)>(
      [previous, extra](auto& census) {
        if (previous) previous(census);
        extra(census);
      });
}
}  // namespace sic::construct

namespace sic::construct {
int Construction::representative_band_bits(int r) {
  const auto& field = scalar_fields->fields[scalar_fields->representative_field[r]];
  for (int bits = std::min(requested_band_bits, field.level); bits > 0; --bits)
    for (const auto& prefix : scalar_fields->fields)
      if (prefix.level == field.level - bits && field.degree / prefix.degree == (1L << bits) &&
          std::all_of(ps.begin() + prefix.level, ps.begin() + field.level,
                      [](long p) { return p == 2; }))
        return bits;
  return 0;
}

}  // namespace sic::construct
