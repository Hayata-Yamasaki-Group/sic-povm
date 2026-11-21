// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/transform/leaf_transform.hpp"

#include <numeric>

#include "assertions.hpp"

static void compare(const std::vector<long>& degrees, std::vector<long> labels) {
  long count = 1;
  for (long degree : degrees) count *= degree;
  sic::support::ComplexBallVector values(count);
  for (long e = 0; e < count; ++e) acb_set_si(values[e], (e * 7) % 101 - 50);
  auto term = [](acb_ptr out, acb_srcptr child, int i, long label, long k) {
    // Label-dependent exact coefficients exercise prefix indexing and
    // mixed-prime terms without relying on a particular SIC tower.
    const long coefficient = (i + 1) * (label + 1) * (k + 1);
    if (k == 0)
      acb_mul_si(out, child, coefficient, 128);
    else
      acb_addmul_si(out, child, coefficient, 128);
  };
  long seen = 0;
  sic::support::ComplexBallVector actual = sic::exact::inverse_leaf_tree(
      degrees, degrees.size(), labels,
      [&](long e, acb_ptr out) {
        SIC_CHECK(e == seen++);  // spool cursors only need sequential reads
        acb_set(out, values[e]);
      },
      term);
  SIC_CHECK(seen == count);
  long lower = 1;
  for (size_t i = 0; i < degrees.size(); ++i) {
    const long degree = degrees[i], block_size = lower * degree;
    sic::support::ComplexBallVector next(count);
    for (long block = 0; block < count / block_size; ++block)
      for (long label = 0; label < block_size; ++label)
        for (long k = 0; k < degree; ++k)
          term(next[block * block_size + label],
               values[block * block_size + k * lower + label / degree], i, label, k);
    values = std::move(next);
    lower = block_size;
  }
  std::sort(labels.begin(), labels.end());
  labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
  SIC_CHECK(actual.size() == labels.size());
  for (size_t q = 0; q < labels.size(); ++q) SIC_CHECK(acb_equal(actual[q], values[labels[q]]));
}

int main() {
  compare({}, {0});
  compare({2, 3, 2}, {11, 0, 5, 5});
  compare({3, 2, 5}, {0, 1, 29, 17});
  compare(std::vector<long>(12, 2), {4095, 0, 2741, 1370});
  std::vector<long> all(2048);
  std::iota(all.begin(), all.end(), 0);
  compare(std::vector<long>(11, 2), all);
  bool refused = false;
  try {
    sic::exact::inverse_leaf_tree(
        std::vector<long>{2}, 1, {2}, [](long, acb_ptr) {},
        [](acb_ptr, acb_srcptr, int, long, long) {});
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  SIC_CHECK(refused);
  return sic_test::report();
}
