// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/transform/fixed_field_tower.hpp"

#include "assertions.hpp"
#include "exact/analytic/product_candidate.hpp"

int main() {
  const std::vector<char> support{0, 1, 0, 1}, trivial{1, 0, 0, 0}, preferred(4, 0);
  SIC_CHECK(sic::exact::quadratic_support_witness(
                4, support, trivial, preferred, [](long a, long b) { return (a + b) % 4; },
                [](long c) { return c % 2 == 0; }) == -1);
  const std::vector<char> diagonal_support{0, 1, 1, 0};
  SIC_CHECK(sic::exact::quadratic_support_witness(
                4, diagonal_support, trivial, preferred, [](long a, long b) { return a ^ b; },
                [](long c) { return c == 0 || c == 3; }) == -1);
  SIC_CHECK(sic::exact::quadratic_support_witness(
                4, diagonal_support, trivial, preferred, [](long a, long b) { return a ^ b; },
                [](long) { return true; }) == 1);
  const std::vector<char> order_two{1, 0, 1, 0};
  SIC_CHECK(sic::exact::quadratic_support_witness(
                4, support, order_two, preferred, [](long a, long b) { return (a + b) % 4; },
                [](long) { return true; }) == 1);
  sic::exact::ProductCandidate product(2, 2, 7), repeat(2, 2, 7);
  SIC_CHECK(product.factors == repeat.factors);
  sic::support::ComplexBallVector actual(1);
  product.evaluate(actual[0], 128, [](int j, acb_ptr value) { acb_set_si(value, j + 1); });
  long expected = 1;
  for (const auto& factor : product.factors) expected *= factor[0] + factor[1] + 2 * factor[2];
  SIC_CHECK(acb_equal_si(actual[0], expected));
  SIC_CHECK(product.height >= 1);
  return sic_test::report();
}
