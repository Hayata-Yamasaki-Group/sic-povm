// SPDX-License-Identifier: AGPL-3.0-or-later
#include <flint/fmpq.h>

#include <iostream>
#include <source_location>
#include <stdexcept>
#include <utility>
#include <vector>

#include "sic/bounds/overlap_bounds.hpp"

namespace {
using sic::bounds::BallVerificationLimits;
using sic::bounds::bound_wh_overlap_deviations;
unsigned checks = 0;
void check(bool condition, const std::source_location where = std::source_location::current()) {
  ++checks;
  if (!condition)
    throw std::runtime_error("ball verification regression failed at line " +
                             std::to_string(where.line()));
}
template <class F>
void refuses(F operation) {
  bool failed = false;
  try {
    operation();
  } catch (const std::exception&) {
    failed = true;
  }
  check(failed);
}
struct Complex {
  std::vector<acb_struct> values;
  explicit Complex(std::size_t n) : values(n) {
    for (auto& v : values) acb_init(&v);
  }
  ~Complex() {
    for (auto& v : values) acb_clear(&v);
  }
  Complex(const Complex&) = delete;
  Complex& operator=(const Complex&) = delete;
  acb_ptr operator[](std::size_t i) { return &values[i]; }
  acb_srcptr operator[](std::size_t i) const { return &values[i]; }
  acb_srcptr data() const { return values.data(); }
  std::size_t size() const { return values.size(); }
};
struct Real {
  arb_t value;
  Real() { arb_init(value); }
  ~Real() { arb_clear(value); }
};

// Independent direct matrix element with the actual tau phase, no DFT row.
void direct_maximum(arb_ptr maximum, const Complex& vector) {
  constexpr slong kBits = 512;
  const auto d = static_cast<ulong>(vector.size());
  Complex work(6);
  Real norm, denominator, scalar, target;
  for (ulong j = 0; j < d; ++j) {
    acb_abs(scalar.value, vector[j], kBits);
    arb_sqr(scalar.value, scalar.value, kBits);
    arb_add(norm.value, norm.value, scalar.value, kBits);
  }
  arb_sqr(denominator.value, norm.value, kBits);
  arb_set_ui(target.value, d + 1);
  arb_inv(target.value, target.value, kBits);
  acb_unit_root(work[0], 2 * d, kBits);
  acb_neg(work[1], work[0]);                  // tau = -exp(pi*i/d)
  acb_mul(work[0], work[0], work[0], kBits);  // omega
  arb_zero(maximum);
  for (ulong a = 0; a < d; ++a)
    for (ulong b = 0; b < d; ++b) {
      if (!a && !b) continue;
      acb_zero(work[2]);
      for (ulong j = 0; j < d; ++j) {
        acb_conj(work[3], vector[(j + a) % d]);
        acb_mul(work[3], work[3], vector[j], kBits);
        acb_pow_ui(work[4], work[0], b * j, kBits);
        acb_mul(work[3], work[3], work[4], kBits);
        acb_add(work[2], work[2], work[3], kBits);
      }
      acb_pow_ui(work[5], work[1], a * b, kBits);
      acb_mul(work[2], work[2], work[5], kBits);
      acb_abs(scalar.value, work[2], kBits);
      arb_sqr(scalar.value, scalar.value, kBits);
      arb_div(scalar.value, scalar.value, denominator.value, kBits);
      arb_sub(scalar.value, scalar.value, target.value, kBits);
      arb_abs(scalar.value, scalar.value);
      arb_max(maximum, maximum, scalar.value, kBits);
      check(arb_is_finite(scalar.value));
    }
}

void small_direct_controls() {
  for (std::size_t d : {2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 12U, 16U, 32U}) {
    Complex vector(d);
    for (std::size_t j = 0; j < d; ++j) {
      acb_set_si(vector[j], static_cast<long>((j * j + 3 * j + 5) % 17) - 8);
      arb_set_si(acb_imagref(vector[j]), static_cast<long>((2 * j + 3) % 11) - 5);
    }
    const auto result = bound_wh_overlap_deviations(vector.data(), d);
    Real direct;
    direct_maximum(direct.value, vector);
    check(arb_contains(result.maximum_squared_overlap_deviation(), direct.value));
    check(arb_is_positive(result.squared_norm()));
    check(result.info().checked_nonidentity_labels == d * d - 1);
    check(result.info().dft_rows == d);
    check(result.info().dft_points == d * d);
    check(result.info().working_bits == 256);
    check(result.info().booked_work_score >= d * d * (d + 8));
    for (std::size_t j = 0; j < d; ++j) {
      check(arb_is_exact(acb_realref(vector[j])));
      check(arb_is_exact(acb_imagref(vector[j])));
    }
  }
}

void basis_and_scaling() {
  for (std::size_t d : {2U, 3U, 4U, 6U, 16U}) {
    Complex basis(d);
    acb_one(basis[0]);
    const auto result = bound_wh_overlap_deviations(basis.data(), d);
    fmpq_t expected;
    fmpq_init(expected);
    fmpq_set_ui(expected, d, d + 1);
    const bool contained = arb_contains_fmpq(result.maximum_squared_overlap_deviation(), expected);
    fmpq_clear(expected);
    check(contained);  // exact maximum is d/(d+1), decisively not small
    check(arb_is_positive(result.maximum_squared_overlap_deviation()));
    arb_set_si(acb_imagref(basis[0]), 2);
    arb_set_si(acb_realref(basis[0]), 3);
    const auto scaled = bound_wh_overlap_deviations(basis.data(), d);
    check(arb_overlaps(result.maximum_squared_overlap_deviation(),
                       scaled.maximum_squared_overlap_deviation()));
  }
}

void qutrit_controls() {
  Complex vector(3);
  acb_one(vector[1]);
  acb_set_si(vector[2], -1);
  Real threshold;
  arb_one(threshold.value);
  arb_mul_2exp_si(threshold.value, threshold.value, -180);
  auto first = bound_wh_overlap_deviations(vector.data(), 3);
  check(arb_contains_zero(first.maximum_squared_overlap_deviation()));
  check(arb_lt(first.maximum_squared_overlap_deviation(), threshold.value));
  check(arb_contains_si(first.squared_norm(), 2));
  auto again = bound_wh_overlap_deviations(vector.data(), 3);
  check(arb_equal(first.maximum_squared_overlap_deviation(),
                  again.maximum_squared_overlap_deviation()));
  auto moved = std::move(again);
  refuses([&] { again.info(); });
  check(moved.info().dimension == 3);
  bool categorized = false;
  try {
    (void)again.info();
  } catch (const sic::Error& error) {
    categorized = error.kind() == sic::ErrorKind::invalid_argument;
  }
  check(categorized);
  for (long exponent : {-1000000L, 1000000L}) {
    Complex scaled(3);
    for (unsigned j = 0; j < 3; ++j) acb_mul_2exp_si(scaled[j], vector[j], exponent);
    const auto result = bound_wh_overlap_deviations(scaled.data(), 3);
    check(arb_lt(result.maximum_squared_overlap_deviation(), threshold.value));
  }
  acb_set_si(vector[2], -2);
  const auto mutated = bound_wh_overlap_deviations(vector.data(), 3);
  check(arb_is_positive(mutated.maximum_squared_overlap_deviation()));
  check(!arb_overlaps(first.maximum_squared_overlap_deviation(),
                      mutated.maximum_squared_overlap_deviation()));

  acb_set_si(vector[2], -1);
  // A nonzero-radius box succeeds and uniformly contains a representative's
  // independent direct maximum, not just the original midpoint residual.
  for (unsigned j = 0; j < 3; ++j) {
    arb_add_error_2exp_si(acb_realref(vector[j]), -200);
    arb_add_error_2exp_si(acb_imagref(vector[j]), -200);
  }
  const auto box = bound_wh_overlap_deviations(vector.data(), 3);
  Complex representative(3);
  acb_one(representative[1]);
  acb_set_si(representative[2], -1);
  arb_one(acb_realref(representative[0]));
  arb_mul_2exp_si(acb_realref(representative[0]), acb_realref(representative[0]), -201);
  Real direct;
  direct_maximum(direct.value, representative);
  check(arb_contains(box.maximum_squared_overlap_deviation(), direct.value));

  // Broad but positive norm is not enough for the requested output accuracy.
  arb_add_error_2exp_si(acb_realref(vector[1]), -3);
  arb_add_error_2exp_si(acb_realref(vector[2]), -3);
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 3); });
}

