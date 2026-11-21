#include <unistd.h>

#include <atomic>
#include <fstream>

#include "assertions.hpp"
#include "exact/certification/newton/newton_linear_refinement.hpp"

namespace inner_refinement = sic::exact::inner_refinement;

static void relative_error(mag_t result, const sic::support::ComplexBallVector& value,
                           const sic::support::ComplexBallVector& exact) {
  acb_t difference;
  acb_init(difference);
  mag_t error, weight;
  mag_init(error);
  mag_init(weight);
  mag_zero(result);
  for (size_t row = 0; row < value.size(); ++row) {
    acb_sub(difference, value[row], exact[row], 512);
    acb_get_mag(error, difference);
    acb_get_mag(weight, exact[row]);
    if (mag_is_zero(weight)) mag_one(weight);
    mag_div(error, error, weight);
    mag_max(result, result, error);
  }
  mag_clear(error);
  mag_clear(weight);
  acb_clear(difference);
}

static void compare_system(long count, bool nonnormal, bool scaled) {
  acb_mat_t matrix, inverse, expected_inverse;
  acb_mat_init(matrix, count, count);
  acb_mat_init(inverse, count, count);
  acb_mat_init(expected_inverse, count, count);
  sic::support::ComplexBallVector exact(count), residual(count), original(count), serial, parallel,
      factor(1);
  for (long row = 0; row < count; ++row) {
    auto diagonal = acb_mat_entry(matrix, row, row);
    acb_one(diagonal);
    arb_set_si(acb_imagref(diagonal), row % 2 ? -1 : 1);
    if (nonnormal && row + 1 < count) {
      auto value = acb_mat_entry(matrix, row, row + 1);
      acb_set_si(value, 2);
      arb_set_si(acb_imagref(value), 3);
      acb_mul_2exp_si(value, value, scaled ? -28 : 12);
    }
    acb_set_si(exact[row], row + 1);
    arb_set_si(acb_imagref(exact[row]), row % 3 - 1);
    acb_mul_2exp_si(exact[row], exact[row], scaled ? 40 * row - 200 : -4);
  }
  SIC_CHECK(acb_mat_inv(expected_inverse, matrix, 512));
  acb_mat_get_mid(inverse, expected_inverse);
  acb_one(factor[0]);
  acb_mul_2exp_si(factor[0], factor[0], -8);
  acb_neg(factor[0], factor[0]);
  acb_add_si(factor[0], factor[0], 1, 512);
  acb_mat_scalar_mul_acb(inverse, inverse, factor[0], 512);
  for (long row = 0; row < count; ++row) {
    acb_dot(residual[row], nullptr, 0, acb_mat_entry(matrix, row, 0), 1, exact.v.data(), 1, count,
            512);
    arb_add_error_2exp_si(acb_realref(residual[row]), -400);
    acb_set(original[row], residual[row]);
  }
  mag_t previous, current, threshold;
  mag_init(previous);
  mag_init(current);
  mag_init(threshold);
  for (int passes = 0; passes <= 2; ++passes) {
    SIC_CHECK(inner_refinement::propose(serial, inverse, matrix, residual, exact, 256, passes, 1,
                                        inner_refinement::scratch_bytes(count, 256, 1)));
    SIC_CHECK(inner_refinement::propose(parallel, inverse, matrix, residual, exact, 256, passes, 4,
                                        inner_refinement::scratch_bytes(count, 256, 4)));
    SIC_CHECK(flint_get_num_threads() == 4);
    for (long row = 0; row < count; ++row) {
      SIC_CHECK(acb_equal(serial[row], parallel[row]));
      SIC_CHECK(acb_is_exact(parallel[row]));
      SIC_CHECK(acb_equal(residual[row], original[row]));
    }
    relative_error(current, parallel, exact);
    if (passes) {
      mag_mul_2exp_si(threshold, previous, -6);
      SIC_CHECK(mag_cmp(current, threshold) < 0);
    } else {
      mag_set_ui_2exp_si(threshold, 1, -6);
      SIC_CHECK(mag_cmp(current, threshold) < 0);
    }
    mag_set(previous, current);
    // Destination radii from another use cannot survive the next proposal.
    for (long row = 0; row < count; ++row) arb_add_error_2exp_si(acb_realref(parallel[row]), -2);
  }
  mag_clear(previous);
  mag_clear(current);
  mag_clear(threshold);
  acb_mat_clear(matrix);
  acb_mat_clear(inverse);
  acb_mat_clear(expected_inverse);
}

