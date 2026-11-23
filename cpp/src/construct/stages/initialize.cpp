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
#include <filesystem>
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
#include "construct/output/quarantine.hpp"
#include "construct/runtime/observations/clock.hpp"
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
#include "sic/error.hpp"
#include "sic/support/flint_raii.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/container/producer_writer.hpp"
#include "support/resources.hpp"
namespace sic::construct {
int Construction::initialize() try {
  if (!producer_options.entry_arb)
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "the adapted tower requires certified Arb entries (SIC_ENTRY_ARB=1)");
  if (!producer_options.out)
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "the native producer requires SIC_OUT (the .sicx file)");
  sic::support::progressf("entry error bounds: Arb arithmetic\n");
  s = producer_options.exponent;
  prec_user = producer_options.initial_bits;
  if (prec_user < 0 || prec_user > WORD_MAX / 16)
    throw sic::Error(sic::ErrorKind::invalid_argument, "unsupported precision");
  seed = producer_options.seed;

  requested_threads = producer_options.requested_threads.get();
  nth = requested_threads;

  completed_cache_root = producer_options.completed_cache_root;
  require_completed_cache = producer_options.require_completed_cache.get();
  require_completed_cache.require_context(
      true,
      producer_options.entry_arb_one && producer_options.phase_one &&
          (!producer_options.stream_tables || producer_options.stream_tables_one),
      completed_cache_root, producer_options.group_cache, producer_options.analytic_cache);
  pinned_inputs = completed_cache_root != nullptr;
  if (completed_cache_root && memory.control() == MemoryControl::keep_all)
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "developer keep-all control is incompatible with completed-class backing");
  if (completed_cache_root) {
#if !defined(__linux__)
    throw sic::Error(sic::ErrorKind::unsupported,
                     "completed-class producer executable identity currently requires Linux");
#endif
    if (!*completed_cache_root || !producer_options.group_cache || !producer_options.analytic_cache)
      throw sic::Error(
          sic::ErrorKind::invalid_argument,
          "completed-class cache requires explicit group and coarse analytic checkpoints");
  }
  T0 = secs();

  if (producer_options.order == ProducerOptions::Order::invalid) {
    fprintf(stderr, "SIC_ORDER must be standard or maximal\n");
    return 1;
  }
  maximal_order = producer_options.order == ProducerOptions::Order::maximal;
  // Maximal order: epsilon = u^k for the positive cycle unit u (sicx_reduced_order.hpp; k = 1 is
  // the library route unchanged, k > 1 stores each cycle k times and takes points modulo u).
  ar_ = std::make_unique<Arithmetic>(s, false, producer_options.policy.dimension);

  if (maximal_order) cycle_unit = reduced_order::configure_arith((*ar_));
  unit_power = cycle_unit.k > 1;
  d = (*ar_).d;
  phase_planned = producer_options.phase.value_or(false);
  stream_actions = producer_options.stream_actions.value_or(phase_planned);
  stream_reps = producer_options.stream_reps.value_or(phase_planned);
  stream_steps = producer_options.stream_steps.value_or(stream_actions);
  compact_work = producer_options.compact_work.value_or(phase_planned);
  sic::support::progressf("d=%ld Delta=%ld h+=%ld threads=%d\n", d, (*ar_).Delta, (long)(*ar_).h(),
                          nth);
  sic::support::progressf("order: index [O:Z[epsilon]]=%ld, original discriminant=%ld\n",
                          (*ar_).order_index, (*ar_).original_Delta);
  sic::support::progressf("quadrature bounds: outward-rounded Arb panel and tail enclosures\n");
  states = 0;
  max_cycle = 0;
  for (const auto& cls : (*ar_).cls) {
    states = sic::support::resources::add(states, cls.digit.size());
    max_cycle = std::max<sic::support::resources::Count>(max_cycle, cls.digit.size());
  }
  model_ =
      std::make_unique<sic::construct::resources::Model>(d, (*ar_).h(), max_cycle, states, s + 1);
  if (maximal_order) {
    sic::support::flint::String ua(fmpq_get_str(nullptr, 10, cycle_unit.u.a.v));
    sic::support::flint::String ub(fmpq_get_str(nullptr, 10, cycle_unit.u.b.v));
    sic::support::progressf("reduced order: positive cycle unit u=%s+%s*sqrt(%ld), epsilon=u^%d\n",
                            ua.get(), ub.get(), (*ar_).Delta, cycle_unit.k);
  }
  if (unit_power) {
    // Table/class caches are keyed to the epsilon-orbit layout: refuse them explicitly.
    if (completed_cache_root || producer_options.analytic_cache)
      throw sic::Error(sic::ErrorKind::unsupported,
                       "reduced order with epsilon = u^k, k > 1: analytic/completed-class caches "
                       "are not supported");
    // Degree h d^2/(2k) with all h d^2 grid points (Model keeps points = h d^2). The epsilon-orbit
    // compact layout (OrbitTableLayout: (d^2-1)/3 slots) does not apply to u-orbits, so the full
    // Arb table is used (the full Arb layout; byte-identical exports at d8/d16 standard).
    (*model_).degree = reduced_order::degree((*ar_), cycle_unit.k);
    (*model_).compact_tables = false;
    sic::support::progressf(
        "reduced order: k=%d: each cycle stored %d times (%d visits), points modulo u, degree "
        "%llu\n",
        cycle_unit.k, cycle_unit.k, 3 * cycle_unit.k, (unsigned long long)(*model_).degree);
  }
  generator_block = producer_options.generator_block.get();
  if (!generator_block || generator_block > 1048576)
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "SIC_GENERATOR_BLOCK must be between1 and1048576 real values");
  (*model_).generator_window = generator_block;
  first_pass = producer_options.first_pass_bits.get();
  if (!first_pass || first_pass > (sic::support::resources::Count)(WORD_MAX / 16))
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "SIC_FIRST_PASS_BITS is outside supported precision range");
  initial_precision = prec_user > 0 ? prec_user : first_pass;
  budget.require("Group construction", (*model_).group_initial());
  budget.require("pass-1 table and unpaired quadrature fallback",
                 (*model_).quadrature(initial_precision + 16, nth));
  budget.require("support transforms", (*model_).support(initial_precision + 16));
  if (!phase_planned)
    budget.require("minimum-precision final live buffers",
                   (*model_).final_buffers(initial_precision + 16, initial_precision + 16));
  if (phase_planned)
    sic::support::progressf(
        "phased final admission deferred until the actual tower, precision and storage lifetimes "
        "are checked\n");

  flint_set_num_threads(nth);

  const std::filesystem::path requested_out(producer_options.out);
  const auto parent =
      requested_out.has_parent_path() ? requested_out.parent_path() : std::filesystem::path(".");
  const auto available = std::filesystem::space(parent).available;
  constexpr uint64_t kMinimumDisk = (uint64_t{1} << 30) + (uint64_t{64} << 20);
  if (available < kMinimumDisk)
    throw sic::support::resources::AdmissionError(
        "insufficient output disk before group construction");
  quarantine = std::make_unique<QuarantineOutput>(requested_out, producer_options.resume);
  quarantine_name = quarantine->path().string();
  return 0;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

}  // namespace sic::construct
