// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/numeric/frame_diagnostics.hpp"
namespace sic::core {
FrameDiagnostics frame_diagnostics(const SicFrame& frame) {
  const int d = frame.dim();
  double worst = 0;
  sic::core::Complex checksum = 0;
  frame.for_each_vector([&](std::int64_t, std::int64_t, const sic::core::Complex* v) {
    double n2 = 0;
    for (int k = 0; k < d; ++k) n2 += std::norm(v[k]);
    worst = std::max(worst, std::abs(std::sqrt(n2) - 1));
    checksum += v[0];
  });

  return {worst, checksum};
}
}  // namespace sic::core
