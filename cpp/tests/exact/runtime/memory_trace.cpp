#include <array>
#include <set>

#include "assertions.hpp"
#include "exact/analytic/table.hpp"
#include "exact/certification/reconstruction/ghost.hpp"
#include "exact/group/group.hpp"
#include "support/resources.hpp"

namespace {
template <class F>
bool throws(F&& operation) {
  try {
    operation();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

void compare_trace(int exponent, slong seed_bits) {
  using namespace sic::exact;
  setenv("SIC_KRAWCZYK_FIRST_BITS", std::to_string(seed_bits).c_str(), 1);
  Arithmetic arithmetic(exponent);
  const long d = arithmetic.d;
  Group group(arithmetic);
  group.generate_standard();
  group.close(
      sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")),
      arithmetic.h() * d * d / 2);
  Table table(arithmetic, seed_bits, 4);
  table.build_class(0);
  sic::support::ComplexBallVector seed(d * d), expected, actual;
  for (long y = 1; y < d * d; ++y) table.read(seed[y], group.ghost_point(y / d, y % d));
  GhostRefiner reference(d, 1, 1), traced(d, 1, 1);
  reference.kernel_threads = traced.kernel_threads = 4;
  reference.correction_passes = traced.correction_passes = 2;
  reference.matrix_precision_hint = traced.matrix_precision_hint = seed_bits;
  reference.retain_certified_root = traced.retain_certified_root = true;
  std::string reason;
  const slong target = 2 * seed_bits;
  const bool baseline = reference.refine(seed.v, target, expected.v, reason);
  SIC_CHECK(baseline);
  if (!baseline) {
    std::cerr << "fixture failed: " << reason << '\n';
    return;
  }
  std::vector<phase_memory::Event> events;
  traced.memory_observer = [&](const phase_memory::Event& event) { events.push_back(event); };
  SIC_CHECK(traced.refine(seed.v, target, actual.v, reason));
  SIC_CHECK(traced.memory_trace == nullptr);
  SIC_CHECK(traced.memory_refusal == phase_memory::Refusal::none);
  SIC_CHECK(traced.S == reference.S && traced.k == reference.k);
  SIC_CHECK(traced.newton_iters == reference.newton_iters);
  SIC_CHECK(traced.final_prec == reference.final_prec &&
            traced.achieved_bits == reference.achieved_bits);
  SIC_CHECK(traced.correction_accepted == reference.correction_accepted);
  SIC_CHECK(actual.size() == expected.size());
  for (size_t y = 0; y < expected.size(); ++y) SIC_CHECK(acb_equal(actual[y], expected[y]));
  SIC_CHECK(traced.certified_root && reference.certified_root);
  if (traced.certified_root && reference.certified_root) {
    const auto& a = traced.certified_root->metadata;
    const auto& b = reference.certified_root->metadata;
    SIC_CHECK(a.d == b.d && a.n == b.n && a.k == b.k && a.chi == b.chi &&
              a.precision == b.precision && a.constants_precision == b.constants_precision);
    for (size_t q = 0; q < traced.certified_root->state.size(); ++q)
      SIC_CHECK(acb_equal(traced.certified_root->state[q], reference.certified_root->state[q]));
  }
  std::set<phase_memory::Phase> phases;
  std::set<std::string> objects;
  for (const auto& event : events) {
    SIC_CHECK(event.forecast_valid);
    SIC_CHECK(event.storage_fits());
    SIC_CHECK(event.observed_contract_ok);
    SIC_CHECK(event.accounting.peak() >= event.accounting.at(event.phase));
    phases.insert(event.phase);
    objects.insert(event.object);
  }
  SIC_CHECK(phases.size() == phase_memory::kPhaseNames.size());
  for (const char* object :
       {"caller seed", "root-map scalar payload", "projection A0", "projection X", "inverse Y",
        "stored M0", "reconstructed output", "residual caches", "Jacobian caches", "top d roots",
        "equation roots", "selection J", "inverse J", "correction J", "empty Mloc", "Krawczyk J",
        "Krawczyk Mloc"})
    SIC_CHECK(objects.contains(object));

  // Stop-like observer exceptions must restore the active trace and unwind
  // all owned refiner storage, including serial and parallel cache paths.
  for (const char* object : {"caller seed", "root-map scalar payload", "projection X", "inverse Y",
                             "residual caches", "Jacobian caches", "reconstructed output"}) {
    GhostRefiner stopped(d, 1, 1);
    stopped.kernel_threads = 4;
    stopped.matrix_precision_hint = seed_bits;
    stopped.memory_observer = [object](const phase_memory::Event& event) {
      if (std::string(event.object) == object) throw std::runtime_error("injected observer stop");
    };
    sic::support::ComplexBallVector rejected;
    SIC_CHECK(throws([&] { stopped.refine(seed.v, target, rejected.v, reason); }));
    SIC_CHECK(stopped.memory_trace == nullptr);
    SIC_CHECK(!stopped.certified_root);
  }
  GhostRefiner refused(d, 1, 1);
  refused.workspace_limit = 1;
  refused.memory_observer = [](const phase_memory::Event&) {};
  SIC_CHECK(!refused.refine(seed.v, target, actual.v, reason));
  SIC_CHECK(refused.memory_refusal == phase_memory::Refusal::class_slot);
  SIC_CHECK(refused.required_workspace > refused.workspace_limit);
  SIC_CHECK(refused.memory_trace == nullptr);
  refused.workspace_limit = UINT64_MAX;
  SIC_CHECK(refused.refine(seed.v, target, actual.v, reason));
  SIC_CHECK(refused.memory_refusal == phase_memory::Refusal::none);
  SIC_CHECK(refused.required_workspace == 0);
  GhostRefiner growth_refused(d, 1, 1);
  growth_refused.matrix_precision_hint = seed_bits;
  bool after_b0 = false, requested_after_b0 = false;
  growth_refused.after_b0 = [&](const acb_mat_struct*, const std::vector<acb_struct>&, slong, slong,
                                slong) {
    after_b0 = true;
    growth_refused.workspace_limit = 1;
  };
  growth_refused.memory_observer = [&](const phase_memory::Event& event) {
    if (after_b0 && event.phase == phase_memory::Phase::high_newton && !event.observed_storage &&
        event.callback_slot == 1)
      requested_after_b0 = true;
  };
  SIC_CHECK(!growth_refused.refine(seed.v, target, actual.v, reason));
  SIC_CHECK(after_b0 && requested_after_b0);
  SIC_CHECK(growth_refused.memory_refusal == phase_memory::Refusal::class_slot);
  if (exponent == 6) {
    // The mandatory radius gate skips the doomed 80-bit attempt. The full-precision
    // retry must still certify and enclose the same reconstruction.
    setenv("SIC_KRAWCZYK_FIRST_BITS", "80", 1);
    GhostRefiner low_precision(d, 1, 1);
    low_precision.kernel_threads = 4;
    low_precision.correction_passes = 2;
    low_precision.matrix_precision_hint = seed_bits;
    low_precision.retain_certified_root = true;
    SIC_CHECK(low_precision.refine(seed.v, target, actual.v, reason));
    SIC_CHECK(low_precision.memory_refusal == phase_memory::Refusal::none);
    SIC_CHECK(low_precision.required_workspace == 0);
    SIC_CHECK(bool(low_precision.certified_root));
    SIC_CHECK(actual.size() == expected.size());
    SIC_CHECK(low_precision.achieved_bits >= target);
    for (size_t q = 0; q < expected.size(); ++q) {
      SIC_CHECK(acb_is_finite(actual[q]));
      SIC_CHECK(acb_overlaps(actual[q], expected[q]));
    }
  }
}

void sticky_storage_contract() {
  using namespace sic::exact;
  phase_memory::Ceilings c;
  c.dimension = 8;
  c.source_bits = c.selection_bits = 256;
  c.inverse_bits = c.stored_matrix_bits = 128;
  c.high_bits = 1024;
  c.candidate_rows = 63;
  c.workers = 1;
  c.modular = true;
  std::vector<phase_memory::Event> events;
  phase_memory::Trace trace(c, UINT64_MAX,
                            [&](const phase_memory::Event& event) { events.push_back(event); });
  sic::support::ComplexBallVector source(64);
  acb_indeterminate(source[1]);
  trace.source(source.v);
  trace.enter(phase_memory::Phase::projection);
  SIC_CHECK(events.size() == 2);
  SIC_CHECK(events.front().forecast_valid && !events.front().storage_fits());
  SIC_CHECK(!events.front().observed_contract_ok);
  SIC_CHECK(events.back().forecast_valid && events.back().storage_fits());
  SIC_CHECK(!events.back().observed_contract_ok);
  SIC_CHECK(!events.back().observed_storage);
  acb_mat_t matrix;
  acb_mat_init(matrix, 2, 3);
  const auto empty = phase_memory::inspect_storage(matrix);
  SIC_CHECK(empty.values == 6 && empty.heap_bytes == 0);
  SIC_CHECK(empty.header_bytes >= 6 * sizeof(acb_struct) + 2 * sizeof(acb_ptr));
  acb_mat_clear(matrix);
}
}  // namespace

int main() {
  flint_set_num_threads(4);
  sticky_storage_contract();
  compare_trace(3, 256);
  compare_trace(4, 256);
  compare_trace(6, 512);
  flint_cleanup_master();
  return sic_test::report();
}
