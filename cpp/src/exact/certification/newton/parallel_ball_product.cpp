// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/newton/parallel_ball_product.hpp"

#include "exact/runtime/memory/phase_accounting.hpp"
#include "exact/runtime/memory/product_tiles.hpp"

namespace sic::exact {
bool parallel_ball_product(acb_mat_t output, const acb_mat_t left, const acb_mat_t right,
                           slong precision, int workers,
                           sic::support::resources::Bytes workspace_limit, bool verbose,
                           const BallProductHooks* hooks) {
#ifndef SIC_ENABLE_TEST_HOOKS
  (void)hooks;
#endif
  const long n = acb_mat_nrows(left);
  if (n <= 0 || acb_mat_ncols(left) != n || acb_mat_nrows(right) != n ||
      acb_mat_ncols(right) != n || acb_mat_nrows(output) != n || acb_mat_ncols(output) != n ||
      precision < 2 || precision > WORD_MAX / 16 || workers <= 0)
    throw std::invalid_argument("invalid ball product dimensions, precision or workers");
  const auto address = [](const acb_mat_t value, bool last) {
    return reinterpret_cast<std::uintptr_t>(acb_mat_entry(
        value, last ? acb_mat_nrows(value) - 1 : 0, last ? acb_mat_ncols(value) - 1 : 0));
  };
  const auto overlaps = [&](const acb_mat_t input) {
    return address(output, false) <= address(input, true) &&
           address(input, false) <= address(output, true);
  };
  // Different window wrappers may still share storage. Reject before creating
  // an output guard, so refusal never clears an aliased input.
  if (overlaps(left) || overlaps(right))
    throw std::invalid_argument("ball product output and inputs must not overlap");
  struct OutputGuard {
    acb_mat_struct* value;
    bool accepted = false;
    ~OutputGuard() {
      if (!accepted) acb_mat_zero(value);
    }
  } publish{output};
  FlintWorkerCap cap(workers);
  int active = std::min<long>(flint_get_num_threads(),
                              n <= product_tiles::kSmallMatrix ? n : product_tiles::count(n));
  if (n > product_tiles::kSmallMatrix) active = std::min(active, product_tiles::kMaximumActive);
  sic::support::resources::BitCount storage_bits = precision;
  const auto maximum_storage = sic::support::resources::newton_matrix_bits(precision);
  for (auto input : {left, right})
    for (long r = 0; r < n; ++r)
      for (long c = 0; c < n; ++c) {
        const auto value = acb_mat_entry(input, r, c);
        if (!acb_is_finite(value)) return false;
        for (auto component : {acb_realref(value), acb_imagref(value)}) {
          const auto midpoint = arb_midref(component);
          const auto bits = (sic::support::resources::BitCount)arf_bits(midpoint);
          if (bits > maximum_storage || !ARB_IS_LAGOM(component))
            throw sic::support::resources::AdmissionError(
                "ball product input storage exceeds declared precision");
          storage_bits = std::max(storage_bits, bits);
        }
      }
  auto scratch = product_tiles::scratch(n, active, storage_bits);
  const auto equivalent =
      sic::support::resources::mul(sic::support::resources::mul(n, n),
                                   sic::support::resources::complex_bytes(
                                       sic::support::resources::newton_matrix_bits(storage_bits)));
  // Memory selects concurrency only. Even the smallest admission executes the
  // same fixed windows; no memory-dependent numerical fallback is substituted.
  for (;;) {
    try {
#ifdef SIC_ENABLE_TEST_HOOKS
      if (hooks && hooks->before_admission) hooks->before_admission(hooks->context, active);
#endif
      scratch = product_tiles::scratch(n, active, storage_bits);
      // The caller retains Y/M0/J/Mloc. The helper owns only its output
      // growth and tile scratch; the fresh process check below is unchanged.
      if (sic::support::resources::add(
              sic::support::resources::mul(phase_memory::kCertificationMatrixOwners, equivalent),
              scratch) > workspace_limit)
        throw sic::support::resources::AdmissionError("ball product exceeds admitted workspace");
      sic::support::resources::Budget().require_additional(
          "Krawczyk product output and tile scratch",
          sic::support::resources::add(equivalent, scratch));
      break;
    } catch (const sic::support::resources::AdmissionError&) {
      if (active == 1) throw;
      --active;
    }
  }
  auto check_stop = [] { sic::support::check_stop(); };
  check_stop();
  const auto start = std::chrono::steady_clock::now();
  if (verbose)
    sic::support::progressf(
        "Krawczyk product start: rows=%ld bits=%ld worker_cap=%d input_bits=%llu tile=%ld\n", n,
        (long)precision, active, (unsigned long long)storage_bits,
        n <= product_tiles::kSmallMatrix ? 0L : product_tiles::kSide);
  if (n <= product_tiles::kSmallMatrix) {
    FlintWorkerCap admitted(active);
#ifdef SIC_ENABLE_TEST_HOOKS
    if (hooks && hooks->before_tile) hooks->before_tile(hooks->context, 0);
#endif
    check_stop();
    acb_mat_mul_threaded(output, left, right, precision);
#ifdef SIC_ENABLE_TEST_HOOKS
    if (hooks && hooks->after_tile) hooks->after_tile(hooks->context, 0);
#endif
  } else
    tiled_ball_product(output, left, right, precision, active, hooks);
  check_stop();
  for (long r = 0; r < n; ++r)
    for (long c = 0; c < n; ++c)
      if (!acb_is_finite(acb_mat_entry(output, r, c))) return false;
  if (verbose)
    sic::support::progressf(
        "Krawczyk product done: elapsed=%.2fs\n",
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
  publish.accepted = true;
  return true;
}

}  // namespace sic::exact
