// SPDX-License-Identifier: AGPL-3.0-or-later
#include <numeric>
#include <limits>
#include <map>
#include <tuple>

#include "assertions.hpp"
#include "exact/arithmetic/arith.hpp"
#include "exact/certification/selection/selection_float.hpp"

static auto form_key(const sic::exact::Form& f) { return std::tuple{f.A, -f.B, f.C}; }

static bool same_form(const sic::exact::Form& a, const sic::exact::Form& b) {
  return form_key(a) == form_key(b);
}

// Keep the A,-B enumeration as a small-dimension regression oracle.
// Comparing full cycles also checks that sorted form order and class order
// remain stable, not only that the reduced-form set has the same cardinality.
static void compare_classes(const sic::exact::Arithmetic& a) {
  using namespace sic::exact;
  std::vector<Form> red;
  long sq = 0;
  while ((sq + 1) * (sq + 1) <= a.Delta) ++sq;
  for (long A = 1; A <= a.Delta; ++A) {
    const long blo = std::max(sq + 1, 2 * A - sq), bhi = 2 * A + sq;
    for (long b = blo; b <= bhi; ++b) {
      const long B = -b;
      const long numerator = B * B - a.Delta;
      if (numerator <= 0 || numerator % (4 * A) != 0) continue;
      const long C = numerator / (4 * A);
      if (C <= 0 || A + B + C >= 0 || std::gcd(std::gcd(A, -B), C) != 1) continue;
      red.push_back({A, B, C});
    }
  }
  std::map<std::tuple<long, long, long>, size_t> index;
  for (size_t i = 0; i < red.size(); ++i) index[form_key(red[i])] = i;
  std::vector<bool> seen(red.size(), false);
  std::vector<std::vector<Form>> classes;
  for (size_t i = 0; i < red.size(); ++i) {
    if (seen[i]) continue;
    std::vector<Form> cycle;
    Form f = red[i];
    for (;;) {
      const auto it = index.find(form_key(f));
      SIC_CHECK(it != index.end());
      if (it == index.end() || seen[it->second]) break;
      seen[it->second] = true;
      cycle.push_back(f);
      const QuadraticNumber tau = a.tau_of(f);
      const long b = ceil1(a.kc, tau);
      f = {f.A * b * b + f.B * b + f.C, -2 * f.A * b - f.B, f.A};
    }
    classes.push_back(std::move(cycle));
  }
  long pb = sq + 1;
  if (pb % 2 == 0) ++pb;
  size_t principal = classes.size();
  for (size_t c = 0; c < classes.size(); ++c)
    for (const Form& f : classes[c])
      if (f.A == 1 && f.B == -pb) principal = c;
  SIC_CHECK(principal < classes.size());
  if (principal < classes.size() && principal != 0) std::swap(classes[0], classes[principal]);
  SIC_CHECK(classes.size() == a.cls.size());
  if (classes.size() == a.cls.size())
    for (size_t c = 0; c < classes.size(); ++c) {
      SIC_CHECK(classes[c].size() == a.cls[c].cycle.size());
      if (classes[c].size() == a.cls[c].cycle.size())
        for (size_t i = 0; i < classes[c].size(); ++i)
          SIC_CHECK(same_form(classes[c][i], a.cls[c].cycle[i]));
    }
}

