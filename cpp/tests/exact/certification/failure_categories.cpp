// SPDX-License-Identifier: AGPL-3.0-or-later
#include <unistd.h>

#include <atomic>
#include <fstream>
#include <limits>
#include <source_location>
#include <thread>

#include "assertions.hpp"
#include "exact/analytic/cyclotomic_trace.hpp"
#include "exact/analytic/direct_eta.hpp"
#include "exact/analytic/direct_eta_rows.hpp"
#include "exact/analytic/step_selection.hpp"
#include "exact/analytic/table.hpp"
#include "exact/arithmetic/maximal_order.hpp"
#include "exact/certification/bounds/height_bounds.hpp"
#include "exact/certification/newton/recognition.hpp"
#include "exact/certification/reconstruction/ghost.hpp"
#include "exact/group/maximal_order_orbits.hpp"
#include "exact/group/orbit_table.hpp"
#include "exact/runtime/disk_budget.hpp"
#include "exact/runtime/memory/product_tiles.hpp"
#include "exact/runtime/newton_class_memory.hpp"
#include "exact/runtime/parallel.hpp"
#include "exact/transform/fixed_field_tower_general.hpp"
#include "sic/error.hpp"
#include "sic/support/flint_raii.hpp"
#include "support/admitted_worker_team.hpp"
#include "temporary_directory.hpp"

using namespace sic::exact;
using sic::Error;
using sic::ErrorKind;
using sic::support::ComplexBallVector;
using sic::support::resources::AdmissionError;
using sic::support::resources::Bytes;
constexpr Bytes kUnlimited = std::numeric_limits<Bytes>::max();

