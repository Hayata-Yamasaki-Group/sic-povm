// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/exact/tower.hpp"

#include "sic/sicx/verify.hpp"
#include "sic/support/flint_raii.hpp"
#include "sicx/codec/block.hpp"
namespace sic::sicx::exact {

QuadraticElement::QuadraticElement() {
  fmpq_init(a);
  fmpq_init(b);
}

QuadraticElement::~QuadraticElement() {
  fmpq_clear(a);
  fmpq_clear(b);
}

QuadraticElement::QuadraticElement(const QuadraticElement& o) {
  fmpq_init(a);
  fmpq_init(b);
  fmpq_set(a, o.a);
  fmpq_set(b, o.b);
}

QuadraticElement::QuadraticElement(QuadraticElement&& o) noexcept {
  fmpq_init(a);
  fmpq_init(b);
  fmpq_swap(a, o.a);
  fmpq_swap(b, o.b);
}

QuadraticElement& QuadraticElement::operator=(const QuadraticElement& o) {
  if (this != &o) {
    fmpq_set(a, o.a);
    fmpq_set(b, o.b);
  }
  return *this;
}

QuadraticElement& QuadraticElement::operator=(QuadraticElement&& o) noexcept {
  fmpq_swap(a, o.a);
  fmpq_swap(b, o.b);
  return *this;
}

bool QuadraticElement::zero() const { return fmpq_is_zero(a) && fmpq_is_zero(b); }

bool QuadraticElement::operator==(const QuadraticElement& o) const {
  return fmpq_equal(a, o.a) && fmpq_equal(b, o.b);
}

bool block_zero(const QuadraticElement* x, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (!x[i].zero()) return false;
  return true;
}

Tower::Tower(const Meta& meta) : t(meta.levels()), p(meta.degrees), N(meta.width), D0(meta.D0) {
  fmpq_init(m);
  if (D0 == 1) {
    s = 0;
    fmpq_zero(m);
  } else if (((D0 % 4) + 4) % 4 == 1) {
    s = 1;
    fmpq_set_si(m, (D0 - 1) / 4, 1);
  } else {
    s = 0;
    fmpq_set_si(m, D0, 1);
  }
  f.resize(t);
  dual.resize(t);
}

Tower::~Tower() { fmpq_clear(m); }

void Tower::kmul_add(QuadraticElement& r, const QuadraticElement& x,
                     const QuadraticElement& y) const {
  if (x.zero() || y.zero()) return;
  fmpq_t ac, bd, cross;
  fmpq_init(ac);
  fmpq_init(bd);
  fmpq_init(cross);
  fmpq_mul(ac, x.a, y.a);
  fmpq_mul(bd, x.b, y.b);
  fmpq_mul(cross, x.a, y.b);
  fmpq_addmul(cross, x.b, y.a);
  // (ac + cross*omega + bd*omega^2), omega^2 = s*omega + m
  fmpq_addmul(ac, bd, m);
  if (s) fmpq_add(cross, cross, bd);
  fmpq_add(r.a, r.a, ac);
  fmpq_add(r.b, r.b, cross);
  fmpq_clear(ac);
  fmpq_clear(bd);
  fmpq_clear(cross);
}

QuadraticElement Tower::kinv(const QuadraticElement& x) const {
  // (a + b w)(a + b w') = a^2 + s a b - m b^2 with w' = s - w
  fmpq_t norm, t0;
  fmpq_init(norm);
  fmpq_init(t0);
  fmpq_mul(norm, x.a, x.a);
  if (s) {
    fmpq_mul(t0, x.a, x.b);
    fmpq_add(norm, norm, t0);
  }
  fmpq_mul(t0, x.b, x.b);
  fmpq_mul(t0, t0, m);
  fmpq_sub(norm, norm, t0);
  if (fmpq_is_zero(norm)) {
    fmpq_clear(norm);
    fmpq_clear(t0);
    throw std::logic_error(std::string("SICX: ") + "division by zero in K");
  }
  QuadraticElement r;
  // inverse = (a + b*s - b*w)/norm
  fmpq_set(r.a, x.a);
  if (s) fmpq_add(r.a, r.a, x.b);
  fmpq_neg(r.b, x.b);
  fmpq_div(r.a, r.a, norm);
  fmpq_div(r.b, r.b, norm);
  fmpq_clear(norm);
  fmpq_clear(t0);
  return r;
}

Elt Tower::zero(unsigned n) const { return Elt(N[n]); }

Elt Tower::one(unsigned n) const {
  Elt x(N[n]);
  fmpq_one(x[0].a);
  return x;
}

void Tower::add_to(QuadraticElement* out, const QuadraticElement* x, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    fmpq_add(out[i].a, out[i].a, x[i].a);
    fmpq_add(out[i].b, out[i].b, x[i].b);
  }
}

