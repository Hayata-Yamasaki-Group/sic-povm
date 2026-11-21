// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/transform/trace_band.hpp"

#include "assertions.hpp"
#include "exact/transform/quadratic_transform.hpp"

namespace {
void dyadic_bands() {
  for (int levels = 0; levels <= 8; ++levels) {
    const long count = 1L << levels;
    std::vector<long> prefix_degrees{1};
    for (int i = 0; i < levels; ++i) prefix_degrees.push_back(2 * prefix_degrees.back());
    sic::support::RealBallVector input(2 * count);
    std::vector<sic::support::RealBallVector> generators;
    for (int i = 0; i < levels; ++i) {
      generators.emplace_back(1L << i);
      for (long c = 0; c < 1L << i; ++c) {
        arb_set_si(generators.back()[c], c % 13 - 6);
        arb_add_error_2exp_si(generators.back()[c], -120);
      }
    }
    for (long q = 0; q < 2 * count; ++q) {
      arb_set_si(input[q], q % 37 - 18);
      arb_add_error_2exp_si(input[q], -116);
    }
    auto generator = [&](int i, long pair) { return generators[i][pair]; };
    sic::support::RealBallVector reference(input.size());
    for (size_t q = 0; q < input.size(); ++q) arb_set(reference[q], input[q]);
    sic::exact::quadratic_real_forward(reference.v, levels, 144, 2, true, generator);
    for (int bits = 0; bits <= levels; ++bits) {
      const long size = count >> bits;
      for (long band = 0; band < (1L << bits); ++band) {
        sic::support::RealBallVector values(2 * size);
        for (int channel = 0; channel < 2; ++channel)
          for (long label = 0; label < count; ++label)
            sic::exact::add_trace_band(values[channel * size + (label >> bits)],
                                       input[channel * count + label], label, prefix_degrees, bits,
                                       band, 144, generator);
        sic::exact::quadratic_real_forward(values.v, levels - bits, 144, 2, true, generator);
        for (int channel = 0; channel < 2; ++channel)
          for (long q = 0; q < size; ++q)
            SIC_CHECK(arb_overlaps(values[channel * size + q],
                                   reference[channel * count + band * size + q]));
      }
    }
  }
}

// Direct monomial traces, independent of the production butterfly transform.
// Odd roots need not be algebraic conjugates to test this linear identity.
long root(int level, long prefix, long digit, long radix) {
  const long even = 1 + (prefix + level) % 3;
  return radix == 2 ? (digit ? -even : even) : digit + even;
}

void direct_trace(sic::support::RealBallVector& output, const sic::support::RealBallVector& input,
                  const std::vector<long>& counts) {
  const long degree = counts.back();
  for (long exponent = 0; exponent < degree; ++exponent)
    for (long label = 0; label < degree; ++label) {
      sic::support::RealBallVector term(2);
      arb_set(term[0], input[label]);
      for (size_t i = 0; i + 1 < counts.size(); ++i) {
        const long radix = counts[i + 1] / counts[i];
        const long prefix = label / (degree / counts[i + 1]);
        arb_set_si(term[1], root(i, prefix / radix, prefix % radix, radix));
        arb_pow_ui(term[1], term[1], (exponent / counts[i]) % radix, 144);
        arb_mul(term[0], term[0], term[1], 144);
      }
      arb_add(output[exponent], output[exponent], term[0], 144);
    }
}

void mixed_bands(std::vector<long> counts) {
  const int odd_levels = counts.size() - 1;
  for (int i = 0; i < 4; ++i) counts.push_back(2 * counts.back());
  const long degree = counts.back();
  sic::support::RealBallVector input(degree), reference(degree);
  std::vector<sic::support::RealBallVector> generators;
  for (size_t i = 0; i + 1 < counts.size(); ++i) {
    generators.emplace_back(counts[i]);
    for (long c = 0; c < counts[i]; ++c) arb_set_si(generators.back()[c], root(i, c, 0, 2));
  }
  for (long label = 0; label < degree; ++label) arb_set_si(input[label], label % 17 - 8);
  direct_trace(reference, input, counts);
  const int levels = counts.size() - 1;
  for (int bits = 0; bits <= levels - odd_levels; ++bits) {
    const long size = degree >> bits;
    const std::vector<long> lower(counts.begin(), counts.end() - bits);
    for (long band = 0; band < (1L << bits); ++band) {
      sic::support::RealBallVector values(size), actual(size);
      for (long label = 0; label < degree; ++label)
        sic::exact::add_trace_band(values[label >> bits], input[label], label, counts, bits, band,
                                   144, [&](int i, long c) { return generators[i][c]; });
      direct_trace(actual, values, lower);
      for (long q = 0; q < size; ++q) SIC_CHECK(arb_equal(actual[q], reference[band * size + q]));
    }
  }
}
}  // namespace

int main() {
  dyadic_bands();
  mixed_bands({1, 3});
  mixed_bands({1, 5});
  mixed_bands({1, 3, 15});
  // Invalid prefixes, labels and nonquadratic removed suffixes fail closed.
  sic::support::RealBallVector value(1);
  auto rejects = [&](std::vector<long> counts, long label, int bits, long band) {
    try {
      sic::exact::add_trace_band(value[0], value[0], label, counts, bits, band, 144,
                                 [&](int, long) { return value[0]; });
      return false;
    } catch (const std::invalid_argument&) {
      return true;
    }
  };
  SIC_CHECK(rejects({}, 0, 0, 0));
  SIC_CHECK(rejects({2, 4}, 0, 1, 0));
  SIC_CHECK(rejects({1, 3, 5}, 0, 1, 0));
  SIC_CHECK(rejects({1, 3, 6}, 6, 1, 0));
  SIC_CHECK(rejects({1, 3, 6}, -1, 1, 0));
  SIC_CHECK(rejects({1, 3, 6}, 0, 2, 0));
  SIC_CHECK(rejects({1, 3, 6}, 0, 1, 2));
  return sic_test::report();
}
