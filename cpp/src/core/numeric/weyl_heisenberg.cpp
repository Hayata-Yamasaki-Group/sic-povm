// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/core/weyl_heisenberg.hpp"

#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace sic::core {

WeylHeisenberg::WeylHeisenberg(int d) : d_(d), dft_(static_cast<std::size_t>(d > 0 ? d : 1)) {
  if (d < 1) throw sic::Error(ErrorKind::invalid_argument, "WeylHeisenberg: d must be >= 1");
  roots_2d_.resize(2 * static_cast<std::size_t>(d));
  for (std::size_t m = 0; m < roots_2d_.size(); ++m) {
    const Real angle = std::numbers::pi_v<Real> * m / d;
    roots_2d_[m] = Complex(std::cos(angle), std::sin(angle));
  }
}

Complex WeylHeisenberg::root_2d(std::int64_t m) const {
  const std::int64_t n = 2 * static_cast<std::int64_t>(d_);
  std::int64_t r = m % n;
  if (r < 0) r += n;
  return roots_2d_[static_cast<std::size_t>(r)];
}

std::int64_t WeylHeisenberg::mod_d(std::int64_t a) const {
  std::int64_t r = a % d_;
  return r < 0 ? r + d_ : r;
}

// tau = -exp(pi i/d) = exp(pi i (d+1)/d)
Complex WeylHeisenberg::tau_pow(std::int64_t k) const {
  // Reduce k mod 2d first so that k*(d+1) cannot overflow.
  const std::int64_t n = 2 * static_cast<std::int64_t>(d_);
  std::int64_t kr = k % n;
  if (kr < 0) kr += n;
  return root_2d(kr * (static_cast<std::int64_t>(d_) + 1));
}

Complex WeylHeisenberg::omega_pow(std::int64_t k) const { return root_2d(2 * mod_d(k)); }

Vector WeylHeisenberg::apply(std::int64_t p1, std::int64_t p2, const Vector& v) const {
  if (static_cast<int>(v.size()) != d_)
    throw sic::Error(ErrorKind::invalid_argument, "apply: size mismatch");
  // Keep the established binary64 multiplication order for this allocating API.
  const auto period = 2 * static_cast<std::uint64_t>(d_);
  auto reduce = [period](std::int64_t x) {
    const auto r = x % static_cast<std::int64_t>(period);
    return static_cast<std::uint64_t>(r < 0 ? r + static_cast<std::int64_t>(period) : r);
  };
  const Complex phase = tau_pow(static_cast<std::int64_t>(reduce(p1) * reduce(p2) % period));
  const auto a = mod_d(p1), b = mod_d(p2);
  Vector out(v.size());
  for (std::int64_t k = 0; k < d_; ++k) {
    const std::int64_t j = mod_d(k - a);
    out[k] = phase * omega_pow(b * j) * v[j];
  }
  return out;
}

void WeylHeisenberg::displace(std::int64_t p1, std::int64_t p2, const Complex* v,
                              Complex* out) const {
  const auto n = static_cast<std::size_t>(d_), n2 = 2 * n;
  auto reduce = [n2](std::int64_t x) {
    auto r = x % static_cast<std::int64_t>(n2);
    return static_cast<std::size_t>(r < 0 ? r + n2 : r);
  };
  const auto a = static_cast<std::size_t>(mod_d(p1));
  const auto step = 2 * static_cast<std::size_t>(mod_d(p2));
  std::size_t phase = (n2 - ((reduce(p1) * reduce(p2) % n2) * (n + 1) % n2)) % n2;
  std::size_t src = (n - a) % n;
  for (std::size_t k = 0; k < n; ++k) {
    out[k] = roots_2d_[phase] * v[src];
    phase += step;
    if (phase >= n2) phase -= n2;
    if (++src == n) src = 0;
  }
}

Complex WeylHeisenberg::matrix_element(const Vector& u, std::int64_t p1, std::int64_t p2,
                                       const Vector& v) const {
  if (u.size() != static_cast<std::size_t>(d_))
    throw sic::Error(ErrorKind::invalid_argument, "matrix_element: size mismatch");
  const Vector w = apply(p1, p2, v);
  Complex s = 0;
  for (int k = 0; k < d_; ++k) s += std::conj(u[k]) * w[k];
  return s;
}

void WeylHeisenberg::overlap_row_impl(std::int64_t p1, const Vector& psi, Complex* out) const {
  // <psi|D_p|psi> = tau^(p1 p2) sum_j conj(psi_(j+p1)) psi_j omega^(p2 j)
  const auto n = static_cast<std::size_t>(d_);
  const auto shift = static_cast<std::size_t>(mod_d(p1));
  auto product = [&](std::size_t j) {
    auto shifted = j + shift;
    if (shifted >= n) shifted -= n;
    return std::conj(psi[shifted]) * psi[j];
  };
  dft_.transform_generated(out, +1, product);
  apply_tau_ramp(p1, out);
}

void WeylHeisenberg::apply_tau_ramp(std::int64_t p1, Complex* out) const {
  const auto n = static_cast<std::size_t>(d_);
  // Exact modular table indices avoid both p1*p2 overflow for arbitrary p1
  // and accumulated phase error from repeated floating-point multiplication.
  const auto period = static_cast<std::int64_t>(roots_2d_.size());
  auto reduced = p1 % period;
  if (reduced < 0) reduced += period;
  const auto increment =
      static_cast<std::size_t>((reduced * (static_cast<std::int64_t>(d_) + 1)) % period);
  std::size_t phase = 0;
  for (std::size_t p2 = 0; p2 < n; ++p2) {
    out[p2] *= roots_2d_[phase];
    phase += increment;
    if (phase >= roots_2d_.size()) phase -= roots_2d_.size();
  }
}

void WeylHeisenberg::overlap_row(std::int64_t p1, const Vector& psi, Vector& out) const {
  if (psi.size() != static_cast<std::size_t>(d_))
    throw sic::Error(ErrorKind::invalid_argument, "overlap_row: size mismatch");
  if (&psi == &out)
    throw sic::Error(ErrorKind::invalid_argument, "overlap_row: input/output alias");
  out.resize(psi.size());
  overlap_row_impl(p1, psi, out.data());
}

void WeylHeisenberg::cross_overlap_row(std::int64_t p1, const Vector& psi, const Vector& phi,
                                       Vector& out) const {
  const auto n = static_cast<std::size_t>(d_);
  if (psi.size() != n || phi.size() != n)
    throw sic::Error(ErrorKind::invalid_argument, "cross_overlap_row: size mismatch");
  if (&psi == &out || &phi == &out)
    throw sic::Error(ErrorKind::invalid_argument, "cross_overlap_row: input/output alias");
  out.resize(n);
  const auto shift = static_cast<std::size_t>(mod_d(p1));
  dft_.transform_generated(out.data(), -1, [&](std::size_t k) {
    auto src = k + n - shift;
    if (src >= n) src -= n;
    return std::conj(psi[src]) * phi[k];
  });
  apply_tau_ramp(p1, out.data());
}

std::vector<Complex> WeylHeisenberg::overlaps(const Vector& psi) const {
  const auto n = static_cast<std::size_t>(d_);
  if (psi.size() != n) throw sic::Error(ErrorKind::invalid_argument, "overlaps: size mismatch");
  if (n > Vector().max_size() / n) throw std::length_error("overlaps: output size overflow");
  std::vector<Complex> out(n * n);
  for (std::size_t p1 = 0; p1 < n; ++p1)
    overlap_row_impl(static_cast<std::int64_t>(p1), psi, out.data() + p1 * n);
  return out;
}

}  // namespace sic::core
