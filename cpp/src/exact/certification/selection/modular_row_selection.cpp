// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/selection/modular_row_selection.hpp"

namespace sic::exact {
ModularDyadicReduction::ModularDyadicReduction(ulong prime) {
  if (prime < 3 || !n_is_prime(prime))
    throw std::invalid_argument("dyadic reduction requires an odd prime");
  nmod_init(&mod_, prime);
  fmpz_init(mantissa_);
  fmpz_init(exponent_);
  for (auto& entry : powers_) entry.exponent = UWORD_MAX;
}

ulong ModularDyadicReduction::operator()(arf_srcptr value) {
  if (!arf_is_finite(value)) throw std::invalid_argument("nonfinite modular midpoint");
  if (arf_is_zero(value)) return 0;
  arf_get_fmpz_2exp(mantissa_, exponent_, value);
  const ulong exponent = fmpz_fdiv_ui(exponent_, mod_.n - 1);
  auto& cached = powers_[exponent % powers_.size()];
  if (cached.exponent != exponent) {
    ulong power = 1, base = 2, remaining = exponent;
    while (remaining) {
      if (remaining & 1) power = nmod_mul(power, base, mod_);
      remaining >>= 1;
      if (remaining) base = nmod_mul(base, base, mod_);
    }
    cached = {exponent, power};
  }
  return nmod_mul(fmpz_fdiv_ui(mantissa_, mod_.n), cached.value, mod_);
}

ModularRowSelection select_modular_rows(const acb_mat_t input, const std::vector<long>& labels,
                                        int workers, slong precision, ulong prime,
                                        bool opposite_root,
                                        sic::support::resources::Bytes workspace_limit) {
  const long rows = acb_mat_nrows(input), columns = acb_mat_ncols(input);
  if (rows <= 0 || columns <= 0 || labels.size() != (size_t)rows || precision <= 0)
    throw std::invalid_argument("invalid modular selector dimensions or precision");
  if (prime < 5 || prime % 4 != 1 || !n_is_prime(prime))
    throw std::invalid_argument("modular selector requires a checked prime equal to1mod4");
  nmod_t mod;
  nmod_init(&mod, prime);
  ulong imaginary = n_sqrtmod(prime - 1, prime);
  if (nmod_mul(imaginary, imaginary, mod) != prime - 1)
    throw std::logic_error("modular imaginary unit check failed");
  if (opposite_root) imaginary = nmod_neg(imaginary, mod);
  const auto required = modular_row_workspace(rows, columns, workers, precision);
  if (required > workspace_limit)
    throw sic::support::resources::AdmissionError("modular selector exceeds admitted workspace");
  sic::support::resources::Budget().require_additional("modular midpoint rows and worker scratch",
                                                       required);
  RowWorkerTeam team(workers);
  const auto cells = sic::support::resources::mul(rows, columns);
  if (cells > std::numeric_limits<size_t>::max() / sizeof(ulong))
    throw sic::support::resources::AdmissionError("modular matrix size is not representable");
  std::unique_ptr<ulong[]> values(new ulong[(size_t)cells]);
  std::vector<long> permutation(rows);
  std::iota(permutation.begin(), permutation.end(), 0);
  std::vector<unsigned char> nonfinite(rows, 0);
  const auto started = std::chrono::steady_clock::now();
  team.for_each(workers, [&](long task) {
    ModularDyadicReduction reduce(prime);
    const long first = rows / workers * task + (rows % workers) * task / workers;
    const long end = rows / workers * (task + 1) + (rows % workers) * (task + 1) / workers;
    for (long r = first; r < end; ++r)
      for (long c = 0; c < columns; ++c) {
        const auto entry = acb_mat_entry(input, r, c);
        auto& destination = values[(size_t)r * columns + c];
        if (!acb_is_finite(entry)) {
          nonfinite[r] = 1;
          destination = 0;
          continue;
        }
        for (auto component : {acb_realref(entry), acb_imagref(entry)}) {
          const auto midpoint = arb_midref(component);
          if (arf_bits(midpoint) > precision ||
              fmpz_bits(ARF_EXPREF(midpoint)) > (ulong)std::max<slong>(precision, FLINT_BITS))
            throw sic::support::resources::AdmissionError(
                "modular midpoint storage exceeds declared precision");
        }
        destination =
            nmod_add(reduce(arb_midref(acb_realref(entry))),
                     nmod_mul(imaginary, reduce(arb_midref(acb_imagref(entry))), mod), mod);
      }
  });
  ModularRowSelection result;
  result.prime = prime;
  result.imaginary_unit = imaginary;
  for (auto invalid : nonfinite)
    if (invalid) {
      result.finite = false;
      return result;
    }
  for (long col = 0; col < columns && result.rank < rows; ++col) {
    long pivot = result.rank;
    while (pivot < rows && !values[(size_t)permutation[pivot] * columns + col]) ++pivot;
    if (pivot == rows) continue;
    std::swap(permutation[result.rank], permutation[pivot]);
    const ulong* pivot_row = &values[(size_t)permutation[result.rank] * columns];
    const ulong inverse = nmod_inv(pivot_row[col], mod);
    const long rank = result.rank;
    team.for_each(rows - rank - 1, [&](long offset) {
      ulong* row = &values[(size_t)permutation[rank + 1 + offset] * columns];
      const ulong factor = nmod_neg(nmod_mul(row[col], inverse, mod), mod);
      _nmod_vec_scalar_addmul_nmod(row + col + 1, pivot_row + col + 1, columns - col - 1, factor,
                                   mod);
      row[col] = 0;
    });
    ++result.rank;
    if (sic::support::kernel_setting("SIC_PROGRESS") &&
        (result.rank % 256 == 0 || result.rank == columns))
      sic::support::progressf(
          "modular selector progress: pivots=%ld/%ld elapsed=%.2fs\n", result.rank, columns,
          std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
  }
  if (result.rank == columns) {
    result.rows.reserve(columns);
    for (long q = 0; q < columns; ++q) result.rows.push_back(labels[permutation[q]]);
  }
  return result;
}

}  // namespace sic::exact
