// SPDX-License-Identifier: AGPL-3.0-or-later
// Exact arithmetic in K = Q(sqrt(Delta)) and rank-2 lattices in K.
// Elements are a + b*sqrt(Delta) with a, b rational (FLINT fmpq).
#pragma once
#include <flint/arb.h>
#include <flint/fmpq.h>
#include <flint/fmpz.h>
#include <flint/fmpz_mat.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sic::exact {

struct Rational {  // RAII rational
  fmpq_t v;
  Rational();
  Rational(long n, long d = 1);
  Rational(const Rational& o);
  Rational& operator=(const Rational& o);
  ~Rational();
  friend Rational operator+(const Rational& x, const Rational& y);
  friend Rational operator-(const Rational& x, const Rational& y);
  friend Rational operator*(const Rational& x, const Rational& y);
  friend Rational operator/(const Rational& x, const Rational& y);
  Rational operator-() const;
  int sgn() const;
  bool is_zero() const;
  bool operator==(const Rational& o) const;
  std::string str() const;
};

struct QuadraticContext {
  long Delta;  // discriminant (positive nonsquare)
};

struct QuadraticNumber {  // a + b sqrt(Delta)
  Rational a, b;
  QuadraticNumber();
  QuadraticNumber(long x);
  QuadraticNumber(const Rational& x, const Rational& y);
};

QuadraticNumber add(const QuadraticNumber& x, const QuadraticNumber& y);
QuadraticNumber sub(const QuadraticNumber& x, const QuadraticNumber& y);
QuadraticNumber mul(const QuadraticContext& c, const QuadraticNumber& x, const QuadraticNumber& y);
QuadraticNumber conj(const QuadraticNumber& x);
inline Rational norm(const QuadraticContext& c, const QuadraticNumber& x) {
  return x.a * x.a - x.b * x.b * Rational(c.Delta);
}
QuadraticNumber inv(const QuadraticContext& c, const QuadraticNumber& x);
inline QuadraticNumber divk(const QuadraticContext& c, const QuadraticNumber& x,
                            const QuadraticNumber& y) {
  return mul(c, x, inv(c, y));
}
QuadraticNumber scal(const Rational& q, const QuadraticNumber& x);
inline bool equal(const QuadraticNumber& x, const QuadraticNumber& y) {
  return x.a == y.a && x.b == y.b;
}

// sign of a + b sqrt(Delta) (embedding 1) and of a - b sqrt(Delta) (embedding 2)
int sign1(const QuadraticContext& c, const QuadraticNumber& x);
inline int sign2(const QuadraticContext& c, const QuadraticNumber& x) { return sign1(c, conj(x)); }
inline bool totally_positive(const QuadraticContext& c, const QuadraticNumber& x) {
  return sign1(c, x) > 0 && sign2(c, x) > 0;
}

// numerical value at embedding 1 (sqrt positive) or 2
void to_arb(arb_t r, const QuadraticContext& c, const QuadraticNumber& x, int emb, slong prec);

// ceil of x at embedding 1, exactly
long ceil1(const QuadraticContext& c, const QuadraticNumber& x);

// A lattice in K is given by a Z-basis (w1, w2).
struct Lattice {
  QuadraticNumber w1, w2;
};

// Coordinates of x in the Q-basis (1, sqrt(Delta)): (a, b).
// Hermite normal form of a lattice spanned by generators, returned as a basis.
Lattice lattice_from_generators(const QuadraticContext& c,
                                const std::vector<QuadraticNumber>& gens);

Lattice lattice_mul(const QuadraticContext& c, const Lattice& X, const Lattice& Y);
Lattice lattice_scale(const QuadraticContext& c, const QuadraticNumber& s, const Lattice& X);
bool lattice_equal(const QuadraticContext& c, const Lattice& X, const Lattice& Y);

}  // namespace sic::exact
