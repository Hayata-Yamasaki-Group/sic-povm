// SPDX-License-Identifier: AGPL-3.0-or-later
#include <sched.h>

#include <cstring>
#include <source_location>

#include "assertions.hpp"
#include "exact/analytic/table.hpp"
#include "exact/analytic/table/quadrature.hpp"
#include "exact/certification/newton/newton_linear_refinement.hpp"
#include "exact/certification/newton/parallel_inverse.hpp"
#include "exact/group/group.hpp"
#include "exact/runtime/parallel.hpp"
#include "support/admitted_worker_team.hpp"
#include "support/resources/checked_bytes.hpp"

namespace {
using namespace sic::exact;
using sic::ErrorKind;
std::atomic<bool> affinity_fault{false};
std::atomic<int> growth_hits{0};
template <class F>
void category(ErrorKind expected, F&& action,
              std::source_location at = std::source_location::current()) {
  bool caught = false;
  try {
    action();
  } catch (const sic::Error& error) {
    caught = error.kind() == expected;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "line %u: %s\n", at.line(), error.what());
  }
  SIC_CHECK(caught);
}
template <class E, class F>
void standard(F&& action) {
  bool caught = false;
  try {
    action();
  } catch (const sic::Error&) {
  } catch (const E&) {
    caught = true;
  }
  SIC_CHECK(caught);
}
struct Matrix {
  acb_mat_t v;
  explicit Matrix(long n) { acb_mat_init(v, n, n); }
  ~Matrix() { acb_mat_clear(v); }
};
void regrowth() {
  Matrix input(16), output(16);
  acb_mat_one(input.v);
  sic::support::ComplexBallVector rhs(16), scale(16), proposal(1);
  for (long row = 0; row < 16; ++row) acb_one(rhs[row]);
  sic::support::KernelSettings settings;
  sic::support::KernelSettingsScope scope(&settings);
  for (int fault : {1, 2}) {
    auto factory = [&](int workers) {
      auto team = std::make_unique<RowWorkerTeam>(workers);
      { RowWorkerTeam temporary(workers); }
      SIC_CHECK(sic::support::local_worker_team().size() == 1);
      if (fault == 1) {
        settings["SIC_MEMORY_MB"] = "0.000001";
        bool refused = false;
        try {
          team->prepare();
        } catch (const sic::support::resources::AdmissionError&) {
          refused = true;
        }
        SIC_CHECK(refused);
      } else
        affinity_fault = true;
      return team;
    };
    for (auto arithmetic : {PointInverseArithmetic::arf, PointInverseArithmetic::double_double}) {
      const auto before = growth_hits.load();
      SIC_CHECK(parallel_point_inverse(output.v, input.v, 128, 4, UINT64_MAX, false, factory,
                                       arithmetic));
      SIC_CHECK(acb_mat_equal(output.v, input.v));
      SIC_CHECK(growth_hits == before + (fault == 2));
      SIC_CHECK(!affinity_fault);
      settings.erase("SIC_MEMORY_MB");
    }
    const auto before = growth_hits.load();
    proposal = sic::support::ComplexBallVector(1);
    acb_set_si(proposal[0], 123);
    SIC_CHECK(!inner_refinement::propose(proposal, input.v, input.v, rhs, scale, 128, 1, 4,
                                         UINT64_MAX, factory));
    SIC_CHECK(proposal.size() == 1 && acb_equal_si(proposal[0], 123));
    SIC_CHECK(growth_hits == before + (fault == 2));
    SIC_CHECK(!affinity_fault);
    settings.erase("SIC_MEMORY_MB");
    SIC_CHECK(
        inner_refinement::propose(proposal, input.v, input.v, rhs, scale, 128, 1, 4, UINT64_MAX));
    SIC_CHECK(proposal.size() == 16);
  }
}
void categories() {
  category(ErrorKind::construction, [] {
    Arithmetic altered(2);
    altered.eps = QuadraticNumber(2);
    Group invalid(altered);
  });
  Arithmetic arithmetic(2);
  Group group(arithmetic);
  group.generate_standard();
  category(ErrorKind::resource, [&] { group.close(2, 1); });
  standard<std::invalid_argument>([&] { group.close(2, 0); });
  standard<std::runtime_error>([&] { group.chi_of_norm(5); });
  standard<std::runtime_error>([&] { group.element_of(GElem{{}, 1, 1}); });
  standard<std::invalid_argument>([] { RowWorkerTeam invalid(0); });
  RowWorkerTeam rows(2);
  standard<std::invalid_argument>([&] { rows.for_each(-1, [](long) {}); });
  sic::support::KernelSettings invalid{{"SIC_PARALLEL_GRAIN", "0"}};
  {
    sic::support::KernelSettingsScope scope(&invalid);
    category(ErrorKind::invalid_argument, [] { parallel_for(8, 2, [](long) {}); });
  }
  // A standard callback exception remains standard after dispatch and drain.
  standard<std::system_error>([&] {
    rows.for_each(8, [](long) {
      throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
    });
  });
  Table table(arithmetic, 128, 1);
  auto view = table.view();
  sic::support::flint::Arb b, eta;
  sic::support::flint::Mag error;
  table_cleanup::Nodes nodes;
  arb_one(b);
  arb_one(eta);
  arb_mul_2exp_si(eta, eta, -1);
  arb_add_error_2exp_si(eta, -64);
  standard<std::invalid_argument>([&] { view.gl_nodes(nodes.values, b, eta, 160, 0, error); });
  arb_one(eta);
  arb_mul_2exp_si(eta, eta, -1);
  // The certified constructor accepts dyadic b, while its double proposal underflows.
  view.prec = 1000000000;
  category(ErrorKind::resource, [&] { view.gl_nodes(nodes.values, b, eta, 160, 0, error); });
  view.prec = 128;
  arb_set_ui(eta, 3);
  category(ErrorKind::precision, [&] { view.gl_nodes(nodes.values, b, eta, 160, 0, error); });
  arb_mul_2exp_si(b, b, -2000);
  arb_set(eta, b);
  category(ErrorKind::precision, [&] { view.gl_nodes(nodes.values, b, eta, 4096, 0, error); });
}
}  // namespace

extern "C" int __real_sched_setaffinity(pid_t, size_t, const cpu_set_t*);
extern "C" int __wrap_sched_setaffinity(pid_t pid, size_t bytes, const cpu_set_t* mask) {
  if (affinity_fault.exchange(false)) {
    ++growth_hits;
    errno = EAGAIN;
    return -1;
  }
  return __real_sched_setaffinity(pid, bytes, mask);
}

int main() {
  flint_set_num_threads(4);
  regrowth();
  categories();
  sic::support::release_local_worker_teams();
  flint_cleanup_master();
  return sic_test::report();
}
