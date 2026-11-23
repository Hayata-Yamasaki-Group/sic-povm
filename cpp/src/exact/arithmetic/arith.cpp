// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/arithmetic/arith.hpp"

#include <algorithm>
#include <numeric>
#include <tuple>

#include "sic/error.hpp"

namespace sic::exact {
Arithmetic::Arithmetic(int s_, bool minimal, DimensionPolicy policy) : s(s_) {
  if (s < 2) throw std::runtime_error("quadratic construction needs exponent at least 2");
  d = policy.checked(static_cast<unsigned>(s));
  t = d - 1;
  Delta = t * t - 4;
  original_Delta = Delta;
  if (minimal) {
    const auto order = maximal_quadratic_order(Delta);
    order_index = order.conductor;
    Delta = order.discriminant;
  }
  kc.Delta = Delta;
  eps = QuadraticNumber(Rational(t, 2), Rational(order_index, 2));
  enumerate_classes();
}

QuadraticNumber Arithmetic::tau_of(const Form& f) const {
  return QuadraticNumber(Rational(-f.B, 2 * f.A), Rational(1, 2 * f.A));
}

void Arithmetic::enumerate_classes() {
  std::vector<Form> red;
  // For a reduced form with A,C>0 and A+B+C<0, put u=A-C, v=A+C,
  // k=-B-v. Then k>=1, v>|u|, and Delta=u^2+2kv+k^2. Thus
  // |u|+k<sqrt(Delta). Conversely, each (u,k) in that triangular range
  // gives at most one form: v=(Delta-u^2-k^2)/(2k), when this is integral
  // and has v>|u| with v congruent to u mod 2. This scans O(Delta)
  // candidates instead of the O(Delta^(3/2)) A,|B| rectangle.
  long sq = 0;
  while ((sq + 1) * (sq + 1) <= Delta) ++sq;
  const long maxsum = sq * sq == Delta ? sq - 1 : sq;
  for (long u = -maxsum; u <= maxsum; ++u) {
    const long au = std::labs(u);
    for (long k = 1; k <= maxsum - au; ++k) {
      const long numerator = Delta - u * u - k * k;
      if (numerator % (2 * k) != 0) continue;
      const long v = numerator / (2 * k);
      if (v <= au || (v - u) % 2 != 0) continue;
      const long A = (v + u) / 2;
      const long C = (v - u) / 2;
      const long B = -(v + k);
      if (std::gcd(std::gcd(A, -B), C) != 1) continue;
      red.push_back({A, B, C});
    }
  }
  std::sort(red.begin(), red.end(), [](const Form& x, const Form& y) {
    return std::tuple{x.A, -x.B, x.C} < std::tuple{y.A, -y.B, y.C};
  });
  // cycles under (A,B,C) -> (Ab^2+Bb+C, -2Ab-B, A), b = ceil(tau)
  std::map<std::tuple<long, long, long>, sic::support::Index> idx;
  for (size_t i = 0; i < red.size(); ++i)
    idx[{red[i].A, red[i].B, red[i].C}] = sic::support::checked_index(i);
  sic::support::admit_index_count(red.size());
  std::vector<char> seen(red.size(), 0);
  for (size_t i = 0; i < red.size(); ++i) {
    if (seen[i]) continue;
    ClassRep cr;
    Form f = red[i];
    QuadraticNumber lam(1);
    for (;;) {
      auto it = idx.find({f.A, f.B, f.C});
      if (it == idx.end())
        throw sic::Error(sic::ErrorKind::construction, "cycle left the reduced set");
      if (seen[it->second]) break;
      seen[it->second] = 1;
      QuadraticNumber tau = tau_of(f);
      long b = ceil1(kc, tau);
      cr.cycle.push_back(f);
      cr.tau.push_back(tau);
      cr.digit.push_back(b);
      cr.scal.push_back(lam);
      // J_{next} = (b - tau)^{-1} J_tau
      lam = mul(kc, lam, inv(kc, sub(QuadraticNumber(b), tau)));
      f = Form{f.A * b * b + f.B * b + f.C, -2 * f.A * b - f.B, f.A};
    }
    cls.push_back(cr);
  }
  // Principal lattice O: its reduced generator has A=1 and the smallest
  // positive odd -B greater than sqrt(Delta).
  long pb = sq + 1;
  if (pb % 2 == 0) ++pb;
  for (size_t c = 0; c < cls.size(); ++c)
    for (auto& f : cls[c].cycle)
      if (f.A == 1 && f.B == -pb) principal = sic::support::checked_index(c);
  if (principal < 0) throw sic::Error(sic::ErrorKind::construction, "principal class not found");
  // Put the principal class first (tau=epsilon in the standard order).
  if (principal != 0) std::swap(cls[0], cls[principal]);
  principal = 0;
}

Lattice Arithmetic::class_involution(sic::support::Index c) const {
  return Lattice{cls[c].tau[0], QuadraticNumber(1)};
}

LatticeIdentification Arithmetic::identify(const Lattice& L) const {
  // 1. totally positive primitive element omega2 and oriented basis.
  // Put L in Hermite form: v1 = a + b sqrt(D), v2 = c sqrt(D) (rational a, b, c, a, c > 0).
  Lattice H = lattice_from_generators(kc, {L.w1, L.w2});
  QuadraticNumber v1 = H.w1, v2 = H.w2;
  if (!v2.a.is_zero() || v1.a.is_zero())
    throw sic::Error(sic::ErrorKind::construction, "identify: unexpected Hermite form");
  QuadraticNumber w2, w1;
  bool found = false;
  for (long m = 1; m < 1000000 && !found; ++m) {
    // n near -m b / c
    Rational q = (-Rational(m)) * v1.b / v2.b;
    fmpz_t fl;
    fmpz_init(fl);
    fmpz_fdiv_q(fl, fmpq_numref(q.v), fmpq_denref(q.v));
    long n0 = fmpz_get_si(fl);
    fmpz_clear(fl);
    for (long n = n0 - 1; n <= n0 + 2 && !found; ++n) {
      if (std::gcd(m, std::labs(n)) != 1) continue;
      QuadraticNumber x = add(scal(Rational(m), v1), scal(Rational(n), v2));
      if (!totally_positive(kc, x)) continue;
      long g, p, qq;
      ext_gcd(n, -m, g, p, qq);  // p*n - qq*m = g
      if (g < 0) {
        p = -p;
        qq = -qq;
      }
      // basis change: (w1, w2) = (p v1 + qq v2, m v1 + n v2) has determinant p n - qq m = 1
      w2 = x;
      w1 = add(scal(Rational(p), v1), scal(Rational(qq), v2));
      found = true;
    }
  }
  if (!found)
    throw sic::Error(sic::ErrorKind::construction, "identify: no totally positive element");
  QuadraticNumber tau = divk(kc, w1, w2);
  if (tau.b.sgn() < 0) {
    tau = QuadraticNumber(-tau.a, -tau.b);
  }  // orientation tau > tau'
  // 2. reduce: L = w2 * J_tau ; track P with J_tau_final = P^{-1} J_tau_start
  QuadraticNumber P(1);
  for (int it = 0; it < 100000; ++it) {
    arb_t v;
    arb_init(v);
    to_arb(v, kc, tau, 2, 256);
    arf_t f;
    arf_init(f);
    fmpz_t fl;
    fmpz_init(fl);
    arf_get_fmpz(fl, arb_midref(v), ARF_RND_FLOOR);
    long k = fmpz_get_si(fl);
    fmpz_clear(fl);
    arf_clear(f);
    arb_clear(v);
    tau = sub(tau, QuadraticNumber(k));  // tau' in [0,1)
    // reduced?
    QuadraticNumber tm1 = sub(tau, QuadraticNumber(1));
    if (sign1(kc, tm1) > 0 && sign2(kc, tau) > 0 && sign2(kc, tm1) < 0) {
      // locate in cycles
      for (sic::support::Index c = 0; c < h(); ++c)
        for (size_t l = 0; l < cls[c].tau.size(); ++l)
          if (equal(cls[c].tau[l], tau)) {
            QuadraticNumber mu = mul(kc, mul(kc, w2, P), cls[c].scal[l]);
            if (!totally_positive(kc, mu))
              throw sic::Error(sic::ErrorKind::construction, "identify: mu not totally positive");
            return {c, mu};
          }
      // reduced but not found: step along the minus continued fraction
    }
    long b = ceil1(kc, tau);
    QuadraticNumber bm = sub(QuadraticNumber(b), tau);
    P = mul(kc, P, bm);
    tau = inv(kc, bm);
  }
  throw sic::Error(sic::ErrorKind::construction, "identify: reduction did not terminate");
}

void Arithmetic::ext_gcd(long a, long b, long& g, long& x, long& y) {
  if (b == 0) {
    g = a;
    x = 1;
    y = 0;
    return;
  }
  long g1, x1, y1;
  ext_gcd(b, a % b, g1, x1, y1);
  g = g1;
  x = y1;
  y = x1 - (a / b) * y1;
}

BasisCoordinates Arithmetic::coords(sic::support::Index c, const QuadraticNumber& x) const {
  const QuadraticNumber& tc = cls[c].tau[0];
  Rational u = x.b / tc.b;
  Rational v = x.a - u * tc.a;
  return {u, v};
}

long resid(const Rational& q, long d) {
  fmpq_t t;
  fmpq_init(t);
  fmpq_mul_si(t, q.v, d);
  if (!fmpz_is_one(fmpq_denref(t))) {
    fmpq_clear(t);
    throw sic::Error(sic::ErrorKind::construction, "resid: not in (1/d)Z");
  }
  long r = fmpz_fdiv_ui(fmpq_numref(t), d);
  fmpq_clear(t);
  return r;
}

}  // namespace sic::exact
