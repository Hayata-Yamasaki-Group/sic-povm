// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/core/fft.hpp"

namespace sic::core {
Dft::Dft(std::size_t n) : n_(n) {
  if (n == 0) throw sic::Error(ErrorKind::invalid_argument, "Dft: length must be positive");
  roots_.resize(n);
  for (std::size_t m = 0; m < n; ++m) {
    const Real angle = 2 * std::numbers::pi_v<Real> * static_cast<Real>(m) / static_cast<Real>(n);
    roots_[m] = Complex(std::cos(angle), std::sin(angle));
  }
  if (std::has_single_bit(n)) {
    reversal_.resize(n);
    for (std::size_t j = 1; j < n; ++j)
      reversal_[j] = (reversal_[j / 2] >> 1) | ((j & 1) * (n / 2));
  }
}
}  // namespace sic::core

namespace sic::core {
void Dft::transform(Complex* data, int sign) const {
  check_sign(sign);
  if (fast()) {
    for (std::size_t j = 0; j < n_; ++j)
      if (j < reversal_[j]) std::swap(data[j], data[reversal_[j]]);
    butterflies(data, sign);
  } else {
    const Vector scratch(data, data + n_);
    direct(data, scratch, sign);
  }
}
}  // namespace sic::core

namespace sic::core {
void Dft::transform(Vector& data, int sign) const {
  if (data.size() != n_) throw sic::Error(ErrorKind::invalid_argument, "Dft: size mismatch");
  transform(data.data(), sign);
}
}  // namespace sic::core

namespace sic::core {
void Dft::check_sign(int sign) {
  if (sign != 1 && sign != -1)
    throw sic::Error(ErrorKind::invalid_argument, "Dft: sign must be +1 or -1");
}
}  // namespace sic::core

namespace sic::core {
void Dft::butterflies(Complex* data, int sign) const {
  for (std::size_t width = 2; width <= n_; width *= 2) {
    const std::size_t half = width / 2, stride = n_ / width;
    for (std::size_t begin = 0; begin < n_; begin += width) {
      for (std::size_t j = 0; j < half; ++j) {
        Complex w = roots_[j * stride];
        if (sign < 0) w = std::conj(w);
        const Complex u = data[begin + j];
        const Complex v = w * data[begin + j + half];
        data[begin + j] = u + v;
        data[begin + j + half] = u - v;
      }
    }
    if (width == n_) break;
  }
}
}  // namespace sic::core

namespace sic::core {
void Dft::direct(Complex* data, const Vector& scratch, int sign) const {
  for (std::size_t k = 0; k < n_; ++k) {
    Complex s = 0;
    std::size_t index = 0;
    for (std::size_t j = 0; j < n_; ++j) {
      s += scratch[j] * (sign < 0 ? std::conj(roots_[index]) : roots_[index]);
      index += k;
      if (index >= n_) index -= n_;
    }
    data[k] = s;
  }
}
}  // namespace sic::core
