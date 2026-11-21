// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/support/flint_raii.hpp"
namespace sic::exact {
template <class Generator>
void quadratic_real_forward(std::vector<arb_struct>& values, int levels, slong precision,
                            int threads, bool fuse, const Generator& generator,
                            const std::function<void(int, bool)>& resident) {
  if (levels < 0 || levels >= 63) throw std::invalid_argument("quadratic transform level count");
  if (!levels || values.empty()) return;
  const long block = 1L << levels;
  if (values.size() % block) throw std::invalid_argument("quadratic transform block size");
  long stride = 1;
  for (int i = levels - 1; i >= 0; --i) {
    // With backing files, the first level alone bounds residency; each later
    // fused pair is smaller than that largest generator.
    const bool two = fuse && i > 0 && (!resident || i < levels - 1);
    if (resident) {
      resident(i, true);
      if (two) resident(i - 1, true);
    }
    const long radix = two ? 4 : 2, total = (long)values.size() / radix;
    const long chunks = std::min<long>(total, 16L * std::max(1, threads));
    parallel_for(chunks, threads, [&](long chunk) {
      std::array<sic::support::flint::Arb, 6> work;
      for (long u = chunk * total / chunks; u < (chunk + 1) * total / chunks; ++u) {
        const long per = block / radix, base = (u / per) * block;
        const long within = u % per, c = within / stride, offset = within % stride;
        const long x = base + radix * c * stride + offset;
        arb_ptr a0 = &values[x], a1 = &values[x + stride];
        arb_add(work[0].get(), a0, a1, precision);
        arb_sub(work[1].get(), a0, a1, precision);
        arb_mul(work[1].get(), work[1].get(), generator(i, two ? 2 * c : c), precision);
        if (two) {
          arb_ptr a2 = &values[x + 2 * stride], a3 = &values[x + 3 * stride];
          arb_add(work[2].get(), a2, a3, precision);
          arb_sub(work[3].get(), a2, a3, precision);
          arb_mul(work[3].get(), work[3].get(), generator(i, 2 * c + 1), precision);
          arb_add(a0, work[0].get(), work[2].get(), precision);
          arb_sub(work[4].get(), work[0].get(), work[2].get(), precision);
          arb_mul(a2, work[4].get(), generator(i - 1, c), precision);
          arb_add(a1, work[1].get(), work[3].get(), precision);
          arb_sub(work[5].get(), work[1].get(), work[3].get(), precision);
          arb_mul(a3, work[5].get(), generator(i - 1, c), precision);
        } else {
          arb_swap(a0, work[0].get());
          arb_swap(a1, work[1].get());
        }
      }
    });
    if (resident) {
      resident(i, false);
      if (two) resident(i - 1, false);
    }
    stride *= radix;
    if (two) --i;
  }
  const long count = (long)values.size();
  const long chunks = std::min<long>(count, 16L * std::max(1, threads));
  parallel_for(chunks, threads, [&](long chunk) {
    for (long q = chunk * count / chunks; q < (chunk + 1) * count / chunks; ++q) {
      const long base = (q / block) * block, index = q % block;
      const long reverse = (long)reverse_low_bits(index, levels);
      if (index < reverse) arb_swap(&values[q], &values[base + reverse]);
    }
  });
}

template <class Generator, class Window>
void quadratic_real_forward_windowed(std::vector<arb_struct>& values, int levels, slong precision,
                                     int threads, size_t window, const Generator& generator,
                                     const Window& resident) {
  if (levels < 0 || levels >= 63 || !window)
    throw std::invalid_argument("windowed quadratic transform dimensions");
  if (!levels || values.empty()) return;
  const long block = 1L << levels, count = values.size();
  if (count % block) throw std::invalid_argument("windowed quadratic transform block size");
  long stride = 1;
  for (int i = levels - 1; i >= 0; --i, stride *= 2) {
    const long labels = 1L << i;
    for (long first = 0; first < labels;) {
      const long size = std::min<size_t>(window, labels - first);
      resident(i, first, size, true);
      const long per_block = size * stride, tasks = (count / block) * per_block;
      const long chunks = std::min<long>(tasks, 16L * std::max(1, threads));
      parallel_for(chunks, threads, [&](long chunk) {
        std::array<sic::support::flint::Arb, 2> work;
        for (long u = chunk * tasks / chunks; u < (chunk + 1) * tasks / chunks; ++u) {
          const long base = (u / per_block) * block, offset = u % per_block;
          const long c = first + offset / stride, x = base + 2 * c * stride + offset % stride;
          arb_add(work[0].get(), &values[x], &values[x + stride], precision);
          arb_sub(work[1].get(), &values[x], &values[x + stride], precision);
          arb_mul(work[1].get(), work[1].get(), generator(i, c), precision);
          arb_swap(&values[x], work[0].get());
          arb_swap(&values[x + stride], work[1].get());
        }
      });
      resident(i, first, size, false);
      first += size;
    }
  }
  const long chunks = std::min<long>(count, 16L * std::max(1, threads));
  parallel_for(chunks, threads, [&](long chunk) {
    for (long q = chunk * count / chunks; q < (chunk + 1) * count / chunks; ++q) {
      const long base = (q / block) * block, index = q % block;
      const long reverse = reverse_low_bits(index, levels);
      if (index < reverse) arb_swap(&values[q], &values[base + reverse]);
    }
  });
}

template <class Dual>
void quadratic_complex_inverse(std::vector<acb_struct>& values, int levels, slong precision,
                               int threads, const Dual& dual) {
  if (levels < 0 || levels >= 63) throw std::invalid_argument("quadratic inverse level count");
  if (!levels || values.empty()) return;
  const long block = 1L << levels, count = values.size();
  if (count % block) throw std::invalid_argument("quadratic inverse block size");
  const long permutation_chunks = std::min<long>(count, 16L * std::max(1, threads));
  parallel_for(permutation_chunks, threads, [&](long chunk) {
    for (long q = chunk * count / permutation_chunks; q < (chunk + 1) * count / permutation_chunks;
         ++q) {
      const long base = (q / block) * block, index = q % block;
      const long reverse = reverse_low_bits(index, levels);
      if (index < reverse) acb_swap(&values[q], &values[base + reverse]);
    }
  });
  long stride = block / 2;
  for (int i = 0; i < levels; ++i, stride /= 2) {
    const long total = count / 2, per = block / 2;
    const long chunks = std::min<long>(total, 16L * std::max(1, threads));
    parallel_for(chunks, threads, [&](long chunk) {
      std::array<sic::support::flint::Acb, 3> work;
      for (long u = chunk * total / chunks; u < (chunk + 1) * total / chunks; ++u) {
        const long base = (u / per) * block, within = u % per;
        const long c = within / stride, offset = within % stride;
        const long x = base + 2 * c * stride + offset;
        acb_mul_2exp_si(work[0].get(), &values[x], -1);
        dual(work[2].get(), i, c);
        acb_mul(work[1].get(), &values[x + stride], work[2].get(), precision);
        acb_add(&values[x], work[0].get(), work[1].get(), precision);
        acb_sub(&values[x + stride], work[0].get(), work[1].get(), precision);
      }
    });
  }
}

}  // namespace sic::exact
