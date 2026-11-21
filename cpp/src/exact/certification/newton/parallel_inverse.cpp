// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/newton/parallel_inverse.hpp"

#include "exact/certification/newton/double_double_inverse.hpp"
#include "exact/runtime/parallel.hpp"
#include "sic/support/flint_raii.hpp"
#include "support/resources.hpp"

namespace sic::exact {
int parallel_point_inverse(acb_mat_t output, const acb_mat_t input, slong precision, int workers,
                           sic::support::resources::Bytes workspace_limit, bool verbose,
                           const InverseTeamFactory& factory, PointInverseArithmetic arithmetic) {
  const long n = acb_mat_nrows(input);
  if (n <= 0 || acb_mat_ncols(input) != n || acb_mat_nrows(output) != n ||
      acb_mat_ncols(output) != n || precision < 2 || precision > WORD_MAX / 16 || workers <= 0 ||
      (arithmetic != PointInverseArithmetic::arf &&
       arithmetic != PointInverseArithmetic::double_double))
    throw std::invalid_argument("invalid point inverse dimensions, precision or workers");
  const auto address = [](const acb_mat_t value, bool last) {
    return reinterpret_cast<std::uintptr_t>(acb_mat_entry(
        value, last ? acb_mat_nrows(value) - 1 : 0, last ? acb_mat_ncols(value) - 1 : 0));
  };
  if (address(output, false) <= address(input, true) &&
      address(input, false) <= address(output, true))
    throw std::invalid_argument("point inverse input and output must not overlap");
  struct OutputGuard {
    acb_mat_struct* value;
    bool accepted = false;
    ~OutputGuard() {
      if (!accepted) acb_mat_zero(value);
    }
  } publish{output};
  for (long r = 0; r < n; ++r)
    for (long c = 0; c < n; ++c) {
      const auto value = acb_mat_entry(input, r, c);
      if (!acb_is_finite(value)) return 0;
      for (auto component : {acb_realref(value), acb_imagref(value)}) {
        const auto midpoint = arb_midref(component);
        if (arf_bits(midpoint) > precision ||
            fmpz_bits(ARF_EXPREF(midpoint)) > (ulong)std::max<slong>(precision, FLINT_BITS))
          throw sic::support::resources::AdmissionError(
              "inverse input storage exceeds declared precision");
      }
    }
  constexpr long kTileColumns = 8;
  const long tasks = 1 + (n - 1) / kTileColumns;
  int active = std::min<long>(workers, tasks);
  const auto equivalent =
      sic::support::resources::mul(sic::support::resources::mul(n, n),
                                   sic::support::resources::complex_bytes(
                                       sic::support::resources::newton_matrix_bits(precision)));
  // The native kernel's two dense complex-DD owners (64 N² bytes) and
  // three N-slong vectors fit inside seven future complex equivalents.
  const auto native_bytes = sic::support::resources::add(
      sic::support::resources::mul(sic::support::resources::mul(n, n), 8 * sizeof(double)),
      sic::support::resources::mul(n, 3 * sizeof(slong)));
  if (native_bytes > sic::support::resources::mul(7, equivalent))
    throw sic::support::resources::AdmissionError("native inverse exceeds matrix allowance");
  // Keep the existing eight-matrix allocation forecast, with explicit new
  // scratch. This is not a universal bound for every FLINT implementation.
  // J is already populated. Y may contain only headers, so charge all seven
  // future equivalents, including its prospective precision-sized payload.
  for (;;) {
    const auto scratch = sic::support::resources::inverse_column_scratch(n, active, precision);
    try {
      if (sic::support::resources::add(sic::support::resources::mul(8, equivalent), scratch) >
          workspace_limit)
        throw sic::support::resources::AdmissionError("column inverse exceeds admitted workspace");
      sic::support::resources::Budget().require_additional(
          "point inverse matrices and column scratch",
          sic::support::resources::add(sic::support::resources::mul(7, equivalent), scratch));
      break;
    } catch (const sic::support::resources::AdmissionError&) {
      if (active == 1) throw;
      active = std::max(1, active / 2);
    }
  }
  auto check_stop = [] { sic::support::check_stop(); };
  check_stop();
  auto accept = [&] {
    check_stop();
    for (long r = 0; r < n; ++r)
      for (long c = 0; c < n; ++c)
        if (!acb_is_finite(acb_mat_entry(output, r, c))) return 0;
    acb_mat_get_mid(output, output);
    check_stop();
    publish.accepted = true;
    return 1;
  };
  std::unique_ptr<RowWorkerTeam> team;
  try {
    team = factory ? factory(active) : std::make_unique<RowWorkerTeam>(active);
    if (team) team->prepare();
  } catch (const std::system_error&) {
    // RowWorkerTeam joins partially constructed teams before throwing.
    // Run the same panels/column tiles serially: startup availability must
    // not choose a different numerical algorithm or reduction order.
    if (verbose)
      sic::support::progressf("column inverse: worker startup refused; serial execution\n");
    check_stop();
    active = 1;
    team = std::make_unique<RowWorkerTeam>(active);
  } catch (const sic::support::resources::AdmissionError&) {
    // A second, fresher stack admission can lose to shared-host pressure.
    // A one-worker team owns no additional threads or worker stacks.
    if (verbose)
      sic::support::progressf("column inverse: worker resources refused; serial execution\n");
    check_stop();
    active = 1;
    team = std::make_unique<RowWorkerTeam>(active);
  }
  if (!team) throw std::logic_error("inverse worker factory returned no team");
  if (arithmetic == PointInverseArithmetic::double_double) {
    const auto start = std::chrono::steady_clock::now();
    const bool result = double_double_inverse(output, input, *team);
    if (verbose)
      sic::support::progressf(
          "double-double inverse: rows=%ld workers=%d success=%d elapsed=%.3fs\n", n, active,
          int(result),
          std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    check_stop();
    if (result) {
      team.reset();
      return accept();
    }
    // The native arrays have been destroyed. Retry the range-safe Arb path
    // at the original precision before allowing any numerical fallback.
    if (verbose) sic::support::progressf("double-double inverse refused; original Arb retry\n");
  }
  sic::support::flint::AcbMat lu(n, n);
  std::vector<slong> permutation(n);
  using Clock = std::chrono::steady_clock;
  check_stop();
  auto start = Clock::now();
  if (verbose)
    sic::support::progressf("column inverse LU start: rows=%ld bits=%ld solve_workers=%d\n", n,
                            (long)precision, active);
  // A fixed worker cap makes recursive LU products independent of the
  // class's thread share. It only lowers TLS capacity, never the global pool.
  int result;
  {
    FlintWorkerCap serial_lu(1);
    result = acb_mat_approx_lu(permutation.data(), lu, input, precision);
  }
  if (verbose)
    sic::support::progressf("column inverse LU done: success=%d elapsed=%.2fs\n", result,
                            std::chrono::duration<double>(Clock::now() - start).count());
  check_stop();
  if (!result) return 0;
  acb_mat_one(output);
  start = Clock::now();
  team->for_each(tasks, [&](long task) {
    check_stop();
    const long first = task * kTileColumns, end = std::min(n, first + kTileColumns);
    acb_mat_t window;
    acb_mat_window_init(window, output, 0, first, n, end);
    struct ClearWindow {
      acb_mat_struct* value;
      ~ClearWindow() { acb_mat_window_clear(value); }
    } clear_window{window};
    // Width <=8 forces classical triangular solves, without per-worker
    // recursive copies or packed copies of the shared LU matrix.
    acb_mat_approx_solve_lu_precomp(window, permutation.data(), lu, window, precision);
  });
  team.reset();
  check_stop();
  if (verbose)
    sic::support::progressf("column inverse solves done: workers=%d tile_columns=8 elapsed=%.2fs\n",
                            active, std::chrono::duration<double>(Clock::now() - start).count());
  return accept();
}

}  // namespace sic::exact
