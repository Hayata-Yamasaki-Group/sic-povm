// SPDX-License-Identifier: AGPL-3.0-or-later
// Empirical subsystem selection from exact Gaussian-dyadic midpoints.
// Modular rank is not a rank or conditioning certificate for the analytic system.
#pragma once
#include <flint/acb_mat.h>
#include <flint/nmod_vec.h>
#include <flint/ulong_extras.h>

#include <array>
#include <chrono>
#include <memory>
#include <numeric>

#include "exact/runtime/row_worker_team.hpp"
#include "sic/support/kernel_settings.hpp"
#include "support/resources.hpp"

namespace sic::exact {

class ModularDyadicReduction {
 public:
  explicit ModularDyadicReduction(ulong prime);
  ~ModularDyadicReduction() {
    fmpz_clear(mantissa_);
    fmpz_clear(exponent_);
  }
  ModularDyadicReduction(const ModularDyadicReduction&) = delete;
  ModularDyadicReduction& operator=(const ModularDyadicReduction&) = delete;
  ulong operator()(arf_srcptr value);

 private:
  nmod_t mod_;
  fmpz_t mantissa_, exponent_;
  // Direct mapping has a fixed byte bound; exponent keys are residues, not fmpz.
  struct CachedPower {
    ulong exponent;
    ulong value;
  };
  std::array<CachedPower, 4096> powers_;
};

inline sic::support::resources::Bytes modular_row_workspace(
    sic::support::resources::Count rows, sic::support::resources::Count columns, int workers,
    sic::support::resources::BitCount precision) {
  return sic::support::resources::modular_selection_bytes(rows, columns, workers, precision);
}

struct ModularRowSelection {
  std::vector<long> rows;
  long rank = 0;
  ulong prime = 0, imaginary_unit = 0;
  bool finite = true;
};

ModularRowSelection select_modular_rows(
    const acb_mat_t input, const std::vector<long>& labels, int workers, slong precision,
    ulong prime = UWORD(2013265921), bool opposite_root = false,
    sic::support::resources::Bytes workspace_limit =
        std::numeric_limits<sic::support::resources::Bytes>::max());

}  // namespace sic::exact
