// SPDX-License-Identifier: AGPL-3.0-or-later
#include "algebra.hpp"

#include "../format/canonical_json.hpp"
#include "../format/container.hpp"
#include "../format/integer_codec.hpp"
#include "../format/metadata.hpp"
#include "../runtime/diagnostics.hpp"
namespace norm = sic_check::norm;
namespace sic_check::norm {
QuadraticElement::QuadraticElement() {
  fmpq_init(a);
  fmpq_init(b);
}

QuadraticElement::QuadraticElement(const QuadraticElement& v) : QuadraticElement() {
  fmpq_set(a, v.a);
  fmpq_set(b, v.b);
}

QuadraticElement::QuadraticElement(QuadraticElement&& v) noexcept : QuadraticElement() {
  fmpq_swap(a, v.a);
  fmpq_swap(b, v.b);
}

QuadraticElement& QuadraticElement::operator=(const QuadraticElement& v) {
  fmpq_set(a, v.a);
  fmpq_set(b, v.b);
  return *this;
}

QuadraticElement::~QuadraticElement() {
  fmpq_clear(a);
  fmpq_clear(b);
}

auto QuadraticElement::zero() const -> bool { return fmpq_is_zero(a) && fmpq_is_zero(b); }

bool block_zero(const QuadraticElement* x, size_t n) {
  for (size_t j = 0; j < n; ++j)
    if (!x[j].zero()) return false;
  return true;
}

void acb_from_ke(acb_t out, const QuadraticElement& x, const acb_t omega, slong prec) {
  ComplexBall coefficient;
  acb_set_fmpq(out, x.a, prec);
  acb_set_fmpq(coefficient.v, x.b, prec);
  acb_addmul(out, coefficient.v, omega, prec);
}

Tower::Tower(const Meta& m)
    : t(unsigned(m.degrees.size())),
      s((m.D0 % 4 + 4) % 4 == 1 ? 1 : 0),
      constant(s ? (m.D0 - 1) / 4 : m.D0),
      p(m.degrees),
      f(t),
      dual(t) {
  for (unsigned degree : p) N.push_back(N.back() * degree);
}

auto Tower::product(const QuadraticElement& x, const QuadraticElement& y) const
    -> QuadraticElement {
  QuadraticElement z;
  fmpq_t cross;
  fmpq_init(cross);
  fmpq_mul(cross, x.b, y.b);
  fmpq_mul_si(z.a, cross, constant);
  fmpq_addmul(z.a, x.a, y.a);
  fmpq_mul(z.b, x.a, y.b);
  fmpq_addmul(z.b, x.b, y.a);
  if (s) fmpq_add(z.b, z.b, cross);
  fmpq_clear(cross);
  return z;
}

auto Tower::plus(Elt& x, const Elt& y, bool negative) -> void {
  require(x.size() == y.size(), "algebra addition size");
  for (size_t j = 0; j < x.size(); ++j) {
    if (negative) {
      fmpq_sub(x[j].a, x[j].a, y[j].a);
      fmpq_sub(x[j].b, x[j].b, y[j].b);
    } else {
      fmpq_add(x[j].a, x[j].a, y[j].a);
      fmpq_add(x[j].b, x[j].b, y[j].b);
    }
  }
}

auto Tower::one(unsigned n) const -> Elt {
  Elt x(N[n]);
  fmpq_one(x[0].a);
  return x;
}

auto Tower::generator(unsigned i) const -> Elt {
  Elt x(N[i + 1]);
  fmpq_one(x[N[i]].a);
  return x;
}

auto Tower::multiply(const QuadraticElement* x, const QuadraticElement* y, unsigned n) const
    -> Elt {
  if (!n) return Elt{product(*x, *y)};
  size_t width = N[n - 1];
  unsigned degree = p[n - 1];
  std::vector<Elt> convolution(2 * degree - 1, Elt(width));
  for (unsigned i = 0; i < degree; ++i)
    if (!block_zero(x + i * width, width))
      for (unsigned j = 0; j < degree; ++j)
        if (!block_zero(y + j * width, width))
          plus(convolution[i + j], multiply(x + i * width, y + j * width, n - 1));
  for (unsigned k = 2 * degree - 1; k-- > degree;)
    if (!block_zero(convolution[k].data(), width))
      for (unsigned j = 0; j < degree; ++j)
        if (!block_zero(f[n - 1][j].data(), width))
          plus(convolution[k - degree + j],
               multiply(convolution[k].data(), f[n - 1][j].data(), n - 1), true);
  Elt result(N[n]);
  for (unsigned j = 0; j < degree; ++j)
    std::copy(convolution[j].begin(), convolution[j].end(), result.begin() + j * width);
  return result;
}

auto Tower::mul(const Elt& x, const Elt& y, unsigned n) const -> Elt {
  require(x.size() == N[n] && y.size() == N[n], "algebra multiplication size");
  return multiply(x.data(), y.data(), n);
}

auto Tower::inverse(const Elt& x, unsigned n) const -> Elt {
  require(x.size() == N[n], "inverse size");
  if (!n) {
    QuadraticElement z;
    fmpq_t den, temp;
    fmpq_init(den);
    fmpq_init(temp);
    fmpq_set(temp, x[0].a);
    if (s) fmpq_add(temp, temp, x[0].b);
    fmpq_mul(den, x[0].a, temp);
    fmpq_mul(z.b, x[0].b, x[0].b);
    fmpq_mul_si(z.b, z.b, constant);
    fmpq_sub(den, den, z.b);
    bool ok = !fmpq_is_zero(den);
    if (ok) {
      fmpq_div(z.a, temp, den);
      fmpq_div(z.b, x[0].b, den);
      fmpq_neg(z.b, z.b);
    }
    fmpq_clear(den);
    fmpq_clear(temp);
    require(ok, "noninvertible quadratic scalar");
    return Elt{z};
  }
  if (p[n - 1] == 2) {
    const size_t h = N[n - 1];
    Elt a(x.begin(), x.begin() + h), b(x.begin() + h, x.end());
    // X^2 = -f_0; (a+bX)^-1 = (a-bX)/(a^2+b^2 f_0).
    Elt den = mul(a, a, n - 1);
    plus(den, mul(mul(b, b, n - 1), f[n - 1][0], n - 1));
    Elt inv = inverse(den, n - 1), lo = mul(a, inv, n - 1), hi = mul(b, inv, n - 1);
    lo.resize(N[n]);
    for (size_t j = 0; j < h; ++j) {
      fmpq_neg(hi[j].a, hi[j].a);
      fmpq_neg(hi[j].b, hi[j].b);
      lo[h + j] = hi[j];
    }
    return lo;
  }
  // General fallback: invert the multiplication map over Q. No assumption of
  // irreducibility is needed; a singular map refuses instead of dividing by zero.
  slong dim = slong(2 * N[n]);
  fmpq_mat_t matrix, rhs, solution;
  fmpq_mat_init(matrix, dim, dim);
  fmpq_mat_init(rhs, dim, 1);
  fmpq_mat_init(solution, dim, 1);
  fmpq_one(fmpq_mat_entry(rhs, 0, 0));
  for (slong j = 0; j < dim; ++j) {
    Elt basis(N[n]);
    if (j & 1)
      fmpq_one(basis[size_t(j) / 2].b);
    else
      fmpq_one(basis[size_t(j) / 2].a);
    auto column = mul(x, basis, n);
    for (size_t k = 0; k < N[n]; ++k) {
      fmpq_set(fmpq_mat_entry(matrix, slong(2 * k), j), column[k].a);
      fmpq_set(fmpq_mat_entry(matrix, slong(2 * k + 1), j), column[k].b);
    }
  }
  bool ok = fmpq_mat_solve(solution, matrix, rhs) != 0;
  Elt result(N[n]);
  if (ok)
    for (size_t k = 0; k < N[n]; ++k) {
      fmpq_set(result[k].a, fmpq_mat_entry(solution, slong(2 * k), 0));
      fmpq_set(result[k].b, fmpq_mat_entry(solution, slong(2 * k + 1), 0));
    }
  fmpq_mat_clear(matrix);
  fmpq_mat_clear(rhs);
  fmpq_mat_clear(solution);
  require(ok, "noninvertible tower element");
  return result;
}

auto Tower::from_leaves(const QuadraticElement* lam, unsigned n) const -> Elt {
  if (!n) return Elt{*lam};
  Elt out(N[n]);
  for (unsigned k = 0; k < p[n - 1]; ++k) {
    if (block_zero(lam + k * N[n - 1], N[n - 1])) continue;
    Elt lower = from_leaves(lam + k * N[n - 1], n - 1);
    lower.resize(N[n]);
    plus(out, mul(lower, dual[n - 1][k], n));
  }
  return out;
}

auto Tower::make_duals(unsigned i) -> void {
  unsigned degree = p[i];
  size_t width = N[i];
  // q_{p-1}=1; q_{k-1}=g_i q_k+f_k, then b_k=q_k/f'(g_i).
  auto g = generator(i);
  Elt derivative(N[i + 1]);
  for (unsigned k = 1; k <= degree; ++k) {
    Elt term = k == degree ? one(i) : f[i][k];
    term.resize(N[i + 1]);
    for (auto& c : term) {
      fmpq_mul_ui(c.a, c.a, k);
      fmpq_mul_ui(c.b, c.b, k);
    }
    Elt power = one(i + 1);
    for (unsigned j = 1; j < k; ++j) power = mul(power, g, i + 1);
    plus(derivative, mul(term, power, i + 1));
  }
  auto dinv = inverse(derivative, i + 1);
  Elt q = one(i + 1);
  dual[i].resize(degree);
  for (unsigned k = degree; k-- > 0;) {
    dual[i][k] = mul(q, dinv, i + 1);
    if (k) {
      q = mul(q, g, i + 1);
      Elt coeff = f[i][k];
      coeff.resize(width * degree);
      plus(q, coeff);
    }
  }
}

auto Tower::substitute(const Elt& x, unsigned n, const std::vector<Elt>& images,
                       bool conjugate_base) const -> Elt {
  Elt answer(N[n]);
  std::vector<std::vector<Elt>> powers(n);
  for (unsigned i = 0; i < n; ++i) {
    powers[i].push_back(one(n));
    Elt image = images.at(i);
    image.resize(N[n]);
    for (unsigned k = 1; k < p[i]; ++k) powers[i].push_back(mul(powers[i].back(), image, n));
  }
  for (size_t e = 0; e < x.size(); ++e)
    if (!x[e].zero()) {
      Elt term(N[n]);
      term[0] = x[e];
      if (conjugate_base) {
        if (s) fmpq_add(term[0].a, term[0].a, term[0].b);
        fmpq_neg(term[0].b, term[0].b);
      }
      for (unsigned i = 0; i < n; ++i) {
        size_t digit = (e / N[i]) % p[i];
        if (digit) term = mul(term, powers[i][digit], n);
      }
      plus(answer, term);
    }
  return answer;
}

Elt read_leaves(const Reader& r, size_t id, const Tower&) {
  const auto& raw = r.element_leaves(id);
  Elt out(raw.size());
  for (size_t j = 0; j < raw.size(); ++j) {
    fmpq_set_fmpz(out[j].a, raw[j].x.v);
    fmpq_set_fmpz(out[j].b, raw[j].y.v);
  }
  return out;
}

std::vector<ComplexBall> monomials(const Tower& T, unsigned n,
                                   const std::vector<ComplexBall>& roots, slong prec) {
  std::vector<ComplexBall> out(T.N[n]);
  acb_one(out[0].v);
  for (unsigned i = 0; i < n; ++i)
    for (unsigned k = 1; k < T.p[i]; ++k)
      for (size_t j = 0; j < T.N[i]; ++j)
        acb_mul(out[k * T.N[i] + j].v, out[(k - 1) * T.N[i] + j].v, roots[i].v, prec);
  return out;
}

void evaluate(acb_t out, const Tower& T, const Elt& x, unsigned n,
              const std::vector<ComplexBall>& roots, const acb_t omega, slong prec) {
  auto terms = monomials(T, n, roots, prec);
  ComplexBall c;
  acb_zero(out);
  for (size_t j = 0; j < x.size(); ++j)
    if (!x[j].zero()) {
      acb_from_ke(c.v, x[j], omega, prec);
      acb_addmul(out, c.v, terms[j].v, prec);
    }
}

std::vector<ComplexBall> level_roots(const Tower& T, unsigned i,
                                     const std::vector<ComplexBall>& lower, const acb_t omega,
                                     slong prec) {
  const unsigned degree = T.p[i];
  std::vector<ComplexBall> result(degree);
  ComplexBall c;
  if (degree == 2) {
    evaluate(c.v, T, T.f[i][0], i, lower, omega, prec);
    acb_neg(c.v, c.v);
    if (arb_is_negative(acb_realref(c.v))) {
      acb_neg(c.v, c.v);
      acb_sqrt(result[0].v, c.v, prec);
      acb_mul_onei(result[0].v, result[0].v);
    } else
      acb_sqrt(result[0].v, c.v, prec);
    acb_neg(result[1].v, result[0].v);
    require(acb_is_finite(result[0].v) && !acb_contains_zero(result[0].v) &&
                !acb_overlaps(result[0].v, result[1].v),
            "quadratic root isolation");
  } else {
    acb_poly_t polynomial;
    acb_poly_init(polynomial);
    for (unsigned k = 0; k < degree; ++k) {
      evaluate(c.v, T, T.f[i][k], i, lower, omega, prec);
      acb_poly_set_coeff_acb(polynomial, k, c.v);
    }
    acb_one(c.v);
    acb_poly_set_coeff_acb(polynomial, degree, c.v);
    acb_ptr roots = _acb_vec_init(degree);
    bool isolated = acb_poly_find_roots(roots, polynomial, nullptr, 0, prec) == slong(degree);
    for (unsigned j = 0; j < degree; ++j) {
      acb_set(result[j].v, roots + j);
      isolated = isolated && acb_is_finite(roots + j);
      for (unsigned k = 0; k < j; ++k) isolated = isolated && !acb_overlaps(roots + j, roots + k);
    }
    _acb_vec_clear(roots, degree);
    acb_poly_clear(polynomial);
    require(isolated, "odd root isolation");
  }
  return result;
}

Builder::Builder(const Reader& r, BuildOptions o) : reader(r), options(o), T(r.meta()) {}

auto Builder::find(const std::string& kind, unsigned level, int64_t axis, int64_t k) const
    -> size_t {
  const auto& el = reader.meta().elements;
  for (size_t j = 0; j < el.size(); ++j)
    if (el[j].kind == kind && el[j].level == level && el[j].axis == axis && el[j].k == k) return j;
  return SIZE_MAX;
}

auto Builder::element(size_t id) const -> Elt {
  auto lam = read_leaves(reader, id, T);
  return T.from_leaves(lam.data(), reader.meta().elements[id].level);
}

auto Builder::build_exact() -> void {
  tau.resize(reader.meta().smith.size(), std::vector<Elt>(T.t));
  for (unsigned i = 0; i < T.t; ++i) {
    unsigned degree = T.p[i];
    T.f[i].resize(degree, Elt(T.N[i]));
    if (degree == 2) {
      T.f[i][0] = element(find("wsq", i));
      for (auto& c : T.f[i][0]) {
        fmpq_neg(c.a, c.a);
        fmpq_neg(c.b, c.b);
      }
    } else
      for (unsigned k = 0; k < degree; ++k) T.f[i][k] = element(find("fcoef", i, -1, k));
    T.make_duals(i);
    for (size_t axis = 0; axis < tau.size(); ++axis) {
      Elt image = T.generator(i);
      size_t id = find(degree == 2 ? "act" : "actodd", i, int64_t(axis), degree == 2 ? -1 : 0);
      if (id != SIZE_MAX) {
        if (degree == 2) {
          Elt action = element(id);
          action.resize(T.N[i + 1]);
          image = T.mul(action, T.inverse(image, i + 1), i + 1);
        } else {
          Elt denominator = T.inverse(element(find("actdelta", i)), i);
          image.assign(T.N[i + 1], QuadraticElement());
          for (unsigned k = 0; k < degree; ++k) {
            Elt c = T.mul(element(find("actodd", i, int64_t(axis), k)), denominator, i);
            std::copy(c.begin(), c.end(), image.begin() + k * T.N[i]);
          }
        }
      }
      tau[axis][i] = std::move(image);
    }
  }
  for (const auto& [r, id] : reader.meta().representative) {
    rep_index.emplace(r, reps.size());
    reps.push_back(element(id));
  }
}

auto Builder::check_endomorphism(const std::vector<Elt>& map, bool flip,
                                 const std::string& label) const -> void {
  for (unsigned i = 0; i < T.t; ++i) {
    Elt x = map[i];
    x.resize(T.N[i + 1]);
    Elt residual = T.one(i + 1);
    for (unsigned k = T.p[i]; k-- > 0;) {
      residual = T.mul(residual, x, i + 1);
      Elt coefficient = T.substitute(T.f[i][k], i, map, flip);
      coefficient.resize(T.N[i + 1]);
      Tower::plus(residual, coefficient);
    }
    require(block_zero(residual.data(), residual.size()),
            label + " fails relation at level " + std::to_string(i));
  }
}

auto Builder::certify_roots() -> void {
  const auto& m = reader.meta();
  arb_set_si(acb_realref(omega.v), m.D0);
  acb_sqrt(omega.v, omega.v, options.precision);
  if (T.s) {
    acb_neg(omega.v, omega.v);
    acb_add_ui(omega.v, omega.v, 1, options.precision);
    acb_mul_2exp_si(omega.v, omega.v, -1);
  }
  roots.resize(T.t);
  for (unsigned i = 0; i < T.t; ++i) {
    auto all = level_roots(T, i, roots, omega.v, options.precision);
    int selected = -1;
    for (unsigned j = 0; j < all.size(); ++j)
      if (acb_overlaps(all[j].v, m.branches[i].v)) {
        require(selected < 0, "ambiguous branch");
        selected = int(j);
      }
    require(selected >= 0 && acb_contains(m.branches[i].v, all[size_t(selected)].v),
            "branch root containment");
    roots[i] = all[size_t(selected)];
  }
}

auto Builder::transport(const std::vector<ComplexBall>& old, size_t axis) const
    -> std::vector<ComplexBall> {
  std::vector<ComplexBall> next(T.t);
  ComplexBall candidate;
  for (unsigned i = 0; i < T.t; ++i) {
    evaluate(candidate.v, T, tau[axis][i], i + 1, old, omega.v, options.precision);
    auto all = level_roots(T, i, next, omega.v, options.precision);
    int pick = -1;
    for (size_t j = 0; j < all.size(); ++j)
      if (acb_overlaps(all[j].v, candidate.v)) {
        require(pick < 0, "ambiguous action root");
        pick = int(j);
      }
    require(pick >= 0, "action root not found");
    next[i] = all[size_t(pick)];
  }
  return next;
}

auto Builder::find_conjugation() -> void {
  const auto& mod = reader.meta().smith;
  std::vector<uint64_t> word(mod.size(), 0), selected;
  size_t matches = 0;
  // Higher axes are applied first, matching mixed-radix enumeration with
  // the least nonzero digit removed for each parent. Keep only one path
  // per recursion depth instead of caching every candidate embedding.
  std::function<void(size_t, const std::vector<ComplexBall>&)> visit =
      [&](size_t remaining, const std::vector<ComplexBall>& embedding) {
        if (remaining) {
          size_t axis = remaining - 1;
          auto current = embedding;
          for (uint64_t digit = 0; digit < mod[axis]; ++digit) {
            word[axis] = digit;
            visit(axis, current);
            if (digit + 1 < mod[axis]) current = transport(current, axis);
          }
          return;
        }
        bool match = true;
        ComplexBall target;
        for (unsigned i = 0; i < T.t; ++i) {
          acb_conj(target.v, roots[i].v);
          if (!acb_overlaps(target.v, embedding[i].v)) match = false;
        }
        if (match) {
          selected = word;
          ++matches;
        }
      };
  visit(mod.size(), roots);
  require(matches == 1, "complex conjugation word not unique");
  for (unsigned i = 0; i < T.t; ++i) conj.push_back(T.generator(i));
  for (size_t axis = 0; axis < mod.size(); ++axis)
    for (uint64_t count = 0; count < selected[axis]; ++count) {
      auto previous = conj;
      for (unsigned i = 0; i < T.t; ++i) conj[i] = T.substitute(tau[axis][i], i + 1, previous);
    }
  report.conjugation.assign(selected.begin(), selected.end());
  check_endomorphism(conj, false, "conjugation");
}

auto Builder::conj_images_numeric(const std::vector<Elt>& images, bool) const
    -> std::vector<ComplexBall> {
  std::vector<ComplexBall> values(T.t);
  for (unsigned i = 0; i < T.t; ++i)
    evaluate(values[i].v, T, images[i], i + 1, roots, omega.v, options.precision);
  return values;
}
}  // namespace sic_check::norm
