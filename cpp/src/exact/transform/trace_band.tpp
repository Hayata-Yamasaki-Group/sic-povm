// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact {
template <class Generator>
void add_trace_band(arb_ptr destination, arb_srcptr value, long label,
                    std::span<const long> prefix_degrees, int band_bits, long band, slong precision,
                    const Generator& generator) {
  if (prefix_degrees.empty() || prefix_degrees.size() > 63)
    throw std::invalid_argument("invalid trace band prefix degrees");
  const int levels = static_cast<int>(prefix_degrees.size()) - 1;
  if (band_bits < 0 || band_bits > levels || band < 0 || band >= (1L << band_bits) ||
      prefix_degrees.front() != 1 || label < 0 || label >= prefix_degrees.back())
    throw std::invalid_argument("invalid trace band dimensions");
  for (int i = 0; i < levels; ++i) {
    const long before = prefix_degrees[i], after = prefix_degrees[i + 1];
    if (before <= 0 || after <= before || after % before ||
        (i >= levels - band_bits && after / before != 2))
      throw std::invalid_argument("trace band requires a quadratic suffix");
  }
  sic::support::RealBallVector work(1);
  arb_set(work[0], value);
  const int cut = levels - band_bits;
  for (int i = cut; i < levels; ++i)
    if ((band >> (i - cut)) & 1) {
      const long prefix = label >> (levels - i - 1);
      arb_mul(work[0], work[0], generator(i, prefix / 2), precision);
      if (prefix & 1) arb_neg(work[0], work[0]);
    }
  arb_add(destination, destination, work[0], precision);
}

}  // namespace sic::exact
