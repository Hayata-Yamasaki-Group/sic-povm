// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/stages/table/newton_seed.hpp"

#include "construct/model/session_state.hpp"
#include "construct/runtime/resources/construction_model.hpp"
#include "construct/runtime/resources/table_memory.hpp"
namespace sic::construct {
bool Construction::build_newton_seed(NewtonSeedState& state, long c,
                                     phase_memory::PreparedSeed<Table>& pending_seed) {
  auto& class_memory = state.class_memory;
  auto& memory_owners = state.memory_owners;
  auto& phase_admission = state.phase_admission;
  auto& why = state.why;
  auto& seedfail = state.seedfail;
  auto& seed_refused = state.seed_refused;
  auto& retained_memory = state.retained_memory;
  auto& target = state.target;
  auto& matrix_hint = state.matrix_hint;
  std::optional<sic::support::Reservations::Lease> scratch;
  sic::support::resources::Bytes base = 0;
  try {
    base = std::max(sic::support::resources::mul(
                        6 * d * d, sic::support::resources::complex_bytes(pending_seed->prec + 96)),
                    (*model_).quadrature_workspace(pending_seed->prec, pending_seed->nthreads));
    if (memory_owners) {
      scratch = class_memory->try_acquire(base);
      if (!scratch)
        throw sic::support::resources::AdmissionError("owned seed-build scratch refused");
    }
    budget.require_additional("isolated Newton seed grids", base);
  } catch (const sic::support::resources::AdmissionError& error) {
    why[c] = error.what();
    seedfail[c] = 0;
    seed_refused[c] = 1;
    return false;
  }
  try {
    {
      std::optional<sic::construct::resources::SeedKernelMemory> kernels;
      if (memory_owners) {
        const int active = pending_seed->stream_states
                               ? 1
                               : std::min<int>(pending_seed->nthreads, (*ar_).cls[c].digit.size());
        kernels.emplace(*pending_seed, *scratch, base, active);
      }
      pending_seed->build_class(c);
      if (kernels && producer_options.progress)
        sic::support::progressf(
            "Newton seed kernel owner: class=%ld base=%llu kernel_peak=%llu scratch_grant=%llu\n",
            c, (unsigned long long)base, (unsigned long long)kernels->kernel_peak(),
            (unsigned long long)scratch->bytes());
    }  // restore Table's callback before any Table destruction
    if (memory_owners) {
      const auto storage = sic::construct::resources::inspect_retained_table(*pending_seed);
      if (!storage.finite_lagom || !storage.source.finite_lagom)
        throw phase_memory::StorageContractError("retained prepared Table");
      if (!pending_seed.try_grow(storage.owned))
        throw sic::support::resources::AdmissionError("retained prepared Table grant refused");
      if (phase_admission)
        phase_memory::merge(
            *retained_memory[c],
            phase_memory::propose(d, target, matrix_hint, storage.source, state.options[c]));
    }
  } catch (const sic::support::resources::AdmissionError& error) {
    why[c] = error.what();
    seedfail[c] = 0;
    seed_refused[c] = 1;
    pending_seed.reset();
    return false;  // Table dies while build scratch is still owned
  } catch (const phase_memory::StorageContractError& error) {
    why[c] = error.what();
    seedfail[c] = 0;
    seed_refused[c] = 1;
    pending_seed.reset();
    return false;
  } catch (...) {
    pending_seed.reset();
    throw;
  }  // destroy partial Table before scratch grant
  return true;
}
}  // namespace sic::construct
