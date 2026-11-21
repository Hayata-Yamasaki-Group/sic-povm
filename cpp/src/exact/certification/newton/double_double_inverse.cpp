// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/newton/double_double_inverse.hpp"

#include "exact/arithmetic/double_double.hpp"
#include "sic/support/flint_raii.hpp"
#include "support/resources.hpp"

namespace sic::exact {
namespace {
struct Complex {
  DoubleDouble re{}, im{};
  Complex(DoubleDouble a = 0, DoubleDouble b = 0) : re(a), im(b) {}
  friend Complex operator-(Complex a, Complex b) { return {a.re - b.re, a.im - b.im}; }
  friend Complex operator*(Complex a, Complex b) {
    return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
  }
  friend Complex operator/(Complex a, Complex b) {
    const DoubleDouble norm = b.re * b.re + b.im * b.im;
    return {(a.re * b.re + a.im * b.im) / norm, (a.im * b.re - a.re * b.im) / norm};
  }
};
bool finite(Complex x) {
  return std::isfinite(x.re.hi) && std::isfinite(x.re.lo) && std::isfinite(x.im.hi) &&
         std::isfinite(x.im.lo);
}
double weight(Complex x) { return std::max(std::abs(double(x.re)), std::abs(double(x.im))); }
DoubleDouble from_arf(arf_srcptr x, slong shift) {
  sic::support::flint::Arf t, h;
  arf_mul_2exp_si(t, x, shift);
  const double hi = arf_get_d(t, ARF_RND_NEAR);
  // Equilibration keeps input components <=1. The subtraction recovers the
  // next 53 bits without first rounding the source to binary64.
  arf_set_d(h, hi);
  arf_sub(t, t, h, 128, ARF_RND_NEAR);
  return {hi, arf_get_d(t, ARF_RND_NEAR)};
}
void to_arf(arf_ptr x, DoubleDouble a, slong shift) {
  sic::support::flint::Arf lo;
  arf_set_d(x, a.hi);
  arf_set_d(lo, a.lo);
  arf_add(x, x, lo, 128, ARF_RND_NEAR);
  arf_mul_2exp_si(x, x, shift);
}
}  // namespace

bool double_double_inverse(acb_mat_t output, const acb_mat_t input, RowWorkerTeam& team) {
  const long n = acb_mat_nrows(input);
  constexpr slong kUnset = -1000000;
  std::vector<slong> row(n, kUnset), col(n, kUnset), permutation(n);
  for (long i = 0; i < n; ++i) {
    sic::support::check_stop();
    for (long j = 0; j < n; ++j)
      for (auto part :
           {acb_realref(acb_mat_entry(input, i, j)), acb_imagref(acb_mat_entry(input, i, j))}) {
        auto x = arb_midref(part);
        if (!arf_is_zero(x)) {
          if (!fmpz_fits_si(ARF_EXPREF(x))) return false;
          const slong exponent = fmpz_get_si(ARF_EXPREF(x));
          // Refuse extreme ranges before any exponent subtraction; Arb remains
          // the range-safe fallback, including for underflowed native pivots.
          if (exponent < -100000 || exponent > 100000) return false;
          row[i] = std::max(row[i], exponent);
        }
      }
  }
  for (long i = 0; i < n; ++i)
    if (row[i] == kUnset) return false;
  for (long i = 0; i < n; ++i)
    for (long j = 0; j < n; ++j)
      for (auto part :
           {acb_realref(acb_mat_entry(input, i, j)), acb_imagref(acb_mat_entry(input, i, j))}) {
        auto x = arb_midref(part);
        if (!arf_is_zero(x)) col[j] = std::max(col[j], fmpz_get_si(ARF_EXPREF(x)) - row[i]);
      }
  for (long j = 0; j < n; ++j)
    if (col[j] == kUnset) return false;
  const auto count = sic::support::resources::mul(n, n);
  std::vector<Complex> lu(count), inverse(count);
  for (long i = 0; i < n; ++i) {
    sic::support::check_stop();
    permutation[i] = i;
    for (long j = 0; j < n; ++j) {
      auto value = acb_mat_entry(input, i, j);
      lu[i * n + j] = {from_arf(arb_midref(acb_realref(value)), -row[i] - col[j]),
                       from_arf(arb_midref(acb_imagref(value)), -row[i] - col[j])};
      if (!finite(lu[i * n + j])) return false;
    }
  }
  // B = Dr^-1 J Dc^-1. Panels and trailing tiles always have width 32;
  // every entry visits k in increasing order, independent of the worker count.
  // Pivot ties choose the first row. No threaded BLAS/reduction is involved.
  constexpr long kBlock = 32;
  for (long first = 0; first < n; first += kBlock) {
    sic::support::check_stop();
    const long end = std::min(n, first + kBlock);
    for (long k = first; k < end; ++k) {
      sic::support::check_stop();
      long pivot = k;
      for (long i = k + 1; i < n; ++i)
        if (weight(lu[i * n + k]) > weight(lu[pivot * n + k])) pivot = i;
      if (!(weight(lu[pivot * n + k]) > 0) || !finite(lu[pivot * n + k])) return false;
      if (pivot != k) {
        for (long j = 0; j < n; ++j) std::swap(lu[k * n + j], lu[pivot * n + j]);
        std::swap(permutation[k], permutation[pivot]);
      }
      for (long i = k + 1; i < n; ++i) {
        lu[i * n + k] = lu[i * n + k] / lu[k * n + k];
        for (long j = k + 1; j < end; ++j)
          lu[i * n + j] = lu[i * n + j] - lu[i * n + k] * lu[k * n + j];
      }
    }
    for (long i = first; i < end; ++i)
      for (long k = first; k < i; ++k)
        for (long j = end; j < n; ++j)
          lu[i * n + j] = lu[i * n + j] - lu[i * n + k] * lu[k * n + j];
    team.for_each(n - end, [&](long index) {
      sic::support::check_stop();
      const long i = end + index;
      for (long j0 = end; j0 < n; j0 += kBlock)
        for (long k = first; k < end; ++k)
          for (long j = j0; j < std::min(n, j0 + kBlock); ++j)
            lu[i * n + j] = lu[i * n + j] - lu[i * n + k] * lu[k * n + j];
    });
  }
  team.for_each((n + 7) / 8, [&](long task) {
    const long first = task * 8, end = std::min(n, first + 8);
    for (long i = 0; i < n; ++i) {
      sic::support::check_stop();
      for (long c = first; c < end; ++c) inverse[i * n + c] = Complex(permutation[i] == c ? 1 : 0);
      for (long k = 0; k < i; ++k)
        for (long c = first; c < end; ++c)
          inverse[i * n + c] = inverse[i * n + c] - lu[i * n + k] * inverse[k * n + c];
    }
    for (long i = n - 1; i >= 0; --i) {
      sic::support::check_stop();
      for (long k = i + 1; k < n; ++k)
        for (long c = first; c < end; ++c)
          inverse[i * n + c] = inverse[i * n + c] - lu[i * n + k] * inverse[k * n + c];
      for (long c = first; c < end; ++c) inverse[i * n + c] = inverse[i * n + c] / lu[i * n + i];
    }
  });
  for (const auto& value : inverse)
    if (!finite(value)) return false;
  // J^-1 = Dc^-1 B^-1 Dr^-1. Only publish after every native solve is finite;
  // the caller's guard clears even partially converted output on cancellation.
  for (long i = 0; i < n; ++i) {
    sic::support::check_stop();
    for (long j = 0; j < n; ++j) {
      auto value = acb_mat_entry(output, i, j);
      acb_zero(value);
      to_arf(arb_midref(acb_realref(value)), inverse[i * n + j].re, -col[i] - row[j]);
      to_arf(arb_midref(acb_imagref(value)), inverse[i * n + j].im, -col[i] - row[j]);
    }
  }
  sic::support::check_stop();
  return true;
}
}  // namespace sic::exact
