// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/sicx/verify.hpp"
#include "sicx/codec/block.hpp"
#include "sicx/exact/exact.hpp"
namespace sic::sicx::exact {
Builder::Images Builder::identity_images() const {
  Images S;
  for (unsigned i = 0; i < T.t; ++i) S.push_back(T.generator(i));
  return S;
}

Builder::Images Builder::compose(const Images& S, size_t l) const {
  Images out;
  for (unsigned i = 0; i < T.t; ++i) out.push_back(T.apply(tau[l][i], i + 1, S));
  return out;
}

Builder::Images Builder::word_images(const std::vector<uint64_t>& word) const {
  Images S = identity_images();
  for (size_t l = 0; l < word.size(); ++l)
    for (uint64_t e = 0; e < word[l]; ++e) S = compose(S, l);
  return S;
}

void Builder::check_endomorphism(const Images& S, bool conjugate_k, const std::string& what) const {
  for (unsigned i = 0; i < T.t; ++i) {
    const unsigned pi = T.p[i];
    Elt sum = T.zero(i + 1);
    Elt power = T.one(i + 1);
    for (unsigned k = 0; k <= pi; ++k) {
      Elt coef = k == pi ? T.one(i + 1) : T.embed(T.apply(T.f[i][k], i, S, conjugate_k), i + 1);
      Elt term = T.mul(coef, power, i + 1);
      Tower::add_to(sum.data(), term.data(), term.size());
      power = T.mul(power, S[i], i + 1);
    }
    if (!block_zero(sum.data(), sum.size()))
      fail(sic::ErrorKind::construction,
           what + ": images violate the relation of level " + std::to_string(i));
  }
}

std::vector<ComplexBall> Builder::conj_images_numeric(const Images& S, bool conjugate_k) const {
  (void)conjugate_k;
  std::vector<ComplexBall> out(T.t);
  for (unsigned i = 0; i < T.t; ++i) {
    const auto mono = monomials(T, i + 1, roots, opt.precision);
    evaluate(out[i].v, S[i], mono, omega.v, opt.precision);
  }
  return out;
}

void Builder::find_conjugation() {
  const slong prec = opt.precision;
  if (meta.D0 < 0) {
    conj_k = true;
    if (!opt.conjugation_images.empty()) {
      if (opt.conjugation_images.size() != T.t)
        throw Error(ErrorKind::invalid_argument, "conjugation image count mismatch");
      for (unsigned i = 0; i < T.t; ++i)
        if (opt.conjugation_images[i].size() != T.N[i + 1])
          throw Error(ErrorKind::invalid_argument, "conjugation image width mismatch");
      conj = opt.conjugation_images;
    } else {
      conj = identity_images();
      for (unsigned i = 0; i < T.t; ++i) {
        if (T.p[i] != 2)
          throw Error(ErrorKind::unsupported,
                      "supply exact semilinear conjugation images for this presentation");
        const auto mono = monomials(T, i + 1, roots, prec);
        ComplexBall candidate, target;
        evaluate(candidate.v, conj[i], mono, omega.v, prec);
        acb_conj(target.v, roots[i].v);
        const bool positive = acb_overlaps(candidate.v, target.v);
        acb_neg(candidate.v, candidate.v);
        const bool negative = acb_overlaps(candidate.v, target.v);
        if (positive == negative)
          throw Error(ErrorKind::unsupported,
                      "supply exact semilinear conjugation images for this presentation");
        if (negative)
          for (auto& c : conj[i]) {
            fmpq_neg(c.a, c.a);
            fmpq_neg(c.b, c.b);
          }
      }
    }
  } else {
    conj_k = false;
    std::vector<std::vector<uint64_t>> found;
    std::vector<uint64_t> word;
    std::function<void(size_t, std::vector<ComplexBall>)> walk = [&](size_t l,
                                                                     std::vector<ComplexBall> V) {
      if (l == meta.smith.size()) {
        bool all = true;
        acb_t c;
        acb_init(c);
        for (unsigned i = 0; i < T.t && all; ++i) {
          acb_conj(c, roots[i].v);
          all = acb_overlaps(c, V[i].v);
        }
        acb_clear(c);
        if (all) found.push_back(word);
        return;
      }
      for (uint64_t e = 0; e < meta.smith[l]; ++e) {
        word.push_back(e);
        walk(l + 1, V);
        word.pop_back();
        // V <- numeric images of sigma o tau_l: tau_l(g_i) evaluated at V
        std::vector<ComplexBall> next(T.t);
        for (unsigned i = 0; i < T.t; ++i) {
          std::vector<ComplexBall> head(V.begin(), V.begin() + i + 1);
          const auto mono = monomials(T, i + 1, head, prec);
          evaluate(next[i].v, tau[l][i], mono, omega.v, prec);
        }
        V = std::move(next);
      }
    };
    uint64_t work = 1;
    for (const auto factor : meta.smith) {
      if (!factor || work > opt.maximum_conjugation_search / factor)
        throw Error(ErrorKind::resource, "conjugation Smith search exceeds work policy");
      work *= factor;
    }
    walk(0, roots);
    if (found.size() != 1)
      fail(sic::ErrorKind::construction, "complex conjugation: " + std::to_string(found.size()) +
                                             " Smith words match (expected exactly 1)");
    conj = word_images(found[0]);
    report.conjugation = found[0];
  }
  check_endomorphism(conj, conj_k, "conjugation");
  for (unsigned i = 0; i < T.t; ++i) {
    const auto twice = T.apply(conj[i], i + 1, conj, conj_k);
    const auto generator = T.generator(i);
    if (twice != generator)
      fail(sic::ErrorKind::construction, "conjugation does not square to identity");
  }
  // numerical identification: conj(iota(g_i)) overlaps iota(S_i), and omega -> conj(omega)
  const auto num = conj_images_numeric(conj, conj_k);
  acb_t c;
  acb_init(c);
  for (unsigned i = 0; i < T.t; ++i) {
    acb_conj(c, roots[i].v);
    if (!acb_overlaps(c, num[i].v)) {
      acb_clear(c);
      fail(sic::ErrorKind::construction, "conjugation image of g_" + std::to_string(i) +
                                             " is not the complex conjugate of its root");
    }
  }
  acb_clear(c);
}

Elt Builder::conj_omega() const {  // the image of omega under conjugation, as an element of L
  Elt w(T.N[T.t]);
  if (meta.D0 == 1) return w;
  if (conj_k) {
    fmpq_set_si(w[0].a, T.s, 1);
    fmpq_set_si(w[0].b, -1, 1);
  } else
    fmpq_one(w[0].b);
  return w;
}

std::vector<Elt> Builder::conj_generators_full() const {
  std::vector<Elt> out;
  for (unsigned i = 0; i < T.t; ++i) out.push_back(T.embed(conj[i], T.t));
  return out;
}
}  // namespace sic::sicx::exact
