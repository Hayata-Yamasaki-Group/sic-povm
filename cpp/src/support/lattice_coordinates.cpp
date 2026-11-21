// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/lattice_coordinates.hpp"

namespace sic::support {
LatticeCoordinates::LatticeCoordinates(long discriminant) : discriminant_(discriminant) {
  if (discriminant <= 1 || discriminant % 4 != 1 || n_is_square(discriminant))
    throw std::invalid_argument(
        "lattice coordinates require nonsquare field D0 congruent to 1 modulo 4");
  fmpz_init(square_);
  fmpz_init(shift_);
}
}  // namespace sic::support

namespace sic::support {
LatticeCoordinates::~LatticeCoordinates() {
  fmpz_clear(square_);
  fmpz_clear(shift_);
}
}  // namespace sic::support

namespace sic::support {
void LatticeCoordinates::shift(fmpz_t result, const fmpz_t y) {
  if (result == y) throw std::invalid_argument("lattice shift requires distinct output and input");
  if (fmpz_is_zero(y)) {
    fmpz_zero(result);
    return;
  }
  fmpz_mul(square_, y, y);
  fmpz_mul_ui(square_, square_, discriminant_);
  fmpz_sqrt(result, square_);
  if (fmpz_sgn(y) < 0) {
    fmpz_neg(result, result);
    fmpz_sub_ui(result, result, 1);
  }
  fmpz_sub(result, result, y);
  fmpz_fdiv_q_2exp(result, result, 1);
}
}  // namespace sic::support

namespace sic::support {
void LatticeCoordinates::encode(fmpz_t x, const fmpz_t y) {
  shift(shift_, y);
  fmpz_sub(x, x, shift_);
}
}  // namespace sic::support

namespace sic::support {
void LatticeCoordinates::decode(fmpz_t residual, const fmpz_t y) {
  shift(shift_, y);
  fmpz_add(residual, residual, shift_);
}
}  // namespace sic::support
