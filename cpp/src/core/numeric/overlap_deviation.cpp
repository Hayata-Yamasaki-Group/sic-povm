// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/numeric/overlap_deviation.hpp"

#include "sic/core/fft.hpp"
namespace sic::core {
double overlap_deviation(const std::vector<Complex>& w) {
  const size_t d = w.size();
  double worst = 0;
  std::vector<Complex> u(d);
  const sic::core::Dft dft(d);
  for (size_t a = 0; a < d; ++a) {
    for (size_t j = 0; j < d; ++j) u[j] = std::conj(w[(j + a) % d]) * w[j];
    dft.transform(u, -1);
    for (size_t b = 0; b < d; ++b)
      if (a || b)
        worst = std::max(worst, std::abs(std::norm(u[b]) - 1.0 / static_cast<double>(d + 1)));
  }
  return worst;
}
}  // namespace sic::core
