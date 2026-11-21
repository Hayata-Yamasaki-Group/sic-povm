// SPDX-License-Identifier: AGPL-3.0-or-later
// A high-exponent band is a partial trace against the corresponding monomial.
// After this accumulation, the ordinary lower-prefix FFT emits a contiguous
// band in natural leaf order. Prefix degrees start at 1 and include the
// complete field degree. Only the removed suffix must be quadratic; the
// retained prefix may contain any odd degrees.
// The caller owns synchronization of destination.
#pragma once

#include <span>
#include <stdexcept>

#include "sic/support/ball_vectors.hpp"

namespace sic::exact {

template <class Generator>
void add_trace_band(arb_ptr destination, arb_srcptr value, long label,
                    std::span<const long> prefix_degrees, int band_bits, long band, slong precision,
                    const Generator& generator);

}  // namespace sic::exact

#include "exact/transform/trace_band.tpp"