void Tower::sub_from(QuadraticElement* out, const QuadraticElement* x, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    fmpq_sub(out[i].a, out[i].a, x[i].a);
    fmpq_sub(out[i].b, out[i].b, x[i].b);
  }
}

void Tower::mul_add(QuadraticElement* out, const QuadraticElement* a, const QuadraticElement* b,
                    unsigned n) const {
  if (n == 0) {
    kmul_add(out[0], a[0], b[0]);
    return;
  }
  const unsigned pn = p[n - 1];
  const size_t M = N[n - 1];
  std::vector<char> za(pn), zb(pn);
  for (unsigned j = 0; j < pn; ++j) {
    za[j] = block_zero(a + j * M, M);
    zb[j] = block_zero(b + j * M, M);
  }
  Elt C((2 * pn - 1) * M);
  for (unsigned j = 0; j < pn; ++j) {
    if (za[j]) continue;
    for (unsigned k = 0; k < pn; ++k)
      if (!zb[k]) mul_add(C.data() + (j + k) * M, a + j * M, b + k * M, n - 1);
  }
  for (unsigned mm = 2 * pn - 2; mm >= pn; --mm) {
    if (block_zero(C.data() + mm * M, M)) continue;
    for (unsigned k = 0; k < pn; ++k) {
      const auto& fk = f[n - 1][k];
      if (block_zero(fk.data(), M)) continue;
      Elt tmp(M);
      mul_add(tmp.data(), C.data() + mm * M, fk.data(), n - 1);
      sub_from(C.data() + (mm - pn + k) * M, tmp.data(), M);
    }
  }
  add_to(out, C.data(), pn * M);
}

Elt Tower::mul(const Elt& a, const Elt& b, unsigned n) const {
  Elt out(N[n]);
  mul_add(out.data(), a.data(), b.data(), n);
  return out;
}

Elt Tower::inverse(const Elt& x, unsigned n) const {
  if (n == 0) {
    Elt r(1);
    r[0] = kinv(x[0]);
    return r;
  }
  const unsigned pn = p[n - 1];
  const size_t M = N[n - 1];
  if (pn == 2 && block_zero(f[n - 1][1].data(), M)) {
    // (A + B g)(A - B g) = A^2 + f0 B^2 since g^2 = -f0.
    Elt A(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(M)),
        B(x.begin() + static_cast<std::ptrdiff_t>(M), x.end());
    Elt norm = mul(A, A, n - 1);
    mul_add(norm.data(), mul(B, B, n - 1).data(), f[n - 1][0].data(), n - 1);
    const Elt inv = inverse(norm, n - 1);
    Elt out(N[n]);
    mul_add(out.data(), A.data(), inv.data(), n - 1);
    Elt nb(M);
    mul_add(nb.data(), B.data(), inv.data(), n - 1);
    for (size_t i = 0; i < M; ++i) {
      fmpq_neg(out[M + i].a, nb[i].a);
      fmpq_neg(out[M + i].b, nb[i].b);
    }
    return out;
  }
  return inverse_general(x, n);
}

Elt Tower::inverse_general(const Elt& x, unsigned n) const {
  const size_t NN = N[n];
  sic::support::flint::FmpqMat A(static_cast<slong>(2 * NN), static_cast<slong>(2 * NN));
  sic::support::flint::FmpqMat B(static_cast<slong>(2 * NN), 1);
  sic::support::flint::FmpqMat X(static_cast<slong>(2 * NN), 1);
  for (size_t j = 0; j < NN; ++j) {
    for (int u = 0; u < 2; ++u) {
      Elt e(NN);
      if (u == 0)
        fmpq_one(e[j].a);
      else
        fmpq_one(e[j].b);
      const Elt prod = mul(x, e, n);
      for (size_t i = 0; i < NN; ++i) {
        fmpq_set(fmpq_mat_entry(A, static_cast<slong>(2 * i), static_cast<slong>(2 * j + u)),
                 prod[i].a);
        fmpq_set(fmpq_mat_entry(A, static_cast<slong>(2 * i + 1), static_cast<slong>(2 * j + u)),
                 prod[i].b);
      }
    }
  }
  fmpq_one(fmpq_mat_entry(B, 0, 0));
  if (!fmpq_mat_solve(X, A, B)) {
    throw std::logic_error(std::string("SICX: ") + "element is not invertible");
  }
  Elt out(NN);
  for (size_t i = 0; i < NN; ++i) {
    fmpq_set(out[i].a, fmpq_mat_entry(X, static_cast<slong>(2 * i), 0));
    fmpq_set(out[i].b, fmpq_mat_entry(X, static_cast<slong>(2 * i + 1), 0));
  }
  return out;
}

