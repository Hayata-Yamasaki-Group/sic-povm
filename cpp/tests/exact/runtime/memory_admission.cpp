#include "assertions.hpp"
#include "exact/analytic/table.hpp"
#include "exact/certification/reconstruction/ghost.hpp"
#include "exact/group/group.hpp"
#include "exact/runtime/newton_class_memory.hpp"
#include "support/resources.hpp"

namespace phase_memory = sic::exact::phase_memory;

namespace {

phase_memory::Options options(bool modular) {
  phase_memory::Options result;
  result.workers = 4;
  result.modular = modular;
  result.column_inverse = result.threaded_product = true;
  result.refined_correction = result.parallel_reconstruction = true;
  result.native_scalar_bytes = sizeof(sic::exact::SelectionWide);
  result.native_complex_bytes =
      sizeof(sic::exact::GhostRefiner::ComplexBall<sic::exact::SelectionWide>);
  return result;
}

void compare(int exponent, slong bits, bool modular) {
  using namespace sic::exact;
  setenv("SIC_KRAWCZYK_FIRST_BITS", std::to_string(bits).c_str(), 1);
  Arithmetic arithmetic(exponent);
  const long d = arithmetic.d;
  Group group(arithmetic);
  group.generate_standard();
  group.close(
      sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")),
      arithmetic.h() * d * d / 2);
  Table table(arithmetic, bits, 4);
  table.build_class(0);
  sic::support::ComplexBallVector seed(d * d), expected, actual;
  for (long y = 1; y < d * d; ++y) table.read(seed[y], group.ghost_point(y / d, y % d));
  // Preserve the exact certified input, but retain a much wider old capacity.
  // Neither its current precision nor acb_bits may replace this obligation.
  sic::support::ComplexBallVector original(1);
  acb_set(original[0], seed[1]);
  arb_const_pi(acb_realref(seed[1]), 8192);
  acb_set(seed[1], original[0]);
  phase_memory::CopySource source;
  for (const auto& value : seed.v) source.observe(&value);
  SIC_CHECK(source.finite_lagom && source.bits <= (sic::support::resources::BitCount)bits);
  SIC_CHECK(source.grid_storage(d) >= phase_memory::inspect_storage(seed.v).bytes());
  const auto initial = phase_memory::propose(d, 2 * bits, bits, source, options(modular));
  const phase_memory::Publication publication{
      sic::support::resources::mul((d * d - 1) / 3,
                                   sic::support::resources::complex_bytes(2 * bits)),
      4, true};
  const auto plan = phase_memory::requirement(initial, publication);
  SIC_CHECK(phase_memory::shortcut_workspace(initial, publication.packed_output) >
            sic::support::resources::add(
                publication.packed_output,
                sic::support::resources::add(
                    128 * sic::support::resources::kMib,
                    sic::support::resources::mul(
                        32, sic::support::resources::complex_bytes(bits + 256)))));
  SIC_CHECK(plan.callback_peak >=
            sic::support::resources::add(plan.private_peak, publication.packed_output));
  SIC_CHECK(phase_memory::correction_baseline(plan.accounting) <
            plan.accounting.at(phase_memory::Phase::high_newton));
  GhostRefiner baseline(d, 1, 1), phase(d, 1, 1);
  for (auto* refiner : {&baseline, &phase}) {
    refiner->kernel_threads = 4;
    refiner->matrix_precision_hint = bits;
    refiner->correction_passes = 2;
    refiner->retain_certified_root = true;
  }
  phase.phase_admission = true;
  phase.retained_memory = initial;
  phase.workspace_limit = plan.private_peak;
  phase.callback_workspace_limit = plan.callback_peak;
  phase.memory_publication = publication;
  size_t observed = 0;
  phase.memory_observer = [&](const phase_memory::Event& event) {
    ++observed;
    SIC_CHECK(event.forecast_valid && event.storage_fits());
    SIC_CHECK(event.ceilings.high_bits == phase.retained_memory->high_bits);
    SIC_CHECK(event.accounting.peak() ==
              phase_memory::requirement(*phase.retained_memory, publication).private_peak);
    const auto N = 2 * d - 2;
    if (std::string(event.object) == "selection J")
      SIC_CHECK(event.object_forecast <=
                phase_memory::matrix_bytes(
                    event.storage.values / N, N,
                    sic::support::resources::complex_bytes(
                        sic::support::resources::add(event.ceilings.selection_bits, 128))));
    if (std::string(event.object) == "correction J")
      SIC_CHECK(event.object_forecast <=
                phase_memory::matrix_bytes(
                    N, N,
                    sic::support::resources::complex_bytes(sic::support::resources::add(
                        std::min(event.ceilings.high_bits,
                                 sic::support::resources::add(event.ceilings.inverse_bits, 512)),
                        128))));
  };
  std::string reason;
  const bool reference_ok = baseline.refine(seed.v, 2 * bits, expected.v, reason);
  SIC_CHECK(reference_ok);
  if (!reference_ok) {
    std::cerr << "baseline: " << reason << '\n';
    return;
  }
  const bool admitted = phase.refine(seed.v, 2 * bits, actual.v, reason);
  SIC_CHECK(admitted);
  if (!admitted) {
    std::cerr << "phase: " << reason << '\n';
    return;
  }
  SIC_CHECK(observed > 20 && phase.memory_trace == nullptr);
  SIC_CHECK(phase.newton_iters == baseline.newton_iters && phase.S == baseline.S);
  SIC_CHECK(phase.final_prec == baseline.final_prec &&
            phase.achieved_bits == baseline.achieved_bits);
  SIC_CHECK(phase.correction_accepted == baseline.correction_accepted);
  SIC_CHECK(actual.size() == expected.size());
  for (size_t y = 0; y < actual.size(); ++y) SIC_CHECK(acb_equal(actual[y], expected[y]));
  SIC_CHECK(phase.certified_root && baseline.certified_root);
  if (phase.certified_root && baseline.certified_root) {
    const auto& a = phase.certified_root->metadata;
    const auto& b = baseline.certified_root->metadata;
    SIC_CHECK(a.d == b.d && a.n == b.n && a.k == b.k && a.chi == b.chi &&
              a.precision == b.precision && a.constants_precision == b.constants_precision);
    for (size_t q = 0; q < phase.certified_root->state.size(); ++q)
      SIC_CHECK(acb_equal(phase.certified_root->state[q], baseline.certified_root->state[q]));
  }

  // A callback growth refusal must carry its entire retained requirement,
  // including source capacity. Readmission uses that same shared calculator.
  phase.workspace_limit = 1;
  SIC_CHECK(!phase.refine(seed.v, 2 * bits, actual.v, reason));
  SIC_CHECK(phase.memory_refusal == phase_memory::Refusal::class_slot && !phase.certified_root);
  auto retry = phase_memory::requirement(*phase.retained_memory, publication);
  SIC_CHECK(retry.private_peak == phase.required_workspace);
  SIC_CHECK(retry.ceilings.source_storage_bytes >= source.grid_storage(d));
  phase.workspace_limit = retry.private_peak;
  phase.callback_workspace_limit = retry.callback_peak;
  bool fired = false;
  phase.after_b0 = [&](const acb_mat_struct*, const std::vector<acb_struct>&, slong, slong, slong) {
    fired = true;
    phase.workspace_limit = 1;
  };
  SIC_CHECK(!phase.refine(seed.v, 2 * bits, actual.v, reason));
  SIC_CHECK(fired && phase.has_complete_selection() && phase.memory_trace == nullptr);
  SIC_CHECK(phase.memory_refusal == phase_memory::Refusal::class_slot);
  phase.S_hint = phase.S;
  phase.cond_hint = phase.cond_est;
  phase.hint_kind = phase.selection_kind;
  phase.inverse_precision_hint = phase.inverse_precision_proposal;
  phase.after_b0 = {};
  retry = phase_memory::requirement(*phase.retained_memory, publication);
  phase.workspace_limit = retry.private_peak;
  phase.callback_workspace_limit = retry.callback_peak;
  SIC_CHECK(phase.refine(seed.v, 2 * bits, actual.v, reason));
  SIC_CHECK(phase.memory_refusal == phase_memory::Refusal::none);
  for (size_t y = 0; y < actual.size(); ++y) SIC_CHECK(acb_equal(actual[y], expected[y]));

  // A byte-only private grant must not accidentally authorize publication.
  phase.callback_workspace_limit = 1;
  SIC_CHECK(!phase.refine(seed.v, 2 * bits, actual.v, reason));
  SIC_CHECK(phase.memory_refusal == phase_memory::Refusal::class_slot);
  SIC_CHECK(phase.required_workspace ==
            phase_memory::requirement(*phase.retained_memory, publication).private_peak);
  phase.callback_workspace_limit = retry.callback_peak;
  // Trusted observers retain their exception type/message, even when the
  // same types are used internally for expected admission outcomes.
  for (bool overflow : {false, true}) {
    phase.memory_observer = [overflow](const phase_memory::Event&) {
      if (overflow) throw std::overflow_error("observer-overflow");
      throw phase_memory::StorageContractError("observer-storage");
    };
    bool propagated = false;
    try {
      phase.refine(seed.v, 2 * bits, actual.v, reason);
    } catch (const std::overflow_error& error) {
      propagated = overflow && std::string(error.what()) == "observer-overflow";
    } catch (const phase_memory::StorageContractError& error) {
      propagated = !overflow && std::string(error.what()) == "observer-storage";
    }
    SIC_CHECK(propagated && phase.memory_trace == nullptr &&
              phase.memory_refusal == phase_memory::Refusal::none);
  }
  phase.memory_observer = {};
  phase.after_b0 = [](const acb_mat_struct*, const std::vector<acb_struct>&, slong, slong, slong) {
    throw phase_memory::StorageContractError("after-B0-storage");
  };
  bool propagated = false;
  try {
    phase.refine(seed.v, 2 * bits, actual.v, reason);
  } catch (const phase_memory::StorageContractError& error) {
    propagated = std::string(error.what()) == "after-B0-storage";
  }
  SIC_CHECK(propagated && phase.memory_trace == nullptr &&
            phase.memory_refusal == phase_memory::Refusal::none);
  phase.after_b0 = {};
  fmpz_t exponent_value;
  fmpz_init(exponent_value);
  fmpz_one(exponent_value);
  fmpz_mul_2exp(exponent_value, exponent_value, 128);
  arb_mul_2exp_fmpz(acb_realref(seed[1]), acb_realref(seed[1]), exponent_value);
  fmpz_clear(exponent_value);
  SIC_CHECK(!phase.refine(seed.v, 2 * bits, actual.v, reason));
  SIC_CHECK(phase.memory_refusal == phase_memory::Refusal::storage_contract &&
            phase.required_workspace == 0);
  SIC_CHECK(!phase.certified_root && phase.memory_trace == nullptr);
}
}  // namespace

int main() {
  flint_set_num_threads(4);
  compare(3, 256, true);
  compare(4, 256, true);
  compare(6, 512, true);
  flint_cleanup_master();
  return sic_test::report();
}
