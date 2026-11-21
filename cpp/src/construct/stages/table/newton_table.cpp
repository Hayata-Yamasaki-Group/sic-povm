// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/model/session_state.hpp"
#include "construct/model/worker_policy.hpp"
#include "construct/runtime/resources/construction_model.hpp"
#include "construct/runtime/resources/resident_memory.hpp"
#include "construct/stages/table/analytic_table.hpp"
#include "construct/stages/table/class_refinement.hpp"
#include "construct/stages/table/class_schedule.hpp"
#include "construct/stages/table/newton_admission.hpp"
#include "construct/stages/table/newton_diagnostics.hpp"
#include "construct/stages/table/newton_seed.hpp"
#include "exact/runtime/newton_class_memory.hpp"
#include "exact/runtime/newton_memory_admission.hpp"
#include "sic/error.hpp"
#include "support/admitted_worker_team.hpp"
#include "support/resources/platform_snapshot.hpp"
namespace sic::construct {
std::unique_ptr<Table> Construction::newton_table(slong precision,
                                                  sic::support::resources::Bytes retained_labels,
                                                  bool stream) {
  auto cache = newton_cache_plan(precision, stream);
  const auto reused = cache.reused, physical = cache.physical;
  if (cache.table) return std::move(cache.table);
  sic::support::release_local_worker_teams();
#if defined(__GLIBC__)
  sic::support::resources::release_free_heap();
#endif
  const auto matrix_hint = producer_options.matrix_bits.get().value_or(
      stream ? std::min<sic::support::resources::BitCount>(precision + 16, 512)
             : (sic::support::resources::BitCount)(precision + 16));
  if (matrix_hint < 64 || matrix_hint > (sic::support::resources::BitCount)(WORD_MAX / 16))
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "SIC_NEWTON_MATRIX_BITS is outside the supported range");
  const bool modular_reservation = true;
  const bool inverse_reservation = true;
  const bool product_reservation = true;
  const bool reconstruction_reservation = true;
  const bool memory_audit = producer_options.memory_audit;
  const bool memory_owners = producer_options.memory_owners && stream && analytic_layout;
  if (memory_owners && (!stream || !analytic_layout))
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "owned Newton callbacks require streamed compact tables");
  const bool phase_admission = producer_options.phase_admission && memory_owners;
  if (phase_admission && !memory_owners)
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "phase Newton admission requires SIC_NEWTON_MEMORY_OWNERS=1");
  const auto resident_plan = newton_resident_plan(precision, retained_labels, stream);
  const auto resident = resident_plan.resident;
  const auto future_controls = resident_plan.future_controls;
  phase_memory::CopySource coarse_source;
  if (phase_admission) {
    coarse_source = resident_plan.table.source;
    if (!resident_plan.table.finite_lagom || !coarse_source.finite_lagom) {
      sic::support::progressf(
          "Newton coarse source storage unsupported; separately admitted fallback required\n");
      return nullptr;
    }
  }
  const auto output_bytes =
      stream ? sic::support::resources::mul((d * d - 1) / 3,
                                            sic::support::resources::complex_bytes(precision + 16))
             : 0;
  const phase_memory::Publication publication{output_bytes, nth, false};
  auto phase_options = [&](int workers, bool correction) {
    phase_memory::Options options;
    options.workers = workers;
    options.modular = modular_reservation;
    options.column_inverse = inverse_reservation;
    options.threaded_product = product_reservation;
    options.refined_correction = correction;
    options.parallel_reconstruction = reconstruction_reservation;
    options.reference = false;
    options.native_scalar_bytes = sizeof(SelectionWide);
    options.native_complex_bytes = sizeof(GhostRefiner::ComplexBall<SelectionWide>);
    return options;
  };
  auto phase_plan = [&](int workers, bool correction) {
    return phase_memory::requirement(
        phase_memory::propose(d, precision + 16, matrix_hint, coarse_source,
                              phase_options(workers, correction)),
        publication);
  };
  auto kernel_share = [&](sic::support::Index classes) {
    int share = static_cast<int>(std::max<sic::support::Index>(1, nth / classes));
    if (const auto& selection = producer_options.selection_threads.get(); selection)
      share = std::min(share, *selection);
    return share;
  };
  auto worker_bytes =
      phase_admission ? sic::support::resources::Bytes(0)
                      : (*model_).newton_worker(precision + 16, false, matrix_hint,
                                                modular_reservation, nth, inverse_reservation,
                                                product_reservation, reconstruction_reservation);
  const auto base_worker_bytes = worker_bytes;
  const auto seed_bytes = sic::support::resources::add(
      64 * sic::support::resources::kMib,
      sic::support::resources::mul(analytic_layout ? (d * d - 1) / 3 : d * d,
                                   sic::support::resources::complex_bytes(precision + 16)));
  int effective_correction_passes = static_cast<int>(kCorrectionRequested);
  if (effective_correction_passes && !phase_admission) {
    try {
      const auto expanded = (*model_).newton_worker(
          precision + 16, false, matrix_hint, modular_reservation, nth, inverse_reservation,
          product_reservation, reconstruction_reservation, true);
      budget.workers(1, resident,
                     sic::support::resources::add(
                         expanded, sic::support::resources::add(output_bytes, seed_bytes)));
      worker_bytes = expanded;
    } catch (const sic::support::resources::AdmissionError& error) {
      effective_correction_passes = 0;
      sic::support::progressf(
          "Newton correction refinement not admitted: %s; retaining original correction\n",
          error.what());
    } catch (const std::overflow_error&) {
      effective_correction_passes = 0;
      sic::support::progressf(
          "Newton correction refinement forecast overflow; retaining original correction\n");
    }
  }
  const auto class_threads = producer_options.class_threads.get();
  const int class_requested =
      newton_class_workers(class_threads, nth, static_cast<int>(physical - reused));
  int class_nth;
  try {
    auto admit_workers = [&] {
      if (phase_admission) {
        for (int workers = std::min(class_requested, nth); workers > 0; --workers) {
          const auto plan = phase_plan(kernel_share(workers), effective_correction_passes > 0);
          try {
            if (budget.workers(workers, resident,
                               sic::support::resources::add(plan.callback_peak, seed_bytes)) >=
                workers) {
              worker_bytes = plan.private_peak;
              return workers;
            }
          } catch (const sic::support::resources::AdmissionError&) {
            if (workers == 1) throw;
          }
        }
        throw sic::support::resources::AdmissionError("no Newton phase worker fits");
      }
      return budget.workers(
          std::min(class_requested, nth), resident,
          sic::support::resources::add(worker_bytes,
                                       sic::support::resources::add(output_bytes, seed_bytes)));
    };
    try {
      class_nth = admit_workers();
    } catch (const sic::support::resources::AdmissionError& error) {
      if (!effective_correction_passes) throw;
      effective_correction_passes = 0;
      worker_bytes = base_worker_bytes;
      sic::support::progressf(
          "Newton correction refinement lost fresh admission: %s; retrying original correction "
          "workers\n",
          error.what());
      class_nth = admit_workers();
    }
  } catch (const sic::support::resources::AdmissionError& error) {
    sic::support::progressf(
        "Newton accelerator unavailable: %s; quadrature fallback requires separate admission\n",
        error.what());
    return nullptr;
  } catch (const std::overflow_error&) {
    sic::support::progressf(
        "Newton worker forecast unrepresentable; quadrature fallback requires separate "
        "admission\n");
    return nullptr;
  }
  sic::support::progressf(
      "Newton workers: %d class, %d kernel (memory/CPU admission, matrix proposal %llu bits%s)\n",
      class_nth, nth, (unsigned long long)matrix_hint, stream ? ", completed classes backed" : "");
  std::unique_ptr<sic::support::Reservations> class_memory;
  if (memory_owners) {
    try {
      const auto cap = budget.limit();
      if (resident >= cap)
        throw sic::support::resources::AdmissionError("no owned Newton class capacity");
      class_memory = std::make_unique<sic::support::Reservations>(
          cap - resident, [owned_budget = budget, resident,
                           future_controls](sic::support::resources::Bytes aggregate) {
            try {
              owned_budget.require("owned Newton resident obligations",
                                   sic::support::resources::add(resident, aggregate));
              owned_budget.require_additional(
                  "owned Newton obligations",
                  sic::support::resources::add(aggregate, future_controls));
              return true;
            } catch (const sic::support::resources::AdmissionError&) {
              return false;
            }
          });
      sic::support::progressf(
          "Newton callback owners: enabled capacity=%llu callback_slots=%d; seed/work grants "
          "separate\n",
          (unsigned long long)(cap - resident), (int)!phase_admission);
    } catch (const sic::support::resources::AdmissionError& error) {
      sic::support::progressf(
          "Newton owner pool unavailable: %s; separately admitted fallback required\n",
          error.what());
      return nullptr;
    }
  }
  // Newton refinement of one conjugate ghost per class, from the pass-1 table (certified by
  // Krawczyk)
  auto Tn = std::make_unique<Table>((*ar_), precision + 16, nth);
  configure_table(*Tn);
  Tn->alloc_values();
  const slong target = precision + 16;
  const sic::support::Index x0 = (*group_).orbit[(*group_).pidx(0, 0, 1)];
  std::vector<long> sig = completed_elements;
  if (sig.empty()) {
    sig.assign((*ar_).h(), -1);
    for (long g = 0; g < N; ++g) {
      sic::support::Index c = (*group_).act(g, x0) / (d * d);
      if (sig[c] < 0) sig[c] = g;
    }
  }
  std::vector<char> ok((*ar_).h(), 0), used_quadrature((*ar_).h(), 0);
  for (sic::support::Index c = 0; c < (*ar_).h(); ++c)
    if (derived[c] || (stream && pending_table_backing[c])) ok[c] = 1;
  std::vector<std::string> why((*ar_).h());
  std::vector<std::unique_ptr<CertifiedGhostRoot>> completed_roots((*ar_).h());
  const bool root_backing = stream && producer_options.root_backing.value_or(true);
  std::vector<double> cond((*ar_).h(), 0), ratio((*ar_).h(), 0);
  std::vector<std::vector<long>> Shint(
      (*ar_).h());  // subsystem of a failed attempt, reused on the retry
  std::vector<GhostRefiner::SelectionKind> selection_kinds((*ar_).h(),
                                                           GhostRefiner::SelectionKind::native);
  std::vector<slong> inverse_hints((*ar_).h(), 0);
  std::vector<char> seedfail((*ar_).h(), 0);  // the seed was too coarse (B0 test failed or skipped)
  std::vector<phase_memory::ClassOutcome> outcomes((*ar_).h(),
                                                   phase_memory::ClassOutcome::numerical_failure);
  std::vector<sic::support::resources::Bytes> private_requests((*ar_).h(), 0);
  std::vector<std::optional<phase_memory::Ceilings>> retained_memory((*ar_).h());
  if (phase_admission) {
    const auto plan = phase_plan(kernel_share(class_nth), effective_correction_passes > 0);
    for (auto& ceilings : retained_memory) ceilings = plan.ceilings;
    sic::support::progressf(
        "Newton phase admission: source_bits=%llu source_storage=%llu private_slot=%llu "
        "callback_slot=%llu kernel_share=%d; retained ceilings precede allocations\n",
        (unsigned long long)plan.ceilings.source_bits,
        (unsigned long long)plan.ceilings.source_storage_bytes,
        (unsigned long long)plan.private_peak, (unsigned long long)plan.callback_peak,
        kernel_share(class_nth));
  }
