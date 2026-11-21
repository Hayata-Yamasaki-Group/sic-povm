// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Unscaled binary64 DFT: out[k] = sum_j in[j] exp(sign * 2 pi i j k / n).
// Radix two for powers of two; direct sum otherwise. Immutable plans, local scratch.
#include <bit>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "sic/core/types.hpp"
#include "sic/error.hpp"

namespace sic::core {
class Dft {
 public:
  // Zero length, vector size mismatch and invalid sign throw sic::Error(invalid_argument).
  explicit Dft(std::size_t n);
  std::size_t size() const { return n_; }
  bool fast() const { return !reversal_.empty(); }
  Complex root(long long m) const {
    const auto n = static_cast<long long>(n_);
    const auto r = m % n;
    return roots_[static_cast<std::size_t>(r < 0 ? r + n : r)];
  }
  void transform(Complex* data, int sign) const;
  void transform(Vector& data, int sign) const;
  // Fuses product generation and bit permutation for streaming overlap rows.
  // The generator must not read from data (the output is written in permuted order).
  template <class Generator>
  void transform_generated(Complex* data, int sign, Generator generate) const {
    check_sign(sign);
    if (fast()) {
      for (std::size_t j = 0; j < n_; ++j) data[reversal_[j]] = generate(j);
      butterflies(data, sign);
    } else {
      Vector scratch(n_);
      for (std::size_t j = 0; j < n_; ++j) scratch[j] = generate(j);
      direct(data, scratch, sign);
    }
  }

 private:
  std::size_t n_;
  std::vector<Complex> roots_;
  std::vector<std::size_t> reversal_;
  static void check_sign(int sign);
  void butterflies(Complex* data, int sign) const;
  void direct(Complex* data, const Vector& scratch, int sign) const;
};
}  // namespace sic::core