static void refusal_and_stop() {
  constexpr long kCount = 16;
  acb_mat_t matrix, inverse;
  acb_mat_init(matrix, kCount, kCount);
  acb_mat_init(inverse, kCount, kCount);
  acb_mat_one(matrix);
  acb_mat_one(inverse);
  sic::support::ComplexBallVector residual(kCount), scale(kCount), output(1), old_output(1);
  for (long row = 0; row < kCount; ++row) {
    acb_set_si(residual[row], row + 1);
    acb_one(scale[row]);
  }
  acb_set_si(output[0], 937);
  acb_set(old_output[0], output[0]);
  const auto scratch = inner_refinement::scratch_bytes(kCount, 256, 4);
  SIC_CHECK(!inner_refinement::propose(output, inverse, matrix, residual, scale, 256, 2, 4, 1));
  SIC_CHECK(output.size() == 1 && acb_equal(output[0], old_output[0]));
  SIC_CHECK(!inner_refinement::propose(
      output, inverse, matrix, residual, scale, 256, 2, 4, scratch,
      [](int workers) -> std::unique_ptr<sic::exact::RowWorkerTeam> {
        SIC_CHECK(workers == 4);
        throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
      }));
  SIC_CHECK(output.size() == 1 && acb_equal(output[0], old_output[0]));
  acb_indeterminate(residual[0]);
  SIC_CHECK(
      !inner_refinement::propose(output, inverse, matrix, residual, scale, 256, 2, 4, scratch));
  acb_one(residual[0]);
  arb_add_error_2exp_si(acb_realref(acb_mat_entry(matrix, 0, 0)), -32);
  SIC_CHECK(
      !inner_refinement::propose(output, inverse, matrix, residual, scale, 256, 2, 4, scratch));
  acb_mat_one(matrix);
  for (long row = 0; row < kCount; ++row) acb_set_si(acb_mat_entry(inverse, row, row), 10);
  SIC_CHECK(
      !inner_refinement::propose(output, inverse, matrix, residual, scale, 256, 2, 4, scratch));
  SIC_CHECK(output.size() == 1 && acb_equal(output[0], old_output[0]));
  acb_mat_zero(inverse);
  SIC_CHECK(
      inner_refinement::propose(output, inverse, matrix, residual, scale, 256, 2, 4, scratch));
  for (long row = 0; row < kCount; ++row) SIC_CHECK(acb_is_zero(output[row]));
  // Success is a finite proposal, not evidence that a singular Y solves F.
  acb_mat_one(inverse);
  acb_zero(scale[0]);  // A zero coordinate uses a strictly positive diagnostic weight.
  SIC_CHECK(
      inner_refinement::propose(output, inverse, matrix, residual, scale, 256, 2, 4, scratch));
  for (long row = 0; row < kCount; ++row) SIC_CHECK(acb_equal(output[row], residual[row]));
  output = sic::support::ComplexBallVector(1);
  acb_set(output[0], old_output[0]);
  std::atomic<long> visited{0};
  bool worker_error = false;
  try {
    inner_refinement::propose(output, inverse, matrix, residual, scale, 256, 2, 4, scratch, {},
                              [&](long row) {
                                ++visited;
                                if (row == 0) throw std::runtime_error("worker fault");
                              });
  } catch (const std::runtime_error& error) {
    worker_error = std::string(error.what()) == "worker fault";
  }
  SIC_CHECK(worker_error);
  SIC_CHECK(visited > 0);
  SIC_CHECK(flint_get_num_threads() == 4);
  SIC_CHECK(output.size() == 1 && acb_equal(output[0], old_output[0]));
  char path[] = "/tmp/sic-linear-refinement-stop-XXXXXX";
  const int descriptor = mkstemp(path);
  SIC_CHECK(descriptor >= 0);
  if (descriptor >= 0) {
    close(descriptor);
    sic::support::ScopedStopFile stop_scope(path);
    bool caught = false;
    try {
      inner_refinement::propose(output, inverse, matrix, residual, scale, 256, 2, 4, scratch);
    } catch (const std::runtime_error&) {
      caught = true;
    }
    SIC_CHECK(caught);
    SIC_CHECK(flint_get_num_threads() == 4);
    unlink(path);
    visited = 0;
    caught = false;
    try {
      inner_refinement::propose(output, inverse, matrix, residual, scale, 256, 2, 4, scratch, {},
                                [&](long) {
                                  if (visited.fetch_add(1) == 0) {
                                    std::ofstream marker(path);
                                  }
                                });
    } catch (const std::runtime_error&) {
      caught = true;
    }
    SIC_CHECK(caught);
    SIC_CHECK(visited > 0);
    SIC_CHECK(flint_get_num_threads() == 4);
    SIC_CHECK(output.size() == 1 && acb_equal(output[0], old_output[0]));
    unlink(path);
  }
  acb_mat_clear(matrix);
  acb_mat_clear(inverse);
}

int main() {
  flint_set_num_threads(4);
  for (long count : {2, 6, 12})
    for (bool nonnormal : {false, true})
      for (bool scaled : {false, true}) compare_system(count, nonnormal, scaled);
  refusal_and_stop();
  flint_cleanup_master();
  return sic_test::report();
}
