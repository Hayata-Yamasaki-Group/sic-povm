// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/arithmetic/balanced_product.hpp"

#include "assertions.hpp"

int main() {
  sic::support::ComplexBallVector factor(1), exact(1), reference(1), result(1), sequential(1),
      inverse(1);
  acb_one(factor[0]);
  arb_one(acb_imagref(factor[0]));
  acb_set(exact[0], factor[0]);
  arb_add_error_2exp_si(acb_realref(factor[0]), -100);
  arb_add_error_2exp_si(acb_imagref(factor[0]), -100);
  for (size_t count : {0, 1, 2, 3, 7, 15, 16, 17, 63, 1023, 1024}) {
    sic::support::BalancedComplexProduct product(count);
    for (int retry = 0; retry < 2; ++retry) {
      product.reset();
      acb_one(reference[0]);
      for (size_t q = 0; q < count; ++q) {
        product.append(factor[0], 128);
        acb_mul(reference[0], reference[0], factor[0], 2048);
      }
      product.get(result[0], 128);
      SIC_CHECK(acb_is_finite(result[0]));
      SIC_CHECK(!acb_contains_zero(result[0]));
      SIC_CHECK(acb_overlaps(result[0], reference[0]));
      acb_pow_ui(sequential[0], exact[0], count, 2048);
      SIC_CHECK(acb_contains(result[0], sequential[0]));
      if (count) SIC_CHECK(acb_rel_accuracy_bits(result[0]) > 70);
    }
    bool refused = false;
    try {
      product.append(factor[0], 128);
    } catch (const std::logic_error&) {
      refused = true;
    }
    SIC_CHECK(refused);
  }
  // Same nonzero factors and same precision: sequential rectangular
  // wrapping loses invertibility, while the balanced product remains useful.
  sic::support::BalancedComplexProduct product(1024);
  acb_one(sequential[0]);
  for (int q = 0; q < 1024; ++q) {
    product.append(factor[0], 128);
    acb_mul(sequential[0], sequential[0], factor[0], 128);
  }
  product.get(result[0], 128);
  SIC_CHECK(acb_contains_zero(sequential[0]));
  acb_inv(inverse[0], result[0], 128);
  SIC_CHECK(acb_is_finite(inverse[0]));
  return sic_test::report();
}
