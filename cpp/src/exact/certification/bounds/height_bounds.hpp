// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <flint/acb.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sic::exact::certified {

// Nonnegative upper bounds only. All arithmetic rounds outwards in mag_t;
// logarithms are used solely to propose an exponent checked against the bound.
class HeightBound {
  mag_t value_;

 public:
  HeightBound();
  explicit HeightBound(ulong n);
  HeightBound(const HeightBound& b);
  HeightBound& operator=(const HeightBound& b) {
    mag_set(value_, b.value_);
    return *this;
  }
  ~HeightBound() { mag_clear(value_); }
  mag_srcptr get() const { return value_; }

  static HeightBound absolute_integer(slong n);
  static HeightBound absolute_ball(const arb_t x) {
    HeightBound b;
    arb_get_mag(b.value_, x);
    return b;
  }
  static HeightBound absolute_complex(const acb_t x) {
    HeightBound b;
    acb_get_mag(b.value_, x);
    return b;
  }
  HeightBound maximum(const HeightBound& b) const {
    HeightBound r;
    mag_max(r.value_, value_, b.value_);
    return r;
  }
  static HeightBound binomial(ulong n, ulong k);
  HeightBound operator+(const HeightBound& b) const {
    HeightBound r;
    mag_add(r.value_, value_, b.value_);
    return r;
  }
  HeightBound operator*(const HeightBound& b) const {
    HeightBound r;
    mag_mul(r.value_, value_, b.value_);
    return r;
  }
  HeightBound pow(ulong n) const {
    HeightBound r;
    mag_pow_ui(r.value_, value_, n);
    return r;
  }
  slong upper_binary_exponent() const;
};

}  // namespace sic::exact::certified
