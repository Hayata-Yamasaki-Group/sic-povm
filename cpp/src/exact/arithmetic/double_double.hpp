// SPDX-License-Identifier: AGPL-3.0-or-later
// Heuristic point arithmetic only; mathematical acceptance always uses Arb.
#pragma once
#include <cmath>

namespace sic::exact {
struct DoubleDouble {
  double hi = 0, lo = 0;
  DoubleDouble(double x = 0) : hi(x) {}
  DoubleDouble(double h, double l) : hi(h), lo(l) {}
  explicit operator double() const { return hi + lo; }
  static DoubleDouble normalize(double h, double l) {
    const double s = h + l;
    return {s, l - (s - h)};
  }
  friend DoubleDouble operator+(DoubleDouble a, DoubleDouble b) {
    const double s = a.hi + b.hi, v = s - a.hi;
    return normalize(s, (a.hi - (s - v)) + (b.hi - v) + a.lo + b.lo);
  }
  friend DoubleDouble operator-(DoubleDouble a) { return {-a.hi, -a.lo}; }
  friend DoubleDouble operator-(DoubleDouble a, DoubleDouble b) { return a + (-b); }
  friend DoubleDouble operator*(DoubleDouble a, DoubleDouble b) {
    const double p = a.hi * b.hi;
    return normalize(p, std::fma(a.hi, b.hi, -p) + a.hi * b.lo + a.lo * b.hi + a.lo * b.lo);
  }
  friend DoubleDouble operator/(DoubleDouble a, DoubleDouble b) {
    const double q = a.hi / b.hi;
    const DoubleDouble r = a - b * DoubleDouble(q);
    return normalize(q, (r.hi + r.lo) / b.hi);
  }
  DoubleDouble& operator+=(DoubleDouble b) { return *this = *this + b; }
  DoubleDouble& operator-=(DoubleDouble b) { return *this = *this - b; }
  DoubleDouble& operator*=(DoubleDouble b) { return *this = *this * b; }
  friend bool operator<(DoubleDouble a, DoubleDouble b) {
    return a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo);
  }
  friend bool operator>(DoubleDouble a, DoubleDouble b) { return b < a; }
  friend bool operator<=(DoubleDouble a, DoubleDouble b) { return !(b < a); }
};
}  // namespace sic::exact
