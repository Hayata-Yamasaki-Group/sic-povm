// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/arithmetic/balanced_product.hpp"

namespace sic::support {
void BalancedComplexProduct::append(acb_srcptr factor, slong precision) {
  if (count_ == capacity_) throw std::logic_error("balanced product capacity exceeded");
  acb_set(work_[0], factor);
  size_t occupied = count_, level = 0;
  while (occupied & 1) {
    acb_mul(work_[0], partial_[level], work_[0], precision);
    occupied >>= 1;
    ++level;
  }
  acb_swap(partial_[level], work_[0]);
  ++count_;
}

void BalancedComplexProduct::get(acb_ptr result, slong precision) const {
  bool first = true;
  for (size_t occupied = count_, level = 0; occupied; occupied >>= 1, ++level) {
    if (!(occupied & 1)) continue;
    if (first) {
      acb_set(result, partial_[level]);
      first = false;
    } else
      acb_mul(result, partial_[level], result, precision);
  }
  if (first) acb_one(result);
}

}  // namespace sic::support

namespace sic::support {
BalancedComplexProduct::BalancedComplexProduct(size_t capacity)
    : partial_(std::bit_width(capacity)), capacity_(capacity) {}

}  // namespace sic::support
