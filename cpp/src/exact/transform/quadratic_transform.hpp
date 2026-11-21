// SPDX-License-Identifier: AGPL-3.0-or-later
// In-place real Galois butterflies for an entirely quadratic tower.
#pragma once
#include <flint/acb.h>

#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>

#include "exact/runtime/parallel.hpp"

namespace sic::exact {

std::uint64_t reverse_low_bits(std::uint64_t value, int bits);

// generator(i,c) is the real even-coset value g_i(2c). After each
// descending level, consumed exponent bits occupy the low physical bits in
// reverse order. One final bit reversal restores the existing leaf labels.
template <class Generator>
void quadratic_real_forward(std::vector<arb_struct>& values, int levels, slong precision,
                            int threads, bool fuse, const Generator& generator,
                            const std::function<void(int, bool)>& resident = {});

// The same single-level butterflies with a bounded window of generator
// labels. Each value is touched once per level; windows partition c, while
// all blocks and offsets of that c use the same resident generator piece.
template <class Generator, class Window>
void quadratic_real_forward_windowed(std::vector<arb_struct>& values, int levels, slong precision,
                                     int threads, size_t window, const Generator& generator,
                                     const Window& resident);

// Physical conjugates need complex duals even when the analytic tower is real.
// Undo the same strided layout, with bit reversal before ascending levels.
template <class Dual>
void quadratic_complex_inverse(std::vector<acb_struct>& values, int levels, slong precision,
                               int threads, const Dual& dual);

}  // namespace sic::exact

#include "exact/transform/quadratic_transform.tpp"
