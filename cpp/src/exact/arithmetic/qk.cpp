// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/arithmetic/qk.hpp"

#include <cstdlib>

#include "sic/error.hpp"
#include "sic/support/flint_raii.hpp"

namespace sic::exact {
Rational::Rational() { fmpq_init(v); }

Rational::Rational(long n, long d) {
  fmpq_init(v);
  fmpq_set_si(v, n, d);
}

Rational::Rational(const Rational& o) {
  fmpq_init(v);
  fmpq_set(v, o.v);
}

Rational& Rational::operator=(const Rational& o) {
  fmpq_set(v, o.v);
  return *this;
}

Rational::~Rational() { fmpq_clear(v); }

Rational operator+(const Rational& x, const Rational& y) {
  Rational r;
  fmpq_add(r.v, x.v, y.v);
  return r;
}

Rational operator-(const Rational& x, const Rational& y) {
  Rational r;
  fmpq_sub(r.v, x.v, y.v);
  return r;
}

Rational operator*(const Rational& x, const Rational& y) {
  Rational r;
  fmpq_mul(r.v, x.v, y.v);
  return r;
}

Rational operator/(const Rational& x, const Rational& y) {
  Rational r;
  fmpq_div(r.v, x.v, y.v);
  return r;
}

Rational Rational::operator-() const {
  Rational r;
  fmpq_neg(r.v, v);
  return r;
}

int Rational::sgn() const { return fmpq_sgn(v); }

bool Rational::is_zero() const { return fmpq_is_zero(v); }

bool Rational::operator==(const Rational& o) const { return fmpq_equal(v, o.v); }

std::string Rational::str() const {
  sic::support::flint::String s(fmpq_get_str(nullptr, 10, v));
  std::string r(s.get());
  return r;
}

QuadraticNumber add(const QuadraticNumber& x, const QuadraticNumber& y) {
  return QuadraticNumber(x.a + y.a, x.b + y.b);
}

QuadraticNumber sub(const QuadraticNumber& x, const QuadraticNumber& y) {
  return QuadraticNumber(x.a - y.a, x.b - y.b);
}

QuadraticNumber mul(const QuadraticContext& c, const QuadraticNumber& x, const QuadraticNumber& y) {
  return QuadraticNumber(x.a * y.a + x.b * y.b * Rational(c.Delta), x.a * y.b + x.b * y.a);
}

QuadraticNumber conj(const QuadraticNumber& x) { return QuadraticNumber(x.a, -x.b); }

QuadraticNumber inv(const QuadraticContext& c, const QuadraticNumber& x) {
  Rational n = norm(c, x);
  if (n.is_zero()) throw std::runtime_error("inverse of zero");
  return QuadraticNumber(x.a / n, (-x.b) / n);
}

QuadraticNumber scal(const Rational& q, const QuadraticNumber& x) {
  return QuadraticNumber(q * x.a, q * x.b);
}

int sign1(const QuadraticContext& c, const QuadraticNumber& x) {
  int sa = x.a.sgn(), sb = x.b.sgn();
  if (sb == 0) return sa;
  if (sa == 0) return sb;
  if (sa == sb) return sa;
  // compare a^2 with b^2 Delta
  Rational lhs = x.a * x.a, rhs = x.b * x.b * Rational(c.Delta);
  int cmp = fmpq_cmp(lhs.v, rhs.v);
  if (cmp == 0) return 0;
  return cmp > 0 ? sa : sb;
}

void to_arb(arb_t r, const QuadraticContext& c, const QuadraticNumber& x, int emb, slong prec) {
  arb_t s, t;
  arb_init(s);
  arb_init(t);
  arb_sqrt_ui(s, (ulong)c.Delta, prec);
  if (emb == 2) arb_neg(s, s);
  arb_set_fmpq(t, x.b.v, prec);
  arb_mul(s, s, t, prec);
  arb_set_fmpq(t, x.a.v, prec);
  arb_add(r, s, t, prec);
  arb_clear(s);
  arb_clear(t);
}

long ceil1(const QuadraticContext& c, const QuadraticNumber& x) {
  sic::support::flint::Arb r;
  slong prec = 256;
  for (;;) {
    to_arb(r, c, x, 1, prec);
#ifdef SIC_ENABLE_TEST_HOOKS
    if (std::getenv("SIC_TEST_CEIL1_FORCE_PRECISION_REFUSAL")) arb_add_error_2exp_si(r, 0);
#endif
    fmpz_t lo, hi;
    fmpz_init(lo);
    fmpz_init(hi);
    arf_t f;
    arf_init(f);
    arb_get_lbound_arf(f, r, prec);
    arf_get_fmpz(lo, f, ARF_RND_CEIL);
    arb_get_ubound_arf(f, r, prec);
    arf_get_fmpz(hi, f, ARF_RND_CEIL);
    bool ok = fmpz_equal(lo, hi);
    long res = fmpz_get_si(lo);
    arf_clear(f);
    fmpz_clear(lo);
    fmpz_clear(hi);
    if (ok) {
      // exact check: if x is an integer, ceil = x; our x are irrational or rational
      return res;
    }
    prec *= 2;
    if (prec > 1 << 16) throw sic::Error(sic::ErrorKind::precision, "ceil1: precision");
  }
}

Lattice lattice_from_generators(const QuadraticContext& c,
                                const std::vector<QuadraticNumber>& gens) {
  (void)c;
  // common denominator
  fmpz_t den;
  fmpz_init_set_ui(den, 1);
  for (const QuadraticNumber& g : gens) {
    fmpz_lcm(den, den, fmpq_denref(g.a.v));
    fmpz_lcm(den, den, fmpq_denref(g.b.v));
  }
  sic::support::flint::FmpzMat A(gens.size(), 2), H(gens.size(), 2);
  for (size_t i = 0; i < gens.size(); ++i) {
    fmpq_t t;
    fmpq_init(t);
    fmpq_mul_fmpz(t, gens[i].a.v, den);
    fmpz_set(fmpz_mat_entry(A, i, 0), fmpq_numref(t));
    fmpq_mul_fmpz(t, gens[i].b.v, den);
    fmpz_set(fmpz_mat_entry(A, i, 1), fmpq_numref(t));
    fmpq_clear(t);
  }
  fmpz_mat_hnf(H, A);
  Lattice L;
  // first two rows of H form the basis (rank 2 assumed)
  for (int i = 0; i < 2; ++i) {
    Rational a, b;
    fmpq_set_fmpz_frac(a.v, fmpz_mat_entry(H, i, 0), den);
    fmpq_set_fmpz_frac(b.v, fmpz_mat_entry(H, i, 1), den);
    (i == 0 ? L.w1 : L.w2) = QuadraticNumber(a, b);
  }
  fmpz_clear(den);
  return L;
}

bool lattice_equal(const QuadraticContext& c, const Lattice& X, const Lattice& Y) {
  Lattice a = lattice_from_generators(c, {X.w1, X.w2});
  Lattice b = lattice_from_generators(c, {Y.w1, Y.w2});
  return equal(a.w1, b.w1) && equal(a.w2, b.w2);
}

}  // namespace sic::exact

namespace sic::exact {
QuadraticNumber::QuadraticNumber() {}

QuadraticNumber::QuadraticNumber(long x) : a(x), b(0) {}

QuadraticNumber::QuadraticNumber(const Rational& x, const Rational& y) : a(x), b(y) {}

Lattice lattice_mul(const QuadraticContext& c, const Lattice& X, const Lattice& Y) {
  return lattice_from_generators(
      c, {mul(c, X.w1, Y.w1), mul(c, X.w1, Y.w2), mul(c, X.w2, Y.w1), mul(c, X.w2, Y.w2)});
}

Lattice lattice_scale(const QuadraticContext& c, const QuadraticNumber& s, const Lattice& X) {
  return lattice_from_generators(c, {mul(c, s, X.w1), mul(c, s, X.w2)});
}

}  // namespace sic::exact
