// SPDX-License-Identifier: AGPL-3.0-or-later
// Reversible coordinates: x+y*omega is stored as (r,y), with
// r=x-floor(y*(sqrt(D0)-1)/2). The algebraic basis is unchanged.
#pragma once

#include <flint/fmpz.h>
#include <flint/ulong_extras.h>

#include <stdexcept>

namespace sic::support {

class LatticeCoordinates {
 public:
  explicit LatticeCoordinates(long discriminant);
  ~LatticeCoordinates();
  LatticeCoordinates(const LatticeCoordinates&) = delete;
  LatticeCoordinates& operator=(const LatticeCoordinates&) = delete;

  void shift(fmpz_t result, const fmpz_t y);
  void encode(fmpz_t x, const fmpz_t y);
  void decode(fmpz_t residual, const fmpz_t y);

 private:
  long discriminant_;
  fmpz_t square_, shift_;
};

}  // namespace sic::support
