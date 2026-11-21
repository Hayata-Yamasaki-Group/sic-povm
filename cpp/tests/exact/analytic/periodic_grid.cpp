// SPDX-License-Identifier: AGPL-3.0-or-later
#include "assertions.hpp"
#include "exact/group/group_fourier.hpp"

static void check(const std::vector<long>& dimensions, const std::vector<long>& periods) {
  sic::support::PeriodicGrid grid(dimensions, periods);
  sic::support::ComplexBallVector full(grid.full_count), reduced(grid.count);
  for (long g = 0; g < grid.full_count; ++g) {
    long source = 0;
    for (size_t l = 0; l < dimensions.size(); ++l)
      source += ((g / grid.stride[l]) % periods[l]) * grid.reduced_stride[l];
    acb_set_si(full[g], source % 7 - 3);
    arb_set_si(acb_imagref(full[g]), source % 5 - 2);
  }
  for (long q = 0; q < grid.count; ++q) acb_set(reduced[q], full[grid.element(q)]);
  sic::exact::rectangular_fourier(dimensions, full, 128, 2);
  sic::exact::rectangular_fourier(periods, reduced, 128, 2);
  std::vector<char> allowed(grid.full_count, 0);
  for (long q = 0; q < grid.count; ++q) {
    const long c = grid.character(q);
    SIC_CHECK(!allowed[c]);
    allowed[c] = 1;
    SIC_CHECK(acb_overlaps(reduced[q], full[c]));
  }
  for (long c = 0; c < grid.full_count; ++c)
    if (!allowed[c]) SIC_CHECK(acb_contains_zero(full[c]));
}

int main() {
  check({4, 6}, {2, 3});
  check({2, 4, 8}, {1, 2, 4});
  check({2, 4, 8}, {2, 4, 8});
  check({4, 6}, {1, 1});
  const auto periods = sic::support::exact_axis_periods(
      std::vector<long>{12, 20}, [](size_t l, long p) { return p % (l ? 5 : 3) == 0; });
  SIC_CHECK(periods == std::vector<long>({3, 5}));
  const sic::support::PeriodicGrid grid({4, 6}, {2, 3});
  for (long q = 0; q < grid.count; ++q) {
    SIC_CHECK(grid.element(q) == q % 2 + 4 * (q / 2));
    SIC_CHECK(grid.character(q) == 2 * (q % 2) + 8 * (q / 2));
  }
  return sic_test::report();
}