void d2_radicals() {
  Complex vector(2);
  Real root, alpha, beta, threshold;
  arb_sqrt_ui(root.value, 3, 512);
  arb_add_ui(alpha.value, root.value, 3, 512);
  arb_div_ui(alpha.value, alpha.value, 6, 512);
  arb_neg(beta.value, root.value);
  arb_add_ui(beta.value, beta.value, 3, 512);
  arb_div_ui(beta.value, beta.value, 12, 512);
  arb_sqrt(acb_realref(vector[0]), alpha.value, 512);
  arb_sqrt(acb_realref(vector[1]), beta.value, 512);
  arb_set(acb_imagref(vector[1]), acb_realref(vector[1]));
  const auto result = bound_wh_overlap_deviations(vector.data(), 2);
  arb_one(threshold.value);
  arb_mul_2exp_si(threshold.value, threshold.value, -180);
  check(arb_contains_si(result.squared_norm(), 1));
  check(arb_contains_zero(result.maximum_squared_overlap_deviation()));
  check(arb_lt(result.maximum_squared_overlap_deviation(), threshold.value));
}

void default_d2048_basis_control() {
  constexpr std::size_t kD = 2048;
  Complex basis(kD);
  acb_one(basis[0]);
  const auto result = bound_wh_overlap_deviations(basis.data(), kD);
  check(arb_is_exact(result.squared_norm()));
  check(arb_contains_si(result.squared_norm(), 1));
  fmpq_t expected;
  fmpq_init(expected);
  fmpq_set_ui(expected, kD, kD + 1);
  const bool contained = arb_contains_fmpq(result.maximum_squared_overlap_deviation(), expected);
  fmpq_clear(expected);
  check(contained);
  check(result.info().checked_nonidentity_labels == 4194303);
  check(result.info().dft_rows == 2048);
  check(result.info().dft_points == 4194304);
  check(result.info().booked_work_score <= BallVerificationLimits{}.maximum_work_score);
  check(result.info().admitted_workspace_bytes <= BallVerificationLimits{}.maximum_workspace_bytes);
  std::cout << "default d2048 basis control: norm1, maximum 2048/2049, "
               "4194303 nonidentity labels, 2048 rows; not a SIC\n";
}

