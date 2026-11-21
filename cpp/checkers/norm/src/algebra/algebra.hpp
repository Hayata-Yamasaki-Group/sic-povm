// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/acb_poly.h>
#include <flint/fmpq_mat.h>

#include "../format/input.hpp"
#include "../model/input_model.hpp"
namespace norm = sic_check::norm;
namespace sic_check::norm {

struct QuadraticElement {
  fmpq_t a, b;

  QuadraticElement();

  QuadraticElement(const QuadraticElement& v);

  QuadraticElement(QuadraticElement&& v) noexcept;

  QuadraticElement& operator=(const QuadraticElement& v);

  ~QuadraticElement();

  bool zero() const;
};

using Elt = std::vector<QuadraticElement>;

bool block_zero(const QuadraticElement* x, size_t n);

void acb_from_ke(acb_t out, const QuadraticElement& x, const acb_t omega, slong prec);

struct Tower {
  unsigned t, s;
  int64_t constant;
  std::vector<unsigned> p;
  std::vector<size_t> N{1};
  std::vector<std::vector<Elt>> f, dual;

  explicit Tower(const Meta& m);

  QuadraticElement product(const QuadraticElement& x, const QuadraticElement& y) const;

  static void plus(Elt& x, const Elt& y, bool negative = false);

  Elt one(unsigned n) const;

  Elt generator(unsigned i) const;

  Elt multiply(const QuadraticElement* x, const QuadraticElement* y, unsigned n) const;

  Elt mul(const Elt& x, const Elt& y, unsigned n) const;

  Elt inverse(const Elt& x, unsigned n) const;

  Elt from_leaves(const QuadraticElement* lam, unsigned n) const;

  void make_duals(unsigned i);

  Elt substitute(const Elt& x, unsigned n, const std::vector<Elt>& images,
                 bool conjugate_base = false) const;
};

Elt read_leaves(const Reader& r, size_t id, const Tower&);

std::vector<ComplexBall> monomials(const Tower& T, unsigned n,
                                   const std::vector<ComplexBall>& roots, slong prec);

void evaluate(acb_t out, const Tower& T, const Elt& x, unsigned n,
              const std::vector<ComplexBall>& roots, const acb_t omega, slong prec);

std::vector<ComplexBall> level_roots(const Tower& T, unsigned i,
                                     const std::vector<ComplexBall>& lower, const acb_t omega,
                                     slong prec);

struct BuildOptions {
  bool numeric_check = false;
  slong precision = 768;
};

struct Builder {
  const Reader& reader;
  BuildOptions options;
  Tower T;
  std::vector<Elt> reps, conj;
  std::vector<std::vector<Elt>> tau;
  std::map<int64_t, size_t> rep_index;
  std::vector<ComplexBall> roots;
  ComplexBall omega;
  bool conj_k = false;
  struct {
    std::vector<int> conjugation;
  } report;

  Builder(const Reader& r, BuildOptions o);

  size_t find(const std::string& kind, unsigned level, int64_t axis = -1, int64_t k = -1) const;

  Elt element(size_t id) const;

  void build_exact();

  void check_endomorphism(const std::vector<Elt>& map, bool flip, const std::string& label) const;

  void certify_roots();

  std::vector<ComplexBall> transport(const std::vector<ComplexBall>& old, size_t axis) const;

  void find_conjugation();

  std::vector<ComplexBall> conj_images_numeric(const std::vector<Elt>& images, bool) const;
};
}  // namespace sic_check::norm