#ifdef SIC_ENABLE_TEST_HOOKS
  std::vector<char> injected_defer((*ar_).h(), 0), injected_entry((*ar_).h(), 0);
#endif
  std::vector<unsigned> seed_generation((*ar_).h(), 0);
#ifdef SIC_ENABLE_TEST_HOOKS
  const char* defer_option = producer_options.raw("SIC_TEST_NEWTON_DEFER_CLASS");
  const auto defer_class = producer_options.defer_class.get();
  if (defer_option && (!memory_owners || defer_class >= (sic::support::resources::Count)(*ar_).h()))
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "invalid owned Newton resource-deferral fixture");
  const bool defer_entry = producer_options.defer_entry;
#endif
  auto callback_bytes = [&](long c, sic::support::resources::Bytes private_slot) {
    if (phase_admission)
      return std::max(phase_memory::requirement(*retained_memory[c], publication).callback_peak,
                      sic::support::resources::add(private_slot, output_bytes));
    return phase_memory::callback_workspace(d, private_slot, output_bytes, nth, false);
  };
  auto attempt_bytes = [&](long c, const Table& source, bool check_seed,
                           sic::support::resources::Bytes private_slot) {
    if (source.prec >= target) {
      if (check_seed) return sic::support::resources::Bytes(64 * sic::support::resources::kMib);
      // The target-precision shortcut has no expanded source/Newton grids.
      // Its packed output and bounded read/serialization scalars stay owned.
      if (phase_admission)
        return phase_memory::shortcut_workspace(*retained_memory[c], output_bytes);
      const auto scalar = sic::support::resources::complex_bytes(source.prec + 256);
      return sic::support::resources::add(
          output_bytes, sic::support::resources::add(128 * sic::support::resources::kMib,
                                                     sic::support::resources::mul(32, scalar)));
    }
    return callback_bytes(c, private_slot);
  };
  // one class: seed values from table Ts, refinement, and the refined values written into Tn
  std::vector<int> class_counts((*ar_).h(), class_nth);
  ClassRefinement refinement{phase_admission,
                             memory_audit,
                             root_backing,
                             target,
                             matrix_hint,
                             effective_correction_passes,
                             class_counts,
                             publication,
                             retained_memory,
                             Shint,
                             cond,
                             ratio,
                             selection_kinds,
                             inverse_hints,
                             ok,
                             seedfail,
                             used_quadrature,
                             why,
                             outcomes,
                             private_requests,
                             Tn,
                             completed_roots,
                             sig,
                             precision
#ifdef SIC_ENABLE_TEST_HOOKS
                             ,
                             defer_class,
                             injected_defer,
                             injected_entry,
                             defer_entry
#endif
  };

  auto refine_class = [&](long c, const Table& Ts, bool check_seed = false,
                          sic::support::resources::Bytes private_slot = 0,
                          std::optional<sic::support::Reservations::Lease> grant = {}) {
    private_slot = std::max(private_slot, worker_bytes);
    if (phase_admission)
      private_slot = std::max(
          private_slot, phase_memory::requirement(*retained_memory[c], publication).private_peak);
    if (memory_owners && !grant) {
      try {
        grant = class_memory->try_acquire(attempt_bytes(c, Ts, check_seed, private_slot));
      } catch (const std::overflow_error&) {
        private_slot = UINT64_MAX;
      }
      if (!grant) {
        outcomes[c] = phase_memory::ClassOutcome::resource_deferred;
        private_requests[c] = private_slot;
        seedfail[c] = 0;
        why[c] = "owned callback grant refused";
        return;
      }
    }
    phase_memory::CallbackRollback rollback([&, c] {
      if (stream && !check_seed) Tn->packed[c] = sic::support::ComplexBallVector();
      if (!check_seed) completed_roots[c].reset();
    });
    outcomes[c] = phase_memory::ClassOutcome::in_progress;
    private_requests[c] = 0;
    if (memory_owners && !check_seed && Ts.prec >= target && producer_options.progress)
      sic::support::progressf(
          "Newton owned target shortcut: class=%ld source=%ld target=%ld callback_grant=%llu\n", c,
          (long)Ts.prec, (long)target, (unsigned long long)grant->bytes());
    const double started = stamp();
    if (!check_seed && producer_options.progress)
      sic::support::progressf(
          "Newton class start: class=%ld seed_bits=%ld target_bits=%ld [%.1fs]\n", c, (long)Ts.prec,
          (long)target, stamp());
    refine_class_data(refinement, c, Ts, check_seed, private_slot,
                      grant ? grant->bytes() : UINT64_MAX);
    sic::construct::resources::require_class_controls(Shint[c], why[c], {});
    if (check_seed) return;
    if (!stream) {
      if (ok[c]) outcomes[c] = phase_memory::ClassOutcome::complete;
      return;
    }
    if (ok[c]) {
      if (grant) {
        auto publication_bytes =
            sic::support::resources::add(output_bytes, 128 * sic::support::resources::kMib);
        if (completed_roots[c]) {
          const auto root_storage = phase_memory::inspect_storage(completed_roots[c]->state.v);
          if (phase_admission &&
              (!root_storage.finite_lagom ||
               root_storage.bytes() >
                   sic::support::resources::mul(
                       2 * d - 2,
                       sic::support::resources::complex_bytes(
                           sic::support::resources::add(retained_memory[c]->high_bits, 128))))) {
            ok[c] = 0;
            outcomes[c] = phase_memory::ClassOutcome::resource_deferred;
            private_requests[c] = 0;
            seedfail[c] = 0;
            why[c] = "unsupported retained root storage before publication";
            return;
          }
          publication_bytes = sic::support::resources::add(
              publication_bytes, sic::support::resources::mul(8, root_storage.bytes()));
        }
        if (publication_bytes > grant->bytes())
          throw std::logic_error("callback forecast omitted live publication owners");
      }
      ok[c] = 0;  // Complete only after successful parking/publication.
      park_completed_class(*Tn, c, std::move(completed_roots[c]), sig[c],
                           used_quadrature[c] ? "quadrature" : "newton");
      sic::construct::resources::require_class_controls(
          Shint[c], why[c],
          {&pending_table_backing[c], completed_cache ? &completed_inventory.backing[c] : nullptr});
      ok[c] = 1;
      outcomes[c] = phase_memory::ClassOutcome::complete;
    } else
      Tn->packed[c] = sic::support::ComplexBallVector();
    if (memory_owners && outcomes[c] == phase_memory::ClassOutcome::resource_deferred)
      sic::support::progressf(
          "Newton callback deferred: class=%ld seed_bits=%ld seed_generation=%u "
          "private_required=%llu hint_rows=%zu; prepared seed retained for serial drain\n",
          c, (long)Ts.prec, seed_generation[c], (unsigned long long)private_requests[c],
          Shint[c].size());
    if (producer_options.progress)
      sic::support::progressf(
          "Newton class done: class=%ld certified=%d elapsed=%.2fs RSS=%.1f MiB reason=%s "
          "[%.1fs]\n",
          c, (int)ok[c], stamp() - started,
          sic::support::resources::inspect().rss / (double)sic::support::resources::kMib,
          why[c].c_str(), stamp());
  };
  // computed classes in parallel (dynamic scheduling); the derived ones follow by conjugation
  std::vector<long> todo;
  for (long c = 0; c < (*ar_).h(); ++c)
    if (!ok[c]) todo.push_back(c);
  std::vector<slong> cur_prec((*ar_).h(), T->prec);
  auto b0fail = [&](long c) { return !ok[c] && seedfail[c] && cur_prec[c] < target; };
  // next seed precision: the B0 failure ratio (log2) scales like the seed radius; a non-finite or
  // huge ratio (box too wide for the Jacobian enclosure) doubles the precision instead. Never
  // beyond the target.
  auto next_prec = [&](long c) {
    const double r = ratio[c];
    const slong inc = (std::isfinite(r) && r >= 0 && r < 2.0 * cur_prec[c])
                          ? (slong)std::ceil(r) + 32
                          : cur_prec[c];
    return std::min<slong>(target, cur_prec[c] + inc);
  };
  std::vector<char> seed_refused((*ar_).h(), 0);
  std::vector<phase_memory::PreparedSeed<Table>> prepared((*ar_).h());
  std::vector<phase_memory::Options> seed_options(
      (*ar_).h(), phase_options(kernel_share(class_nth), effective_correction_passes > 0));
  NewtonSeedState seed_state{class_memory, memory_owners, phase_admission, why,
                             seedfail,     seed_refused,  retained_memory, target,
                             matrix_hint,  seed_options};
  auto prepare_seed = [&](long c, slong seed_precision, int workers) {
    prepared[c].reset();
    std::optional<sic::support::Reservations::Lease> grant;
    if (memory_owners) {
      grant = class_memory->try_acquire(seed_bytes);
      if (!grant) {
        why[c] = "owned prepared-seed grant refused";
        seedfail[c] = 0;
        seed_refused[c] = 1;
        return false;
      }
    }
    auto table = std::make_unique<Table>((*ar_), seed_precision, workers);
    configure_table(*table);

    prepared[c].assign(std::move(table), std::move(grant));
    if (!build_newton_seed(seed_state, c, prepared[c])) {
      prepared[c].reset();
      return false;
    }
    ++seed_generation[c];
    return true;
  };
  // A callback's split is immutable. The coordinator recalculates the split
  // only for queued classes, using free CPU tokens and fully charged owners.
  auto run_classes = [&](std::span<const long> classes, auto seed_precision) {
    std::vector<sic::support::resources::Bytes> slots((*ar_).h(), worker_bytes);
    std::atomic<bool> pause_admission{false};
    std::vector<char> finished((*ar_).h(), 0), drained((*ar_).h(), 0);
    auto drain_finished = [&] {
      for (long c : classes) {
        if (!finished[c] || drained[c] ||
            outcomes[c] != phase_memory::ClassOutcome::resource_deferred)
          continue;
        drained[c] = 1;
        if (memory_owners && !seed_refused[c] && private_requests[c] != UINT64_MAX) {
          const auto retry_slot = std::max(worker_bytes, private_requests[c]);
          sic::support::progressf(
              "Newton resource serial drain: class=%ld seed_bits=%ld seed_generation=%u "
              "private_slot=%llu hint_rows=%zu seed_grant=%llu kernel_share=%d\n",
              c, (long)(prepared[c] ? prepared[c]->prec : T->prec), seed_generation[c],
              (unsigned long long)retry_slot, Shint[c].size(),
              (unsigned long long)prepared[c].granted(), kernel_share(class_counts[c]));
          refine_class(c, prepared[c] ? *prepared[c] : *T, false, retry_slot);
        }
        prepared[c].reset();
      }
    };
    auto choose = [&](long c, size_t remaining, int live,
                      int available) -> std::optional<ClassAllocation> {
      if (pause_admission.load(std::memory_order_acquire)) {
        if (live) return {};
        drain_finished();
        pause_admission.store(false, std::memory_order_release);
      }
      // Embedded unowned tables have no seed-scratch aggregate lease. Keep
      // their seed preparation between live groups, as required by the
      // original RSS admission; owned compact seeds may overlap callbacks.
      if (!memory_owners && !prepared[c] && seed_precision(c) && live) return {};
      const int maximum = std::min<int>(class_requested, live + remaining);
      for (int count = maximum; count > live; --count) {
        // Existing callbacks retain their shares. Never borrow their cores,
        // even if a fresh plan would prefer more classes or larger kernels.
        const auto cpu = class_allocation(nth, count, live, available,
                                          producer_options.selection_threads.get().value_or(nth));
        if (!cpu || cpu->classes != count) continue;
        const int share = cpu->workers;
        try {
          auto private_slot = worker_bytes;
          auto callback_slot =
              phase_memory::callback_workspace(d, private_slot, output_bytes, nth, false);
          if (phase_admission) {
            auto ceilings = *retained_memory[c];
            ceilings.workers = share;
            phase_memory::merge(
                ceilings,
                phase_memory::propose(d, target, matrix_hint, coarse_source,
                                      phase_options(share, effective_correction_passes > 0)));
            const auto requirement = phase_memory::requirement(ceilings, publication);
            private_slot = std::max(private_slot, requirement.private_peak);
            callback_slot = requirement.callback_peak;
          }
          const auto per_class =
              sic::support::resources::add(callback_slot, prepared[c] ? 0 : seed_bytes);
          const auto used = class_memory ? class_memory->used() : 0;
          const int charged = class_memory ? count - live : count;
          if (budget.workers(charged, sic::support::resources::add(resident, used), per_class) <
              charged)
            continue;
          if (!class_memory)
            budget.require_additional("live Newton callbacks",
                                      sic::support::resources::mul(charged, per_class));
          return ClassAllocation{count, share, private_slot};
        } catch (const sic::support::resources::AdmissionError&) {
        } catch (const std::overflow_error&) {
        }
      }
      return {};
    };
    auto refused = [&](long c, sic::support::resources::Bytes bytes) {
      outcomes[c] = phase_memory::ClassOutcome::resource_deferred;
      private_requests[c] = bytes == UINT64_MAX ? UINT64_MAX : slots[c];
      seedfail[c] = 0;
      why[c] = "owned live callback grant refused";
      finished[c] = 1;
      pause_admission.store(true, std::memory_order_release);
    };
    dispatch_live_classes(
        classes, nth, class_memory.get(), choose,
        [&](long c, const ClassAllocation& allocation) {
          class_counts[c] = allocation.classes;
          slots[c] = allocation.private_bytes;
          if (phase_admission) {
            retained_memory[c]->workers = allocation.workers;
            phase_memory::merge(
                *retained_memory[c],
                phase_memory::propose(
                    d, target, matrix_hint, coarse_source,
                    phase_options(allocation.workers, effective_correction_passes > 0)));
          }
          if (prepared[c]) return true;
          const auto bits = seed_precision(c);
          seed_refused[c] = 0;
          if (!bits) return true;
          seed_options[c] = phase_options(allocation.workers, effective_correction_passes > 0);
          ClassNativeShare seed_share(allocation.workers, nth, allocation.classes);
          if (!prepare_seed(c, bits, allocation.workers)) return false;
          sic::support::progressf("  class %ld: seed prepared at %ld bits before live refinement\n",
                                  c, (long)bits);
          return true;
        },
        [&](long c, const ClassAllocation&) {
          return attempt_bytes(c, prepared[c] ? *prepared[c] : *T, false, slots[c]);
        },
        refused,
        [&](long c, const ClassAllocation& allocation,
            std::optional<sic::support::Reservations::Lease> grant) {
          sic::support::progressf(
              "Newton live admission: class=%ld classes=%d workers=%d live=%d private=%llu "
              "forecast=%llu "
              "callback=%llu seed=%llu seed_bits=%ld\n",
              c, allocation.classes, allocation.workers, allocation.live,
              (unsigned long long)slots[c], (unsigned long long)allocation.callback_bytes,
              (unsigned long long)(grant ? grant->bytes() : 0),
              (unsigned long long)prepared[c].granted(),
              (long)(prepared[c] ? prepared[c]->prec : T->prec));
          refine_class(c, prepared[c] ? *prepared[c] : *T, false, slots[c], std::move(grant));
          finished[c] = 1;
          if (!memory_owners || outcomes[c] != phase_memory::ClassOutcome::resource_deferred)
            prepared[c].reset();
          else
            pause_admission.store(true, std::memory_order_release);
        });
    drain_finished();
    for (long c : classes) prepared[c].reset();
  };
  for (long c : todo) refine_class(c, *T, true);
  // Keep each class's precision/retry sequence and the final fallback order.
  // Scheduling changes only when an independent class enters that sequence.
  std::vector<slong> requested_seed((*ar_).h(), 0);
  for (long c : todo)
    if (b0fail(c) && (*ar_).cls[c].digit.size() > 8) requested_seed[c] = cur_prec[c] = next_prec(c);
  run_classes(todo, [&](long c) { return requested_seed[c]; });
  for (int retry = 0; retry < 2; ++retry) {
    std::vector<long> seeded;
    for (long c : todo)
      if (b0fail(c) && (*ar_).cls[c].digit.size() <= 8) {
        requested_seed[c] = cur_prec[c] = next_prec(c);
        seeded.push_back(c);
      }
    run_classes(seeded, [&](long c) { return requested_seed[c]; });
    for (long c : seeded)
      sic::support::progressf("  class %ld: Newton seed recomputed at %ld bits%s\n", c,
                              (long)cur_prec[c], ok[c] ? "" : " (failed)");
  }
  for (int retry = 0; retry < 2; ++retry) {
    std::vector<long> redo;
    for (long c = 0; c < (*ar_).h(); ++c)
      if (b0fail(c)) {
        requested_seed[c] = cur_prec[c] = next_prec(c);
        redo.push_back(c);
      }
    if (redo.empty()) break;
    run_classes(redo, [&](long c) { return requested_seed[c]; });
  }
  // classes where Newton refinement still fails: their table by quadrature at the target precision
  // (one class costs its own cycle states only; the rest of the table stays Newton-refined)
  if (class_memory) {
    for (auto& pending_seed : prepared) pending_seed.reset();
    if (class_memory->used()) throw std::logic_error("unfinished Newton grants before fallback");
    if (producer_options.progress)
      sic::support::progressf(
          "Newton callback owners drained: outstanding=0; later fallback/replay separately "
          "admitted\n");
  }
  for (long c = 0; c < (*ar_).h(); ++c)
    if (!ok[c] && !derived[c]) {
      const double tq0 = stamp();
      if (stream)
        budget.require_additional("streamed Newton quadrature fallback",
                                  sic::support::resources::add(
                                      (*model_).quadrature_workspace(precision + 16, nth),
                                      sic::support::resources::mul(
                                          2 * ((d * d - 1) / 3),
                                          sic::support::resources::complex_bytes(precision + 16))));
      else
        budget.require("Newton quadrature fallback",
                       sic::support::resources::add(
                           (*model_).quadrature(precision + 16, nth, 1),
                           sic::support::resources::add(
                               (*model_).table(T->prec),
                               sic::support::resources::mul(
                                   2 * ((d * d - 1) / 3),
                                   sic::support::resources::complex_bytes(precision + 16)))));
      Table Tq((*ar_), precision + 16, nth);
      configure_table(Tq);
      budget.require_additional(
          "Newton quadrature fallback grids",
          sic::support::resources::mul(6 * d * d,
                                       sic::support::resources::complex_bytes(Tq.prec + 96)));
      Tq.build_class((int)c);
      sic::support::ComplexBallVector value(1);
      for (long q = 1; q < d * d; ++q) {
        Tq.read(value[0], c * d * d + q);
        acb_set(Tn->writable(c * d * d + q), value[0]);
      }
      sic::support::progressf(
          "  class %ld (%s): table by quadrature at %ld bits instead (%d states, %.1fs)\n", (long)c,
          why[c].c_str(), (long)precision + 16, (int)(*ar_).cls[c].digit.size(), stamp() - tq0);
      ok[c] = 1;
      used_quadrature[c] = 1;
      if (stream) park_completed_class(*Tn, c);
    }
  bool all = true;
  double cmax = 0;
  size_t unknown_conditioning = 0;
  for (sic::support::Index c = 0; c < (*ar_).h(); ++c) {
    if (!ok[c]) {
      all = false;
      sic::support::progressf("  Newton refinement failed for class %ld: %s\n", (long)c,
                              why[c].c_str());
    }
    if (std::isfinite(cond[c])) cmax = std::max(cmax, cond[c]);
    if (!used_quadrature[c] && selection_kinds[c] == GhostRefiner::SelectionKind::modular_midpoint)
      ++unknown_conditioning;
  }
  if (all) {
    derive_classes(*Tn);
    if (reused)
      sic::support::progressf(
          "completed-class cache reuse: %zu physical classes; newly computed=%zu\n", reused,
          todo.size());
    report_newton_table(todo.size(), physical, (*ar_).h(), precision, unknown_conditioning, cmax,
                        used_quadrature, stamp());
  }
  if (!all) pending_table_backing.clear();
  return all ? std::move(Tn) : nullptr;
}
}  // namespace sic::construct