namespace {
using Matrix = sic::support::flint::AcbMat;
struct ObserverCause : std::runtime_error {
  ObserverCause() : std::runtime_error("observer cause") {}
};
enum class Fault { admission, startup, allocation, overflow, observer, cancelled };
constexpr Fault kFaults[]{Fault::admission, Fault::startup,  Fault::allocation,
                          Fault::overflow,  Fault::observer, Fault::cancelled};
[[noreturn]] void inject(Fault fault) {
  switch (fault) {
    case Fault::admission:
      throw AdmissionError("injected admission refusal");
    case Fault::startup:
      throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
    case Fault::allocation:
      throw std::bad_alloc();
    case Fault::overflow:
      throw std::overflow_error("injected native overflow");
    case Fault::observer:
      throw ObserverCause();
    case Fault::cancelled:
      throw Error(ErrorKind::cancelled, "arbitrary cancellation wording");
  }
  std::terminate();
}
template <class F>
void kind(ErrorKind expected, F action,
          std::source_location location = std::source_location::current()) {
  bool caught = false;
  try {
    action();
  } catch (const Error& error) {
    caught = true;
    SIC_CHECK(error.kind() == expected);
  } catch (const std::exception& error) {
    std::cerr << "uncategorized: " << error.what() << '\n';
  }
  if (!caught) std::cerr << "missing category at line " << location.line() << '\n';
  SIC_CHECK(caught);
}
template <class F>
void cause(Fault expected, F action) {
  bool caught = false;
  try {
    action();
  } catch (const AdmissionError& error) {
    SIC_CHECK(error.kind() == ErrorKind::resource);
    caught = expected == Fault::admission;
  } catch (const Error& error) {
    caught = expected == Fault::cancelled && error.kind() == ErrorKind::cancelled &&
             std::string(error.what()) == "arbitrary cancellation wording";
  } catch (const std::system_error&) {
    caught = expected == Fault::startup;
  } catch (const std::bad_alloc&) {
    caught = expected == Fault::allocation;
  } catch (const std::overflow_error&) {
    caught = expected == Fault::overflow;
  } catch (const ObserverCause&) {
    caught = expected == Fault::observer;
  }
  SIC_CHECK(caught);
}
void shared_dispatch_failures() {
  // Each cause escapes only after every shared-team callback has left its scope.
  for (Fault fault : kFaults) {
    std::atomic<int> entered{0}, exited{0};
    std::atomic<bool> wrong_dispatch{false};
    cause(fault, [&] {
      parallel_for(64, 4, [&](long row) {
        ++entered;
        struct Exit {
          std::atomic<int>& count;
          ~Exit() { ++count; }
        } exit{exited};
        if (!sic::support::WorkerTeam::inside()) wrong_dispatch = true;
        if (!row) inject(fault);
      });
    });
    SIC_CHECK(entered > 0 && entered == exited && !wrong_dispatch);
    std::atomic<int> recovered{0};
    parallel_for(64, 4, [&](long) { ++recovered; });
    SIC_CHECK(recovered == 64);
  }
}
void proposal_categories() {
  sic_test::TemporaryDirectory work("sic-failure-proposal");
  const std::string path = (work.path / "selection.txt").string();
  const StepSelectionKey key{4, 1, 8, 1};
  auto read = [&] { return read_step_selection(path.c_str(), key); };
  auto put = [&](const std::string& text) { std::ofstream(path, std::ios::binary) << text; };
  std::remove(path.c_str());
  kind(ErrorKind::io, read);
  kind(ErrorKind::io, [&] { read_step_selection("/tmp", key); });
  put(std::string(1024 * 1024 + 1, 'x'));
  kind(ErrorKind::resource, read);
  put("garbled\n");
  kind(ErrorKind::invalid_format, read);
  put("sic-step-selection-checkpoint/1.0 4 1 8 2\nx\ny\n");
  kind(ErrorKind::integrity, read);
  put("sic-step-selection-checkpoint/1.0 4 1 8 1 extra\nx\n");
  kind(ErrorKind::invalid_format, read);
  for (const auto& name : {std::string(), std::string(129, 'x'), std::string("x\0y", 3)}) {
    put("sic-step-selection-checkpoint/1.0 4 1 8 1\n" + name + "\n");
    kind(ErrorKind::invalid_format, read);
  }
  put("sic-step-selection-checkpoint/1.0 4 1 8 1\nx\nextra\n");
  kind(ErrorKind::invalid_format, read);
  kind(ErrorKind::invalid_argument, [&] { write_step_selection(path.c_str(), key, {}); });
  kind(ErrorKind::invalid_argument, [&] { write_step_selection(path.c_str(), key, {"x\ny"}); });
  kind(ErrorKind::io, [&] { write_step_selection(path.c_str(), key, {"x"}); });
  std::remove(path.c_str());
  write_step_selection(path.c_str(), key, {"x"});
  SIC_CHECK(read() == std::vector<std::string>{"x"});
  std::remove(path.c_str());
}
void numerical_categories() {
  sic::support::flint::Arb x, y, one;
  sic::support::flint::Acb output;
  arb_one(one);
  // Native/invariant guards keep standard types, even at an exact entry point.
  bool standard = false;
  try {
    Arithmetic invalid(1);
  } catch (const Error&) {
  } catch (const std::runtime_error&) {
    standard = true;
  }
  SIC_CHECK(standard);
  kind(ErrorKind::unsupported, [] { Arithmetic unsupported(3, true); });
  kind(ErrorKind::construction, [] { resid(Rational(1, 3), 4); });
  Arithmetic arithmetic(2);
  kind(ErrorKind::construction, [&] { reduced_order::degree(arithmetic, 3); });
  kind(ErrorKind::construction,
       [] { reduced_order::contract(false, "premise", ErrorKind::construction); });
  standard = false;
  try {
    reduced_order::contract(false, "invariant");
  } catch (const Error&) {
  } catch (const std::runtime_error&) {
    standard = true;
  }
  SIC_CHECK(standard);
  Group group(arithmetic);
  group.generate_standard();
  group.close(arithmetic.h() * 8);
  kind(ErrorKind::construction, [&] {
    ComplexBallVector roots(8);
    cyclotomic_generator_values(group, 1, {}, 0, 0, 0, roots, 128);
  });
  kind(ErrorKind::construction, [&] {
    ComplexBallVector roots(8);
    cyclotomic_generator_values(group, 1, {}, 1, 0, 1, roots, 128);
  });
  kind(ErrorKind::construction, [] {
    Arithmetic a(2);
    a.eps = QuadraticNumber(3);
    reduced_order::cycle_unit(a);
  });
  kind(ErrorKind::construction, [] {
    Arithmetic a(2);
    a.cls.push_back(a.cls[0]);
    a.cls[1].scal.back() = mul(a.kc, a.cls[1].scal.back(), QuadraticNumber(2));
    reduced_order::cycle_unit(a);
  });
  kind(ErrorKind::construction, [&] {
    long cc = 1;
    while (cc < group.N && group.mul(cc, cc) != 0) ++cc;
    SIC_CHECK(cc < group.N);
    Group altered = group;
    altered.nn[0] = 5;
    ScalarFixedFieldsGeneral invalid(altered, std::vector<long>(8), cc);
  });
  kind(ErrorKind::construction, [] {
    Arithmetic a(2);
    Group g(a);
    reduced_order::configure_group_orbits(g, {QuadraticNumber(2), 2});
  });
  kind(ErrorKind::construction, [] {
    Arithmetic a(2);
    Group g(a);
    reduced_order::configure_group_orbits(g, {QuadraticNumber(3), 3});
  });
  kind(ErrorKind::precision, [&] {
    ComplexBallVector roots(8);
    acb_indeterminate(roots[1]);
    cyclotomic_generator_values(group, 1, {}, 1, 0, 0, roots, 128);
  });
  kind(ErrorKind::construction,
       [&] { ScalarFixedFieldsGeneral invalid(group, std::vector<long>(8), 0); });
  kind(ErrorKind::construction, [] { OrbitTableLayout invalid(4, 1, std::vector<int>(16)); });
  kind(ErrorKind::precision, [&] {
    ComplexBallVector v(4), u(4), roots(8);
    DirectEtaRoot invalid(4, 128, std::move(v), std::move(u), roots.v);
  });
  kind(ErrorKind::unsupported, [&] { RecognitionLattice invalid(-1, one, 128); });
  const auto huge = certified::HeightBound(2).pow(1UL << 50);
  kind(ErrorKind::unsupported, [&] { huge.upper_binary_exponent(); });
  arb_zero(x);
  standard = false;
  try {
    certified::ContourBounds invalid(one, x, 0, 128);
  } catch (const Error&) {
  } catch (const std::runtime_error&) {
    standard = true;
  }
  SIC_CHECK(standard);
  // The invalid parameter guard above is internal, whereas these two are precision refusals.
  arb_set_ui(x, 4);
  kind(ErrorKind::precision, [&] { certified::ContourBounds invalid(one, x, 0, 128); });
  arb_one(x);
  kind(ErrorKind::precision, [&] { certified::ContourBounds invalid(one, x, 2, 128); });
  arb_set_ui(x, 100);
  standard = false;
  try {
    faddeev_strip(output, x, one, 128);
  } catch (const Error&) {
  } catch (const std::runtime_error&) {
    standard = true;
  }
  SIC_CHECK(standard);
  // An interval containing zero fails the internal positive-parameter guard.
  arb_set_ui(x, 1);
  arb_add_error_2exp_si(x, 0);
  standard = false;
  try {
    faddeev_tail_bound(y, x, one, one, one, 128);
  } catch (const Error&) {
  } catch (const std::runtime_error&) {
    standard = true;
  }
  SIC_CHECK(standard);
  arb_one(x);
  arb_mul_2exp_si(y, one, -2000);
  arb_sub(x, x, y, 4096);
  kind(ErrorKind::precision, [&] { faddeev_strip(output, x, one, 4096); });
  arb_mul_2exp_si(y, one, -1020);
  arb_sub(x, one, y, 4096);
  kind(ErrorKind::precision, [&] { faddeev_strip(output, x, one, 4096); });
  arb_mul_2exp_si(x, one, -100);
  kind(ErrorKind::precision, [&] { faddeev_tail_bound(y, one, x, x, one, 2); });
#ifdef SIC_ENABLE_TEST_HOOKS
  setenv("SIC_TEST_CEIL1_FORCE_PRECISION_REFUSAL", "1", 1);
  kind(ErrorKind::precision, [] { ceil1({5}, QuadraticNumber(1)); });
  unsetenv("SIC_TEST_CEIL1_FORCE_PRECISION_REFUSAL");
#endif
  FILE* disk = std::tmpfile();
  SIC_CHECK(disk != nullptr);
  if (disk) {
    kind(ErrorKind::resource,
         [&] { require_leaf_disk(disk, kUnlimited - sic::support::resources::kGib); });
    close(fileno(disk));
    kind(ErrorKind::io, [&] { require_leaf_disk(disk, 0); });
    std::fclose(disk);
  }
}
void helper_routing() {
  constexpr long n = 16;
  Matrix inverse(n, n), jacobian(n, n), product(n, n);
  acb_mat_one(inverse.get());
  acb_mat_one(jacobian.get());
  ComplexBallVector residual(n), scale(n), result(1);
  for (long r = 0; r < n; ++r) acb_one(residual[r]);
  acb_set_si(result[0], 123);
  for (Fault fault : kFaults) {
    int calls = 0;
    auto factory = [&](int) -> std::unique_ptr<RowWorkerTeam> {
      ++calls;
      inject(fault);
    };
    auto refine = [&] {
      return inner_refinement::propose_vectors(result, inverse.get(), jacobian.get(), residual.v,
                                               scale.v, 128, 1, 4, kUnlimited, factory);
    };
    if (fault == Fault::admission || fault == Fault::startup || fault == Fault::allocation) {
      SIC_CHECK(!refine());
      SIC_CHECK(result.size() == 1 && acb_equal_si(result[0], 123));
    } else
      cause(fault, refine);
    SIC_CHECK(calls == 1);
    int starts = 0;
    auto inverse_factory = [&](int workers) -> std::unique_ptr<RowWorkerTeam> {
      ++starts;
      if (workers > 1) inject(fault);
      return std::make_unique<RowWorkerTeam>(workers);
    };
    auto invert = [&] {
      return parallel_point_inverse(product.get(), jacobian.get(), 128, 4, kUnlimited, false,
                                    inverse_factory);
    };
    if (fault == Fault::admission || fault == Fault::startup) {
      SIC_CHECK(invert());
      SIC_CHECK(starts == 1 && acb_mat_equal(product.get(), inverse.get()));
    } else
      cause(fault, invert);
    // Row callbacks must finish before their captured failure escapes.
    std::atomic<int> entered{0}, exited{0};
    cause(fault, [&] {
      inner_refinement::propose_vectors(result, inverse.get(), jacobian.get(), residual.v, scale.v,
                                        128, 1, 4, kUnlimited, {}, [&](long row) {
                                          ++entered;
                                          struct Exit {
                                            std::atomic<int>& count;
                                            ~Exit() { ++count; }
                                          } exit{exited};
                                          if (!row) inject(fault);
                                        });
    });
    SIC_CHECK(entered > 0 && entered == exited);
  }
  SIC_CHECK(!inner_refinement::propose_vectors(result, inverse.get(), jacobian.get(), residual.v,
                                               scale.v, WORD_MAX / 16, 1, 4, kUnlimited));
  for (slong bits : {0L, WORD_MAX / 16 + 1})
    SIC_CHECK(!inner_refinement::propose_vectors(result, inverse.get(), jacobian.get(), residual.v,
                                                 scale.v, bits, 1, 4, kUnlimited));
  // Admission can reduce concurrency, but never alter the product or accept insufficient space.
  const Bytes equivalent = sic::support::resources::mul(
      n * n,
      sic::support::resources::complex_bytes(sic::support::resources::newton_matrix_bits(128)));
  const Bytes limited =
      sic::support::resources::add(4 * equivalent, product_tiles::scratch(n, 1, 128));
  SIC_CHECK(parallel_ball_product(product.get(), inverse.get(), jacobian.get(), 128, 4, limited));
  SIC_CHECK(acb_mat_equal(product.get(), inverse.get()));
  cause(Fault::admission,
        [&] { parallel_ball_product(product.get(), inverse.get(), jacobian.get(), 128, 4, 1); });
  SIC_CHECK(acb_mat_is_zero(product.get()));
}
void reconstruction_routing() {
  GhostRefiner::Ctx high(8, 1, 192), context(high, 128);
  GhostRootMetadata metadata{8, 1, 0, 1, 128, 192};
  ComplexBallVector state(14), serial(64), actual(64);
  for (size_t q = 0; q < state.size(); ++q) acb_one(state[q]);
  reconstruct_ghost_state(metadata, state.v, context,
                          [&](long y, acb_srcptr x) { acb_set(serial[y], x); });
  for (Fault fault : kFaults) {
    int emits = 0;
    auto emit = [&](long y, acb_srcptr x) {
      ++emits;
      acb_set(actual[y], x);
    };
    auto factory = [&](int) -> std::unique_ptr<RowWorkerTeam> { inject(fault); };
    auto attempt = [&] {
      return try_parallel_ghost_reconstruction(metadata, state.v, context, emit, 4, kUnlimited,
                                               false, factory);
    };
    if (fault == Fault::admission || fault == Fault::startup || fault == Fault::allocation) {
      SIC_CHECK(!attempt());
      SIC_CHECK(emits == 0);
      reconstruct_ghost_state(metadata, state.v, context, emit);
      for (long y = 1; y < 64; ++y) SIC_CHECK(acb_equal(serial[y], actual[y]));
    } else
      cause(fault, attempt);
    std::atomic<int> entered{0}, exited{0};
    cause(fault, [&] {
      try_parallel_ghost_reconstruction(
          metadata, state.v, context,
          [&](long, acb_srcptr) {
            ++entered;
            struct Exit {
              std::atomic<int>& count;
              ~Exit() { ++count; }
            } exit{exited};
            inject(fault);
          },
          4, kUnlimited);
    });
    SIC_CHECK(entered > 0 && entered == exited);
  }
  context.p = metadata.precision = WORD_MAX / 16;
  SIC_CHECK(!try_parallel_ghost_reconstruction(
      metadata, state.v, context, [](long, acb_srcptr) {}, 4, kUnlimited));
}
void observers_and_dispatch(const ComplexBallVector& seed) {
  for (Fault fault : kFaults) {
    for (bool after : {false, true}) {
      GhostRefiner refiner(4, 1, 1);
      refiner.kernel_threads = 4;
      ComplexBallVector output;
      std::string why;
      if (after)
        refiner.after_b0 = [&](const acb_mat_struct*, const std::vector<acb_struct>&, slong, slong,
                               slong) { inject(fault); };
      else
        refiner.memory_observer = [&](const phase_memory::Event&) { inject(fault); };
      cause(fault, [&] { refiner.refine(seed.v, 256, output.v, why); });
      SIC_CHECK(!refiner.vector_workspace && !refiner.memory_trace && !refiner.certified_root);
    }
    sic::support::Reservations pool(64);
    const long classes[]{0, 1};
    int refused = 0, callbacks = 0;
    auto dispatch = [&] {
      phase_memory::dispatch_callbacks(
          std::span(classes), 4, &pool, [&](long) -> Bytes { inject(fault); },
          [&](long, Bytes bytes) {
            SIC_CHECK(bytes == kUnlimited);
            ++refused;
          },
          [&](long, auto) { ++callbacks; });
    };
    if (fault == Fault::overflow) {
      dispatch();
      SIC_CHECK(refused == 2 && !callbacks);
    } else
      cause(fault, dispatch);
    SIC_CHECK(pool.used() == 0);
  }
  phase_memory::Ceilings ceilings;
  ceilings.workers = 1;
  ceilings.source_bits = ceilings.selection_bits = ceilings.inverse_bits =
      ceilings.stored_matrix_bits = ceilings.high_bits = 128;
  ceilings.candidate_rows = 1;
  ceilings.modular = true;
  for (bool overflow : {false, true}) {
    ceilings.dimension = overflow ? sic::support::resources::Count{1} << 63 : 0;
    bool observed = false;
    phase_memory::Trace trace(ceilings, kUnlimited, [&](const phase_memory::Event& event) {
      observed = true;
      SIC_CHECK(!event.forecast_valid);
    });
    trace.enter(phase_memory::Phase::roots);
    SIC_CHECK(observed);
  }
}
#ifdef SIC_ENABLE_TEST_HOOKS
void row_and_product_routing() {
  struct State {
    Fault fault;
    std::atomic<int> calls{0}, entered{0}, exited{0};
  };
  GhostRefiner::Ctx context(8, 1, 128);
  ComplexBallVector v(8), u(8), output(4);
  for (long q = 0; q < 8; ++q) {
    acb_one(v[q]);
    acb_one(u[q]);
  }
  DirectEtaRoot root(8, 128, std::move(v), std::move(u), context.w2d);
  DirectEtaRows cache(1 << 20);
  std::vector<DirectEtaPlan> plans(4);
  for (long q = 0; q < 4; ++q) {
    plans[q].k = 2;
    plans[q].L = 4;
    plans[q].a0 = q % 2;
    plans[q].gamma = q / 2;
  }
  for (Fault fault : kFaults) {
    State state{fault};
    root.failure_context = &state;
    root.before_row = [](void* opaque, long) {
      auto& hook_state = *static_cast<State*>(opaque);
      ++hook_state.entered;
      struct Exit {
        std::atomic<int>& count;
        ~Exit() { ++count; }
      } exit{hook_state.exited};
      inject(hook_state.fault);
    };
    cause(fault, [&] { root.evaluate(plans, output, 128, 4, &cache, "authenticated root"); });
    SIC_CHECK(cache.size() == 0 && cache.bytes() == 0);
    SIC_CHECK(state.entered > 0 && state.entered == state.exited);
  }
  root.before_row = nullptr;
  root.evaluate(plans, output, 128, 4, &cache, "authenticated root");
  SIC_CHECK(cache.size() > 0);
  Matrix left(129, 129), right(129, 129), product(129, 129);
  acb_mat_one(left.get());
  acb_mat_one(right.get());
  for (Fault fault : kFaults) {
    for (const int stage : {0, 1, 2}) {
      State state{fault};
      BallProductHooks hooks;
      hooks.context = &state;
      if (stage == 0)
        hooks.before_admission = [](void* opaque, int) {
          auto& hook_state = *static_cast<State*>(opaque);
          if (++hook_state.calls == 1) inject(hook_state.fault);
        };
      if (stage == 1)
        hooks.team_start = [](void* opaque, int) {
          auto& hook_state = *static_cast<State*>(opaque);
          ++hook_state.calls;
          inject(hook_state.fault);
        };
      if (stage == 2)
        hooks.before_tile = [](void* opaque, long) {
          auto& hook_state = *static_cast<State*>(opaque);
          ++hook_state.entered;
          struct Exit {
            std::atomic<int>& count;
            ~Exit() { ++count; }
          } exit{hook_state.exited};
          inject(hook_state.fault);
        };
      auto run = [&] {
        return parallel_ball_product(product.get(), left.get(), right.get(), 128, 4, kUnlimited,
                                     false, &hooks);
      };
      if ((stage == 0 && fault == Fault::admission) || (stage == 1 && fault == Fault::startup)) {
        SIC_CHECK(run());
        SIC_CHECK(acb_mat_equal(product.get(), left.get()));
        SIC_CHECK(state.calls == (stage == 0 ? 2 : 1));
      } else {
        cause(fault, run);
        SIC_CHECK(acb_mat_is_zero(product.get()));
      }
      SIC_CHECK(state.entered == state.exited);
      if (stage == 2) SIC_CHECK(state.entered > 0);
    }
  }
}
struct Probe {
  const char* operation;
  Fault fault;
  int calls = 0;
  static void run(void* context, const char* operation) {
    auto& self = *static_cast<Probe*>(context);
    if (std::string(self.operation) == "reconstruction_serial" &&
        std::string(operation) == "reconstruction_admission")
      throw AdmissionError("force separately admitted serial reconstruction");
    if (std::string(operation) == self.operation) {
      ++self.calls;
      inject(self.fault);
    }
  }
};
void direct_refiner_routing() {
  for (const char* operation :
       {"vector_team", "linear_forecast", "linear_storage", "native_selection"})
    for (Fault fault : kFaults) {
      Probe probe{operation, fault};
      GhostRefiner refiner(4, 1, 1);
      refiner.kernel_threads = 4;
      refiner.S = {1, 2, 3, 4, 5, 6};
      refiner.Kexp.resize(16);
      refiner.failure_context = &probe;
      refiner.before_operation = Probe::run;
      const std::string op(operation);
      Matrix inverse(6, 6);
      acb_mat_one(inverse.get());
      ComplexBallVector residual(6), state(6), proposal(1);
      for (long q = 0; q < 6; ++q) acb_one(state[q]);
      acb_set_si(proposal[0], 123);
      GhostRefiner::Ctx high(4, 1, 256);
      auto run = [&] {
        if (op == "vector_team") return refiner.vector_workers(64, 128) != nullptr;
        if (op == "native_selection")
          return refiner.select_rows<double>(inverse.get(), 6, 6, refiner.S);
        return refiner.refined_correction(proposal, inverse.get(), residual.v, state.v, high, 128,
                                          1, kUnlimited);
      };
      const bool refused =
          (op == "vector_team" && (fault == Fault::admission || fault == Fault::startup)) ||
          (op == "linear_forecast" && fault == Fault::overflow) ||
          (op == "linear_storage" &&
           (fault == Fault::admission || fault == Fault::startup || fault == Fault::allocation));
      if (refused) {
        SIC_CHECK(!run());
        SIC_CHECK(!refiner.vector_workspace);
        SIC_CHECK(proposal.size() == 1 && acb_equal_si(proposal[0], 123));
      } else
        cause(op == "native_selection" && fault == Fault::startup ? Fault::admission : fault, run);
      SIC_CHECK(probe.calls == 1);
    }
}
void refiner_routing(const ComplexBallVector& seed) {
  sic::support::KernelSettings settings;
  settings["SIC_KRAWCZYK_FIRST_BITS"] = "80";
  sic::support::KernelSettingsScope scope(&settings);
  for (const char* operation : {"source_forecast", "workspace_forecast", "modular_selection",
                                "inverse", "correction_forecast", "radius_gate", "ball_product",
                                "reconstruction_admission", "reconstruction_serial"})
    for (Fault fault : kFaults) {
      Probe probe{operation, fault};
      GhostRefiner refiner(4, 1, 1);
      refiner.phase_admission = std::string(operation) == "source_forecast";
      refiner.kernel_threads = 4;
      refiner.matrix_precision_hint = 192;
      refiner.failure_context = &probe;
      refiner.before_operation = Probe::run;
      ComplexBallVector output;
      std::string why;
      const std::string op(operation);
      const bool forecast = op == "source_forecast" || op == "workspace_forecast";
      const bool numerical_refusal =
          (forecast && fault == Fault::overflow) ||
          ((op == "modular_selection") && (fault == Fault::admission || fault == Fault::startup)) ||
          ((op == "inverse" || op == "ball_product" || op == "reconstruction_serial") &&
           fault == Fault::admission);
      const bool cheap_fallback =
          (op == "correction_forecast" && fault == Fault::overflow) ||
          ((op == "radius_gate" || op == "reconstruction_admission") && fault == Fault::admission);
      auto run = [&] { return refiner.refine(seed.v, 256, output.v, why); };
      if (numerical_refusal || cheap_fallback) {
        const bool accepted = run();
        if (accepted != cheap_fallback) std::cerr << operation << ": " << why << '\n';
        SIC_CHECK(accepted == cheap_fallback);
        if (accepted) SIC_CHECK(refiner.achieved_bits >= 256);
        if (op == "ball_product") {
          SIC_CHECK(why.find("Krawczyk product refused") != std::string::npos);
          SIC_CHECK(probe.calls == 1);
        }
        if (forecast)
          SIC_CHECK(refiner.memory_refusal == phase_memory::Refusal::class_slot);
        else if (numerical_refusal)
          SIC_CHECK(refiner.memory_refusal == phase_memory::Refusal::helper);
        else
          SIC_CHECK(refiner.memory_refusal == phase_memory::Refusal::none);
      } else
        cause(fault, run);
      if (!probe.calls) std::cerr << "probe not reached: " << operation << '\n';
      SIC_CHECK(probe.calls > 0);
      SIC_CHECK(!refiner.vector_workspace && !refiner.memory_trace);
    }
  settings["SIC_KRAWCZYK_FIRST_BITS"] = "1";
  GhostRefiner invalid(4, 1, 1);
  ComplexBallVector rejected;
  std::string reason;
  kind(ErrorKind::invalid_argument, [&] { invalid.refine(seed.v, 256, rejected.v, reason); });
  settings["SIC_KRAWCZYK_FIRST_BITS"] = "80";
  // Advisory debug parsing keeps its documented presence-selected behavior.
  settings["SIC_GHOST_DEBUG"] = "bad integer";
  GhostRefiner debug(4, 1, 1);
  ComplexBallVector output;
  std::string why;
  SIC_CHECK(debug.refine(seed.v, 256, output.v, why));
}
#endif
}  // namespace

int main() {
  flint_set_num_threads(4);
  shared_dispatch_failures();
  proposal_categories();
  numerical_categories();
  helper_routing();
  reconstruction_routing();
  Arithmetic arithmetic(2);
  Group group(arithmetic);
  group.generate_standard();
  group.close(arithmetic.h() * 8);
  Table table(arithmetic, 192, 4);
  table.build_class(0);
  ComplexBallVector seed(16);
  for (long y = 1; y < 16; ++y) table.read(seed[y], group.ghost_point(y / 4, y % 4));
  observers_and_dispatch(seed);
#ifdef SIC_ENABLE_TEST_HOOKS
  row_and_product_routing();
  direct_refiner_routing();
  refiner_routing(seed);
#endif
  flint_cleanup_master();
  return sic_test::report();
}
