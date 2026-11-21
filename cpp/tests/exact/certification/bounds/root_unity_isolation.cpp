// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/bounds/root_unity_isolation.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}

struct AcbValue {
  acb_t v;
  AcbValue() { acb_init(v); }
  ~AcbValue() { acb_clear(v); }
  AcbValue(const AcbValue&) = delete;
  AcbValue& operator=(const AcbValue&) = delete;
};

void set_root(acb_t root, ulong index, ulong order, slong precision) {
  arb_t angle;
  arb_init(angle);
  arb_const_pi(angle, precision);
  arb_mul_ui(angle, angle, 2 * index, precision);
  arb_div_ui(angle, angle, order, precision);
  arb_sin_cos(acb_imagref(root), acb_realref(root), angle, precision);
  arb_clear(angle);
}

void add_complex_radius(acb_t value, slong exponent) {
  arb_add_error_2exp_si(acb_realref(value), exponent);
  arb_add_error_2exp_si(acb_imagref(value), exponent);
}

void test_order_and_precision(ulong order, slong precision) {
  constexpr ulong kIndex = 123456;
  sic::exact::RootUnityIsolation isolation(order, precision);
  AcbValue exact, tiny_value, tiny_candidate, adjacent, wide_value, wide_candidate, nonfinite;
  set_root(exact.v, kIndex, order, precision);
  acb_set(tiny_value.v, exact.v);
  acb_set(tiny_candidate.v, exact.v);
  add_complex_radius(tiny_value.v, -precision + 8);
  add_complex_radius(tiny_candidate.v, -precision + 8);
  require(isolation.matches(tiny_value.v, tiny_candidate.v),
          "tiny-error enclosure of the same root was rejected");

  set_root(adjacent.v, kIndex + 1, order, precision);
  add_complex_radius(adjacent.v, -precision + 8);
  acb_set(wide_value.v, exact.v);
  add_complex_radius(wide_value.v, -18);
  require(acb_overlaps(wide_value.v, adjacent.v),
          "nearby value ball and adjacent-root candidate do not overlap as intended");
  require(!isolation.matches(wide_value.v, adjacent.v),
          "adjacent root of unity was accepted as the requested root");

  acb_set(wide_value.v, exact.v);
  add_complex_radius(wide_value.v, -12);
  require(acb_overlaps(wide_value.v, adjacent.v),
          "wide value ball does not cover the adjacent root");
  require(!isolation.matches(wide_value.v, adjacent.v),
          "wide value ball covering two roots passed isolation");

  acb_set(wide_candidate.v, adjacent.v);
  add_complex_radius(wide_candidate.v, -18);
  require(acb_overlaps(tiny_value.v, wide_candidate.v),
          "wide candidate ball does not overlap the target value");
  require(!isolation.matches(tiny_value.v, wide_candidate.v),
          "wide candidate ball covering multiple roots passed isolation");

  acb_indeterminate(nonfinite.v);
  require(!isolation.matches(nonfinite.v, tiny_candidate.v),
          "nonfinite value passed root isolation");
  require(!isolation.matches(tiny_value.v, nonfinite.v),
          "nonfinite candidate passed root isolation");
}

}  // namespace

int main() {
  try {
    constexpr ulong kOrder = 4UL * 1024UL * 1024UL;
    test_order_and_precision(kOrder, 64);
    test_order_and_precision(kOrder, 160);
    std::cout << "root of unity isolation tests passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "root of unity isolation test failed: " << e.what() << '\n';
    return 1;
  }
}
