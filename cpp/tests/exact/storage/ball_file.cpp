// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/storage/ball_file.hpp"

#include <fstream>

#include "assertions.hpp"

int main() {
  sic::support::RealBallVector real(12);
  sic::support::ComplexBallVector complex(12);
  for (size_t q = 0; q < real.size(); ++q) {
    arb_set_si(real[q], (slong)q - 5);
    arb_div_ui(real[q], real[q], 7, 333);
    arb_mul_2exp_si(real[q], real[q], (slong)q * 2000 - 10000);
    if (q % 3) arb_add_error_2exp_si(real[q], (slong)q * 2000 - 10300);
    acb_set_arb(complex[q], real[q]);
    arb_set(acb_imagref(complex[q]), real[real.size() - q - 1]);
  }
  arb_zero(real[0]);
  const auto real_file = sic::exact::BallFile::save(real);
  auto restored = real_file->real();
  SIC_CHECK(real_file->count() == real.size());
  for (size_t q = 0; q < real.size(); ++q) SIC_CHECK(arb_equal(real[q], restored[q]));
  const auto complex_file = sic::exact::BallFile::save(complex);
  auto restored_complex = complex_file->complex();
  for (size_t q = 0; q < complex.size(); ++q) SIC_CHECK(acb_equal(complex[q], restored_complex[q]));
  bool rejected = false;
  try {
    complex_file->real();
  } catch (const std::logic_error&) {
    rejected = true;
  }
  SIC_CHECK(rejected);
  {
    std::fstream corrupt(real_file->path(), std::ios::in | std::ios::out | std::ios::binary);
    corrupt.seekp(0);
    corrupt.put('X');
  }
  rejected = false;
  try {
    real_file->real();
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  SIC_CHECK(rejected);
  arb_indeterminate(real[0]);
  rejected = false;
  try {
    sic::exact::BallFile::save(real);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  SIC_CHECK(rejected);
  return sic_test::report();
}
