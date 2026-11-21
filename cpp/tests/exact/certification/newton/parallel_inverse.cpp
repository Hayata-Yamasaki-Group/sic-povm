// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/newton/parallel_inverse.hpp"

#include <unistd.h>

#include "assertions.hpp"
#include "sic/support/flint_raii.hpp"

namespace {
using Matrix = sic::support::flint::AcbMat;

struct Environment {
  std::string name, saved;
  bool existed;
  explicit Environment(const char* key) : name(key), existed(std::getenv(key) != nullptr) {
    if (existed) saved = std::getenv(key);
  }
  ~Environment() {
    if (existed)
      setenv(name.c_str(), saved.c_str(), 1);
    else
      unsetenv(name.c_str());
  }
};

bool residual_small(const acb_mat_t inverse, const acb_mat_t input, slong bits = 100) {
  Matrix residual(acb_mat_nrows(input), acb_mat_nrows(input));
  acb_mat_mul(residual.get(), inverse, input, 256);
  mag_t row, entry, maximum;
  mag_init(row);
  mag_init(entry);
  mag_init(maximum);
  for (long r = 0; r < acb_mat_nrows(input); ++r) {
    mag_zero(row);
    for (long c = 0; c < acb_mat_ncols(input); ++c) {
      auto value = acb_mat_entry(residual.get(), r, c);
      if (r == c) acb_sub_ui(value, value, 1, 256);
      acb_get_mag(entry, value);
      mag_add(row, row, entry);
    }
    mag_max(maximum, maximum, row);
  }
  const bool result = mag_is_finite(maximum) && mag_cmp_2exp_si(maximum, -bits) < 0;
  mag_clear(row);
  mag_clear(entry);
  mag_clear(maximum);
  return result;
}

void matrices() {
  for (long n : {3L, 9L, 31L, 65L}) {
    Matrix input(n, n), source(n, n), serial(n, n), parallel(n, n), library(n, n);
    for (long r = 0; r < n; ++r)
      for (long c = 0; c < n; ++c) {
        // Cyclically permute a diagonally dominant matrix, forcing pivots.
        const long q = (r + 1) % n;
        auto value = acb_mat_entry(input.get(), r, c);
        acb_set_si(value, (q * 17 + c * 11) % 19 - 9);
        arb_set_si(acb_imagref(value), (q * 7 + c * 13) % 17 - 8);
        if (q == c) acb_add_si(value, value, 64 * n, 192);
        acb_mul_2exp_si(value, value, q % 7 - 3 + c % 9 - 4);
      }
    acb_mat_set(source.get(), input.get());
    const int previous = flint_get_num_threads();
    SIC_CHECK(sic::exact::parallel_point_inverse(serial.get(), input.get(), 192, 1, UINT64_MAX));
    SIC_CHECK(sic::exact::parallel_point_inverse(parallel.get(), input.get(), 192, 4, UINT64_MAX));
    SIC_CHECK(flint_get_num_threads() == previous);
    SIC_CHECK(acb_mat_equal(serial.get(), parallel.get()));
    SIC_CHECK(acb_mat_equal(source.get(), input.get()));
    SIC_CHECK(residual_small(serial.get(), input.get()));
    SIC_CHECK(residual_small(parallel.get(), input.get()));
    SIC_CHECK(acb_mat_approx_inv(library.get(), input.get(), 192));
    SIC_CHECK(residual_small(library.get(), input.get()));
    const sic::exact::InverseTeamFactory refuse =
        [](int) -> std::unique_ptr<sic::exact::RowWorkerTeam> {
      throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
    };
    SIC_CHECK(sic::exact::parallel_point_inverse(parallel.get(), input.get(), 192, 4, UINT64_MAX,
                                                 false, refuse));
    SIC_CHECK(residual_small(parallel.get(), input.get()));
    SIC_CHECK(acb_mat_equal(serial.get(), parallel.get()));
  }
}

void short_matrices() {
  using sic::exact::PointInverseArithmetic;
  for (long n : {3L, 9L, 31L, 32L, 33L, 65L, 129L, 254L}) {
    Matrix input(n, n), source(n, n), serial(n, n), parallel(n, n);
    for (long r = 0; r < n; ++r)
      for (long c = 0; c < n; ++c) {
        const long q = (r + 1) % n;
        auto value = acb_mat_entry(input.get(), r, c);
        acb_set_si(value, (q * 17 + c * 11) % 19 - 9);
        arb_set_si(acb_imagref(value), (q * 7 + c * 13) % 17 - 8);
        if (q == c) acb_add_si(value, value, 64 * n, 192);
        // Native input/output conversion must undo exponents outside binary64.
        acb_mul_2exp_si(value, value, 1000 * (q % 7 - 3) + 500 * (c % 9 - 4));
      }
    acb_mat_set(source.get(), input.get());
    auto run = [&](int workers, sic::support::resources::Bytes limit,
                   const sic::exact::InverseTeamFactory& factory = {}) {
      SIC_CHECK(sic::exact::parallel_point_inverse(parallel.get(), input.get(), 192, workers, limit,
                                                   false, factory,
                                                   PointInverseArithmetic::double_double));
      SIC_CHECK(acb_mat_is_exact(parallel.get()));
      SIC_CHECK(acb_mat_equal(source.get(), input.get()));
    };
    run(1, UINT64_MAX);
    acb_mat_set(serial.get(), parallel.get());
    for (int workers : {2, 8}) {
      run(workers, UINT64_MAX);
      SIC_CHECK(acb_mat_equal(serial.get(), parallel.get()));
    }
    const auto equivalent = sic::support::resources::mul(
        sic::support::resources::mul(n, n),
        sic::support::resources::complex_bytes(sic::support::resources::newton_matrix_bits(192)));
    const auto serial_limit =
        sic::support::resources::add(sic::support::resources::mul(8, equivalent),
                                     sic::support::resources::inverse_column_scratch(n, 1, 192));
    run(8, serial_limit);
    SIC_CHECK(acb_mat_equal(serial.get(), parallel.get()));
    const sic::exact::InverseTeamFactory refuse =
        [](int) -> std::unique_ptr<sic::exact::RowWorkerTeam> {
      throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
    };
    run(8, UINT64_MAX, refuse);
    SIC_CHECK(acb_mat_equal(serial.get(), parallel.get()));
    run(8, UINT64_MAX, [](int) -> std::unique_ptr<sic::exact::RowWorkerTeam> {
      throw sic::support::resources::AdmissionError("injected stack denial");
    });
    SIC_CHECK(acb_mat_equal(serial.get(), parallel.get()));
    // Undo row/column scaling before checking the rigorous norm. The original
    // scaled norm can be huge despite excellent componentwise contraction.
    for (long r = 0; r < n; ++r)
      for (long c = 0; c < n; ++c) {
        const long q = (r + 1) % n;
        acb_mul_2exp_si(acb_mat_entry(input.get(), r, c), acb_mat_entry(input.get(), r, c),
                        -1000 * (q % 7 - 3) - 500 * (c % 9 - 4));
        acb_mul_2exp_si(acb_mat_entry(serial.get(), c, r), acb_mat_entry(serial.get(), c, r),
                        1000 * (q % 7 - 3) + 500 * (c % 9 - 4));
      }
    SIC_CHECK(residual_small(serial.get(), input.get(), 70));
  }
  // Filling both DD components makes the last perturbation disappear natively.
  // The original Arb path must recover this nonsingular matrix automatically.
  Matrix input(2, 2), expected(2, 2), actual(2, 2);
  sic::support::flint::Acb small;
  acb_one(small);
  acb_mul_2exp_si(small, small, -60);
  for (long r = 0; r < 2; ++r)
    for (long c = 0; c < 2; ++c) {
      acb_one(acb_mat_entry(input.get(), r, c));
      acb_add(acb_mat_entry(input.get(), r, c), acb_mat_entry(input.get(), r, c), small, 192);
    }
  acb_mul_2exp_si(small, small, -60);
  acb_add(acb_mat_entry(input.get(), 1, 1), acb_mat_entry(input.get(), 1, 1), small, 192);
  SIC_CHECK(sic::exact::parallel_point_inverse(expected.get(), input.get(), 192, 1, UINT64_MAX));
  SIC_CHECK(sic::exact::parallel_point_inverse(actual.get(), input.get(), 192, 8, UINT64_MAX, false,
                                               {}, PointInverseArithmetic::double_double));
  SIC_CHECK(acb_mat_equal(expected.get(), actual.get()));
  SIC_CHECK(residual_small(actual.get(), input.get(), 50));
  // A range outside the native policy still uses the original exponent-safe
  // inverse. This is a proposal refusal, not a certificate refusal.
  acb_mat_one(input.get());
  acb_mul_2exp_si(acb_mat_entry(input.get(), 0, 0), acb_mat_entry(input.get(), 0, 0), 100001);
  SIC_CHECK(sic::exact::parallel_point_inverse(expected.get(), input.get(), 192, 1, UINT64_MAX));
  SIC_CHECK(sic::exact::parallel_point_inverse(actual.get(), input.get(), 192, 8, UINT64_MAX, false,
                                               {}, PointInverseArithmetic::double_double));
  SIC_CHECK(acb_mat_equal(expected.get(), actual.get()));
  acb_mat_zero(input.get());
  SIC_CHECK(!sic::exact::parallel_point_inverse(actual.get(), input.get(), 192, 8, UINT64_MAX,
                                                false, {}, PointInverseArithmetic::double_double));
  SIC_CHECK(acb_mat_is_zero(actual.get()));
  acb_mat_one(input.get());
  char stop_path[] = "/tmp/sic-native-inverse-stop-XXXXXX";
  int descriptor = mkstemp(stop_path);
  SIC_CHECK(descriptor >= 0);
  if (descriptor >= 0) {
    close(descriptor);
    unlink(stop_path);
    sic::support::ScopedStopFile stop_scope(stop_path);
    const sic::exact::InverseTeamFactory stop_after_admission = [&](int workers) {
      auto team = std::make_unique<sic::exact::RowWorkerTeam>(workers);
      FILE* file = fopen(stop_path, "w");
      SIC_CHECK(file != nullptr);
      if (file) fclose(file);
      return team;
    };
    bool stopped = false;
    try {
      sic::exact::parallel_point_inverse(actual.get(), input.get(), 192, 8, UINT64_MAX, false,
                                         stop_after_admission,
                                         PointInverseArithmetic::double_double);
    } catch (const std::runtime_error& error) {
      stopped = std::string(error.what()).find("requested stop") != std::string::npos;
    }
    SIC_CHECK(stopped);
    SIC_CHECK(acb_mat_is_zero(actual.get()));
    unlink(stop_path);
  }
}

void failures() {
  Matrix input(9, 9), output(9, 9), identity(9, 9);
  acb_mat_one(identity.get());
  acb_mat_one(output.get());
  SIC_CHECK(!sic::exact::parallel_point_inverse(output.get(), input.get(), 192, 4, UINT64_MAX));
  SIC_CHECK(acb_mat_is_zero(output.get()));
  acb_mat_one(input.get());
  bool refused = false;
  try {
    sic::exact::parallel_point_inverse(output.get(), input.get(), 192, 4, 1);
  } catch (const sic::support::resources::AdmissionError&) {
    refused = true;
  }
  SIC_CHECK(refused);
  SIC_CHECK(acb_mat_is_zero(output.get()));
  refused = false;
  try {
    sic::exact::parallel_point_inverse(input.get(), input.get(), 192, 4, UINT64_MAX);
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  SIC_CHECK(refused);
  SIC_CHECK(acb_mat_equal(input.get(), identity.get()));
  refused = false;
  try {
    sic::exact::parallel_point_inverse(output.get(), input.get(), 192, 0, UINT64_MAX);
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  SIC_CHECK(refused);
  acb_indeterminate(acb_mat_entry(input.get(), 0, 0));
  SIC_CHECK(!sic::exact::parallel_point_inverse(output.get(), input.get(), 192, 4, UINT64_MAX));
  SIC_CHECK(acb_mat_is_zero(output.get()));
  acb_mat_one(input.get());
  arb_const_pi(acb_realref(acb_mat_entry(input.get(), 0, 0)), 1024);
  refused = false;
  try {
    sic::exact::parallel_point_inverse(output.get(), input.get(), 64, 4, UINT64_MAX);
  } catch (const sic::support::resources::AdmissionError&) {
    refused = true;
  }
  SIC_CHECK(refused);
  acb_mat_one(input.get());
  const sic::exact::InverseTeamFactory arithmetic_failure =
      [](int) -> std::unique_ptr<sic::exact::RowWorkerTeam> {
    throw std::runtime_error("injected non-startup failure");
  };
  refused = false;
  try {
    sic::exact::parallel_point_inverse(output.get(), input.get(), 192, 4, UINT64_MAX, false,
                                       arithmetic_failure);
  } catch (const std::runtime_error& error) {
    refused = std::string(error.what()).find("injected") != std::string::npos;
  }
  SIC_CHECK(refused);
  SIC_CHECK(acb_mat_is_zero(output.get()));
  char stop_path[] = "/tmp/sic-inverse-stop-XXXXXX";
  const int descriptor = mkstemp(stop_path);
  SIC_CHECK(descriptor >= 0);
  if (descriptor >= 0) {
    close(descriptor);
    sic::support::ScopedStopFile stop_scope(stop_path);
    refused = false;
    try {
      sic::exact::parallel_point_inverse(output.get(), input.get(), 192, 4, UINT64_MAX);
    } catch (const std::runtime_error& error) {
      refused = std::string(error.what()).find("requested stop") != std::string::npos;
    }
    SIC_CHECK(refused);
    SIC_CHECK(acb_mat_is_zero(output.get()));
    unlink(stop_path);
  }
}
}  // namespace

int main() {
  matrices();
  short_matrices();
  failures();
  return sic_test::report();
}