int main() {
  using namespace sic::exact;
  const SelectionFloat large(1e16);
  SIC_CHECK(static_cast<double>((large + SelectionFloat(1)) - large) == 1);
  SIC_CHECK(static_cast<double>(SelectionFloat(3) / SelectionFloat(2)) == 1.5);
  Arithmetic standard(7), minimal(7, true), d256(8, true), d512(9, true), d1024(10, true);
  SIC_CHECK(standard.h() == 20);
  SIC_CHECK(minimal.h() == 4);
  SIC_CHECK(minimal.Delta == 645);
  SIC_CHECK(minimal.order_index == 5);
  SIC_CHECK(d256.Delta == 65021 && d256.order_index == 1 && d256.h() == 24);
  SIC_CHECK(d512.original_Delta == 261117 && d512.Delta == 29013);
  SIC_CHECK(d512.order_index == 3 && d512.h() == 12);
  SIC_CHECK(d1024.original_Delta == 1046525 && d1024.Delta == 41861);
  SIC_CHECK(d1024.order_index == 5 && d1024.h() == 20);
  for (int small_s = 3; small_s <= 8; ++small_s) {
    Arithmetic small(small_s);
    compare_classes(small);
  }
  compare_classes(minimal);
  SIC_CHECK(norm(minimal.kc, minimal.eps) == Rational(1));
  SIC_CHECK(norm(d512.kc, d512.eps) == Rational(1));
  SIC_CHECK(norm(d1024.kc, d1024.eps) == Rational(1));
  const auto& tau = minimal.cls[0].tau[0];
  SIC_CHECK(equal(minimal.eps, sub(scal(Rational(5), tau), QuadraticNumber(4))));
  const auto& tau512 = d512.cls[0].tau[0];
  SIC_CHECK(equal(d512.eps, sub(scal(Rational(3), tau512), QuadraticNumber(1))));
  const auto& tau1024 = d1024.cls[0].tau[0];
  SIC_CHECK(equal(d1024.eps, sub(scal(Rational(5), tau1024), QuadraticNumber(1))));
  // Every reduced cycle used by the table must have period epsilon, not a
  // smaller positive unit. Otherwise the three-cycle product changes meaning.
  for (const auto& cl : minimal.cls) {
    QuadraticNumber period(1);
    for (size_t j = 0; j < cl.tau.size(); ++j)
      period =
          mul(minimal.kc, period, inv(minimal.kc, sub(QuadraticNumber(cl.digit[j]), cl.tau[j])));
    SIC_CHECK(equal(period, minimal.eps));
  }
  // The d=512 reduced-order tower uses epsilon=(511+3 sqrt(29013))/2;
  // verify every reduced cycle has exactly this period before enabling it.
  for (const auto& cl : d512.cls) {
    QuadraticNumber period(1);
    for (size_t j = 0; j < cl.tau.size(); ++j)
      period = mul(d512.kc, period, inv(d512.kc, sub(QuadraticNumber(cl.digit[j]), cl.tau[j])));
    SIC_CHECK(equal(period, d512.eps));
  }
  // epsilon=(1023+5 sqrt(41861))/2 is a unit; the exact norm equations
  // x^2 - 41861 y^2 = +/-4 have no solutions for 1 <= y < 5.
  auto is_square = [](long n) {
    if (n < 0) return false;
    long lo = 0, hi = n + 1;
    while (hi - lo > 1) {
      const long mid = lo + (hi - lo) / 2;
      if (mid <= n / mid)
        lo = mid;
      else
        hi = mid;
    }
    return lo * lo == n;
  };
  for (long y = 1; y <= 5; ++y) {
    const long y2D = 41861 * y * y;
    if (y < 5) {
      SIC_CHECK(!is_square(y2D - 4));
      SIC_CHECK(!is_square(y2D + 4));
    } else {
      SIC_CHECK(!is_square(y2D - 4));
      SIC_CHECK(is_square(y2D + 4));
      SIC_CHECK(1023L * 1023L == y2D + 4);
    }
  }
  // Check every reduced cycle has exactly epsilon as its period.
  for (const auto& cl : d1024.cls) {
    QuadraticNumber period(1);
    for (size_t j = 0; j < cl.tau.size(); ++j)
      period = mul(d1024.kc, period, inv(d1024.kc, sub(QuadraticNumber(cl.digit[j]), cl.tau[j])));
    SIC_CHECK(equal(period, d1024.eps));
  }
  Arithmetic d2048(11);
  SIC_CHECK(d2048.Delta == 4190205);
  SIC_CHECK(d2048.h() == 128);
  SIC_CHECK(d2048.cls[0].cycle[0].A == 1 && d2048.cls[0].cycle[0].B == -2047 &&
            d2048.cls[0].cycle[0].C == 1);
  SIC_CHECK(equal(d2048.eps, d2048.cls[0].tau[0]));
  size_t d2048_states = 0, d2048_min_cycle = std::numeric_limits<size_t>::max(),
         d2048_max_cycle = 0;
  for (const auto& cl : d2048.cls) {
    d2048_states += cl.cycle.size();
    d2048_min_cycle = std::min(d2048_min_cycle, cl.cycle.size());
    d2048_max_cycle = std::max(d2048_max_cycle, cl.cycle.size());
    QuadraticNumber period(1);
    for (size_t j = 0; j < cl.tau.size(); ++j)
      period = mul(d2048.kc, period, inv(d2048.kc, sub(QuadraticNumber(cl.digit[j]), cl.tau[j])));
    SIC_CHECK(equal(period, d2048.eps));
  }
  SIC_CHECK(d2048_states == 5462);
  SIC_CHECK(d2048_min_cycle == 1 && d2048_max_cycle == 2045);
  Arithmetic d8(3, true);
  SIC_CHECK(d8.Delta == 5 && d8.order_index == 3);
  return sic_test::report();
}