void refusals_and_wide_inputs() {
  Complex vector(3);
  acb_one(vector[1]);
  acb_set_si(vector[2], -1);
  const auto accepted = bound_wh_overlap_deviations(vector.data(), 3);
  const auto info = accepted.info();
  BallVerificationLimits limits;
  limits.maximum_workspace_bytes = info.admitted_workspace_bytes - 1;
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 3, limits); });
  limits = {};
  limits.maximum_work_score = info.booked_work_score - 1;
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 3, limits); });
  limits = {};
  limits.maximum_input_mantissa_bits = info.input_mantissa_bits - 1;
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 3, limits); });
  limits = {};
  limits.maximum_dimension = 2;
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 3, limits); });
  limits = {};
  limits.maximum_working_bits = 128;
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 3, limits); });
  limits = {};
  limits.working_bits = 63;
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 3, limits); });
  limits.working_bits = 16385;
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 3, limits); });
  limits = {};
  limits.minimum_absolute_accuracy_bits = 225;
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 3, limits); });
  limits.minimum_absolute_accuracy_bits = 0;
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 3, limits); });
  refuses([&] { bound_wh_overlap_deviations(nullptr, 3); });
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 1); });
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 2049); });
  flint_set_num_threads(2);
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 3); });
  flint_set_num_threads(1);
  Complex zero(3);
  refuses([&] { bound_wh_overlap_deviations(zero.data(), 3); });
  arb_add_error_2exp_si(acb_realref(zero[0]), 0);
  refuses([&] { bound_wh_overlap_deviations(zero.data(), 3); });
  acb_indeterminate(vector[0]);
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 3); });
  acb_one(vector[0]);
  fmpz_t exponent;
  fmpz_init(exponent);
  fmpz_one(exponent);
  fmpz_mul_2exp(exponent, exponent, 100);
  arf_mul_2exp_fmpz(arb_midref(acb_realref(vector[0])), arb_midref(acb_realref(vector[0])),
                    exponent);
  fmpz_clear(exponent);
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 3); });

  acb_zero(vector[0]);
  arb_const_pi(acb_realref(vector[0]), 1000000);
  const slong source_bits = acb_bits(vector[0]);
  check(source_bits > 900000);
  limits = {};
  limits.working_bits = 128;
  limits.minimum_absolute_accuracy_bits = 64;
  const auto rounded = bound_wh_overlap_deviations(vector.data(), 3, limits);
  check(acb_bits(vector[0]) == source_bits);
  check(rounded.info().input_mantissa_bits >= static_cast<std::uint64_t>(source_bits));
  Real direct;
  direct_maximum(direct.value, vector);
  check(arb_contains(rounded.maximum_squared_overlap_deviation(), direct.value));
  limits.maximum_input_mantissa_bits = 1000;
  refuses([&] { bound_wh_overlap_deviations(vector.data(), 3, limits); });
}
}  // namespace

int main() {
  int status = 0;
  try {
    flint_set_num_threads(1);
    small_direct_controls();
    basis_and_scaling();
    qutrit_controls();
    d2_radicals();
    default_d2048_basis_control();
    refusals_and_wide_inputs();
    std::cout << checks << " public ball-verification checks pass\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << " after " << checks << " checks\n";
    status = 1;
  }
  flint_cleanup();
  return status;
}
