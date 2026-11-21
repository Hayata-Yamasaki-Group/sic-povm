// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/core/clifford.hpp"

namespace sic::core::clifford {
Int mod(Int x, Int m) {
  const Int r = x % m;
  return r < 0 ? r + m : r;
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
Int mulmod(Int x, Int y, Int m) {
  return mod(x, m) * mod(y, m) % m;  // moduli here are at most 2^25 (dbar), so no overflow
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
Int inverse_mod(Int x, Int m) {
  Int r0 = m, r1 = mod(x, m), s0 = 0, s1 = 1;
  while (r1 != 0) {
    const Int q = r0 / r1;
    std::tie(r0, r1) = std::make_pair(r1, r0 - q * r1);
    std::tie(s0, s1) = std::make_pair(s1, s0 - q * s1);
  }
  if (r0 != 1) throw sic::Error(ErrorKind::invalid_argument, "inverse_mod: not a unit");
  return mod(s0, m);
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
Int dbar_of(Int d) { return d % 2 == 0 ? 2 * d : d; }
}  // namespace sic::core::clifford

namespace sic::core::clifford {
Mat2 reduce(const Mat2& f, Int m) { return {mod(f.a, m), mod(f.b, m), mod(f.c, m), mod(f.e, m)}; }
}  // namespace sic::core::clifford

namespace sic::core::clifford {
Mat2 mul(const Mat2& x, const Mat2& y, Int m) {
  return {mod(mulmod(x.a, y.a, m) + mulmod(x.b, y.c, m), m),
          mod(mulmod(x.a, y.b, m) + mulmod(x.b, y.e, m), m),
          mod(mulmod(x.c, y.a, m) + mulmod(x.e, y.c, m), m),
          mod(mulmod(x.c, y.b, m) + mulmod(x.e, y.e, m), m)};
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
Int det(const Mat2& f, Int m) { return mod(mulmod(f.a, f.e, m) - mulmod(f.b, f.c, m), m); }
}  // namespace sic::core::clifford

namespace sic::core::clifford {
Mat2 inverse(const Mat2& f, Int m) {
  const Int di = inverse_mod(det(f, m), m);
  return {mulmod(di, f.e, m), mulmod(di, -f.b, m), mulmod(di, -f.c, m), mulmod(di, f.a, m)};
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
Int trace(const Mat2& f, Int m) { return mod(f.a + f.e, m); }
}  // namespace sic::core::clifford

namespace sic::core::clifford {
std::array<Int, 2> apply(const Mat2& f, Int p1, Int p2, Int m) {
  return {mod(mulmod(f.a, p1, m) + mulmod(f.b, p2, m), m),
          mod(mulmod(f.c, p1, m) + mulmod(f.e, p2, m), m)};
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
Int order(const Mat2& f, Int m, Int limit) {
  Mat2 x = reduce(f, m);
  const Mat2 id{1, 0, 0, 1};
  for (Int k = 1; k <= limit; ++k) {
    if (reduce(x, m) == reduce(id, m)) return k;
    x = mul(x, f, m);
  }
  return 0;
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
Mat2 canonical_lift(const Mat2& f_mod_d, Int d, Int sign) {
  if (d < 1 || d > (Int{1} << 30) || (sign != -1 && sign != 1))
    throw sic::Error(ErrorKind::invalid_argument, "canonical_lift: invalid dimension or sign");
  const Int db = dbar_of(d);
  const Mat2 f = reduce(f_mod_d, d);
  if (det(f, d) != mod(sign, d))
    throw sic::Error(ErrorKind::invalid_argument, "canonical_lift: determinant mismatch");
  if (db == d) return f;
  bool found = false;
  Mat2 best;
  for (int bits = 0; bits < 16; ++bits) {
    const Mat2 g{f.a + ((bits >> 3) & 1) * d, f.b + ((bits >> 2) & 1) * d,
                 f.c + ((bits >> 1) & 1) * d, f.e + (bits & 1) * d};
    if (det(g, db) != mod(sign, db)) continue;
    const auto key = std::array<Int, 4>{g.a, g.b, g.c, g.e};
    if (!found || key < std::array<Int, 4>{best.a, best.b, best.c, best.e}) best = g;
    found = true;
  }
  if (!found) throw std::logic_error("canonical_lift: no lift (impossible for det = +-1)");
  return best;
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
Int gcd(Int x, Int y) {
  while (y != 0) std::tie(x, y) = std::make_pair(y, x % y);
  return x < 0 ? -x : x;
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
Factorization factorize(const Mat2& f_in, Int d) {
  const Int db = dbar_of(d);
  const Mat2 f = reduce(f_in, db);
  const Int dt = det(f, db);
  Factorization out;
  Mat2 g = f;
  if (dt != 1 && dt == mod(-1, db)) {
    out.conjugate = true;
    g = mul(f, kJ, db);
  } else if (dt != 1) {
    throw sic::Error(ErrorKind::invalid_argument, "factorize: det F must be +1 or -1 mod dbar");
  }
  if (gcd(g.b, db) == 1) {
    out.factors = {g};
    return out;
  }
  // g = F1 F2, F1 = [[0, -1], [1, x]], F2 = [[c + x a, e + x b], [-a, -b]], e + x b a unit:
  // U_g = U_F1 U_F2 (Appleby2005symmetric).
  Int x = 0;
  while (x < db && gcd(mod(g.e + mulmod(x, g.b, db), db), db) != 1) ++x;
  if (x == db) throw std::logic_error("factorize: no decomposition");
  const Mat2 f1{0, db - 1, 1, x};
  const Mat2 f2{mod(g.c + mulmod(x, g.a, db), db), mod(g.e + mulmod(x, g.b, db), db), mod(-g.a, db),
                mod(-g.b, db)};
  out.factors = {f2, f1};
  return out;
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
Int CliffordAction::checked_dimension(Int d, uint64_t maximum_dimension) {
  if (d < 1 || static_cast<uint64_t>(d) > maximum_dimension || d > (Int{1} << 30))
    throw sic::Error(ErrorKind::invalid_argument, "CliffordAction: dimension exceeds policy");
  return d;
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
CliffordAction::CliffordAction(Int d, uint64_t maximum_dimension)
    : d_(checked_dimension(d, maximum_dimension)),
      db_(dbar_of(d_)),
      dft_(static_cast<std::size_t>(d_)) {
  roots_.resize(static_cast<std::size_t>(2 * d));
  for (Int m = 0; m < 2 * d; ++m) {
    const Real angle = std::numbers::pi_v<Real> * static_cast<Real>(m) / static_cast<Real>(d);
    roots_[static_cast<std::size_t>(m)] = Complex(std::cos(angle), std::sin(angle));
  }
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
void CliffordAction::apply(const Mat2& f_in, const Vector& v, Vector& out) const {
  if (static_cast<Int>(v.size()) != d_)
    throw sic::Error(ErrorKind::invalid_argument, "CliffordAction: size mismatch");
  const Factorization fz = factorize(f_in, d_);
  Vector w(v), next;
  if (fz.conjugate)
    for (Complex& z : w) z = std::conj(z);
  for (const Mat2& g : fz.factors) {
    unit_b(g, w, next);
    w.swap(next);
  }
  out.swap(w);
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
void CliffordAction::unit_b(const Mat2& f, const Vector& v, Vector& out) const {
  const Int bi = inverse_mod(f.b, db_);
  const Int ka = mulmod(bi, f.a, 2 * d_), ke = mulmod(bi, f.e, 2 * d_);
  Vector w(static_cast<std::size_t>(d_));
  for (Int j = 0; j < d_; ++j)
    w[static_cast<std::size_t>(j)] =
        tau_pow(mulmod(ka, mulmod(j, j, 2 * d_), 2 * d_)) * v[static_cast<std::size_t>(j)];
  dft_.transform(w, -1);
  out.resize(static_cast<std::size_t>(d_));
  const Real scale = 1 / std::sqrt(static_cast<Real>(d_));
  const Int bd = mod(bi, d_);
  for (Int u = 0; u < d_; ++u)
    out[static_cast<std::size_t>(u)] = scale * tau_pow(mulmod(ke, mulmod(u, u, 2 * d_), 2 * d_)) *
                                       w[static_cast<std::size_t>(mulmod(bd, u, d_))];
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
bool CliffordElement::antiunitary() const { return sign == -1; }
}  // namespace sic::core::clifford

namespace sic::core::clifford {
Vector apply_element(const CliffordElement& el, const Vector& phi) {
  const Int d = static_cast<Int>(phi.size());
  CliffordAction action(d);
  Vector u, out(phi.size());
  action.apply(el.F, phi, u);
  SicFrame(u, false).displace(el.chi1, el.chi2, u.data(), out.data());
  return out;
}
}  // namespace sic::core::clifford

namespace sic::core::clifford {
std::string to_string(const Mat2& f) {
  return "[[" + std::to_string(f.a) + "," + std::to_string(f.b) + "],[" + std::to_string(f.c) +
         "," + std::to_string(f.e) + "]]";
}
}  // namespace sic::core::clifford
