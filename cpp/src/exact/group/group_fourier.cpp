// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/group/group_fourier.hpp"

namespace sic::exact {
void rectangular_fourier(const std::vector<long>& dimensions,
                         sic::support::ComplexBallVector& values, slong wp, int threads) {
  sic::support::PeriodicGrid grid(dimensions, dimensions);
  const long count = grid.count;
  if (values.size() != (size_t)count) throw std::invalid_argument("wrong group Fourier input size");
  for (size_t l = 0; l < dimensions.size(); ++l) {
    const long n = dimensions[l], stride = grid.stride[l];
    if (n == 1) continue;
    acb_dft_pre_t pre;
    acb_dft_precomp_init(pre, n, wp);
    const long lines = count / n, chunks = std::min<long>(lines, 16L * threads);
    parallel_for(chunks, threads, [&](long chunk) {
      sic::support::ComplexBallVector in(n), out(n);
      for (long line = chunk * lines / chunks; line < (chunk + 1) * lines / chunks; ++line) {
        const long start = (line / stride) * stride * n + line % stride;
        for (long j = 0; j < n; ++j) acb_set(in[j], values[start + j * stride]);
        acb_dft_precomp(out.v.data(), in.v.data(), pre, wp);
        for (long j = 0; j < n; ++j) acb_swap(values[start + j * stride], out[j]);
      }
    });
    acb_dft_precomp_clear(pre);
  }
  parallel_for(count, threads, [&](long c) { acb_div_si(values[c], values[c], count, wp); });
}

}  // namespace sic::exact