Elt Tower::embed(const Elt& x, unsigned to) const {
  Elt out(N[to]);
  for (size_t i = 0; i < x.size(); ++i) out[i] = x[i];
  return out;
}

Elt Tower::generator(unsigned i) const {
  Elt g(N[i + 1]);
  fmpq_one(g[N[i]].a);
  return g;
}

void Tower::set_level(unsigned i, std::vector<Elt> coefficients) {
  f[i] = std::move(coefficients);
  // dual basis b_k = q_k(g) / f'(g), q_{p-1} = 1, q_j = a_{j+1} + g q_{j+1} (a_p = 1).
  const unsigned pi = p[i];
  const Elt g = generator(i);
  auto a = [&](unsigned k) { return k == pi ? one(i + 1) : embed(f[i][k], i + 1); };
  std::vector<Elt> q(pi);
  q[pi - 1] = one(i + 1);
  for (unsigned j = pi - 1; j-- > 0;) {
    q[j] = mul(g, q[j + 1], i + 1);
    add_to(q[j].data(), a(j + 1).data(), N[i + 1]);
  }
  Elt fp(N[i + 1]);
  Elt gpow = one(i + 1);
  for (unsigned k = 1; k <= pi; ++k) {
    Elt term = mul(a(k), gpow, i + 1);
    QuadraticElement kk;
    fmpq_set_si(kk.a, k, 1);
    for (auto& c : term) {
      QuadraticElement r;
      kmul_add(r, c, kk);
      c = std::move(r);
    }
    add_to(fp.data(), term.data(), N[i + 1]);
    gpow = mul(gpow, g, i + 1);
  }
  const Elt inv = inverse(fp, i + 1);
  dual[i].clear();
  for (unsigned k = 0; k < pi; ++k) dual[i].push_back(mul(q[k], inv, i + 1));
}

Elt Tower::from_leaves(const QuadraticElement* lam, unsigned n) const {
  if (n == 0) {
    Elt r(1);
    r[0] = lam[0];
    return r;
  }
  const unsigned pn = p[n - 1];
  const size_t M = N[n - 1];
  Elt out(N[n]);
  for (unsigned k = 0; k < pn; ++k) {
    if (block_zero(lam + k * M, M)) continue;
    const Elt X = from_leaves(lam + k * M, n - 1);
    for (unsigned j = 0; j < pn; ++j) {
      const QuadraticElement* bj = dual[n - 1][k].data() + j * M;
      if (block_zero(bj, M)) continue;
      mul_add(out.data() + j * M, X.data(), bj, n - 1);
    }
  }
  return out;
}

Elt Tower::apply(const Elt& x, unsigned n, const std::vector<Elt>& img, bool conjugate_k) const {
  if (n == 0) {
    Elt r(1);
    r[0] = x[0];
    if (conjugate_k) {  // omega -> s - omega
      fmpq_t t0;
      fmpq_init(t0);
      fmpq_set(r[0].a, x[0].a);
      if (s) fmpq_add(r[0].a, r[0].a, x[0].b);
      fmpq_neg(r[0].b, x[0].b);
      fmpq_clear(t0);
    }
    return r;
  }
  const unsigned pn = p[n - 1];
  const size_t M = N[n - 1];
  auto lower = [&](unsigned k) {
    return apply(Elt(x.begin() + static_cast<std::ptrdiff_t>(k * M),
                     x.begin() + static_cast<std::ptrdiff_t>((k + 1) * M)),
                 n - 1, img, conjugate_k);
  };
  Elt res = embed(lower(pn - 1), n);
  for (unsigned k = pn - 1; k-- > 0;) {
    res = mul(res, img[n - 1], n);
    add_to(res.data(), embed(lower(k), n).data(), M);
  }
  return res;
}
}  // namespace sic::sicx::exact
