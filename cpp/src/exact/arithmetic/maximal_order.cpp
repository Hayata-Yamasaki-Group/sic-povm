// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/arithmetic/maximal_order.hpp"

#include "sic/error.hpp"

namespace sic::exact::reduced_order {
void contract(bool ok, const std::string& what, std::optional<sic::ErrorKind> kind) {
  if (!ok) {
    if (kind) throw sic::Error(*kind, "reduced order contract: " + what);
    throw std::runtime_error("reduced order contract: " + what);
  }
}

QuadraticNumber class_cycle_unit(const Arithmetic& ar, sic::support::Index c) {
  const ClassRep& cr = ar.cls[c];
  const size_t r = cr.digit.size();
  contract(r >= 1 && cr.tau.size() == r && cr.scal.size() == r && cr.cycle.size() == r,
           "class cycle data");
  return mul(ar.kc, cr.scal[r - 1],
             inv(ar.kc, sub(QuadraticNumber(cr.digit[r - 1]), cr.tau[r - 1])));
}

CycleUnit cycle_unit(const Arithmetic& ar) {
  const QuadraticContext& kc = ar.kc;
  CycleUnit result{class_cycle_unit(ar, 0), 0};
  const QuadraticNumber& u = result.u;
  contract(totally_positive(kc, u) && norm(kc, u) == Rational(1) &&
               sign1(kc, sub(u, QuadraticNumber(1))) > 0,
           "cycle unit is a totally positive unit > 1", sic::ErrorKind::construction);
  for (sic::support::Index c = 1; c < ar.h(); ++c)
    contract(equal(class_cycle_unit(ar, c), u), "every class has the same cycle unit",
             sic::ErrorKind::construction);
  QuadraticNumber power = u;
  for (int k = 1; k <= kMaxPower; ++k) {
    if (equal(power, ar.eps)) {
      result.k = k;
      return result;
    }
    if (sign1(kc, sub(power, ar.eps)) > 0) break;
    power = mul(kc, power, u);
  }
  throw sic::Error(sic::ErrorKind::construction,
                   "reduced order contract: epsilon is not a power of the cycle unit");
}

CycleUnit configure_arith(Arithmetic& ar) {
  contract(ar.order_index == 1 && ar.Delta == ar.original_Delta,
           "expected the standard-order arithmetic");
  const auto [fundamental, index] = maximal_quadratic_order(ar.original_Delta);
  if (index != 1) {
    ar.Delta = fundamental;
    ar.order_index = index;
    ar.kc.Delta = fundamental;
    ar.eps = QuadraticNumber(Rational(ar.t, 2), Rational(index, 2));
    ar.cls.clear();
    ar.principal = -1;
    ar.enumerate_classes();
  }
  const CycleUnit unit = cycle_unit(ar);
  if (unit.k == 1) return unit;
  const QuadraticContext& kc = ar.kc;
  for (auto& cr : ar.cls) {
    const size_t r = cr.digit.size();
    QuadraticNumber lam = cr.scal[0];
    for (size_t l = 0; l < r; ++l)
      lam = mul(kc, lam, inv(kc, sub(QuadraticNumber(cr.digit[l]), cr.tau[l])));
    for (int m = 1; m < unit.k; ++m)
      for (size_t l = 0; l < r; ++l) {
        cr.cycle.push_back(cr.cycle[l]);
        cr.tau.push_back(cr.tau[l]);
        cr.digit.push_back(cr.digit[l]);
        cr.scal.push_back(lam);
        lam = mul(kc, lam, inv(kc, sub(QuadraticNumber(cr.digit[l]), cr.tau[l])));
      }
    contract(cr.digit.size() == r * (size_t)unit.k && equal(lam, ar.eps),
             "the stored cycle closes at epsilon", sic::ErrorKind::construction);
  }
  return unit;
}

long degree(const Arithmetic& ar, int k) {
  const long points = (long)ar.h() * ar.d * ar.d;
  contract(k >= 1 && points % (2L * k) == 0, "degree h d^2 / (2k) is an integer",
           sic::ErrorKind::construction);
  return points / (2L * k);
}

}  // namespace sic::exact::reduced_order
