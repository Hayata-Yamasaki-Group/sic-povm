// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/fmpq_mat.h>

#include "sic/sicx/exact.hpp"
namespace sic::sicx::exact {
bool block_zero(const QuadraticElement* x, size_t n);

class Tower {
 public:
  unsigned t = 0;
  std::vector<unsigned> p;
  std::vector<size_t> N;  // N[i] = [F_i : K]
  int s = 0;              // omega^2 = s*omega + m
  fmpq_t m;
  int64_t D0 = 0;
  std::vector<std::vector<Elt>> f;     // f[i][k], k < p_i: g_i^p + sum f_k g_i^k = 0, f_k in F_i
  std::vector<std::vector<Elt>> dual;  // dual[i][k]: trace-dual basis element b_{i,k} in F_{i+1}

  explicit Tower(const Meta& meta);
  ~Tower();
  Tower(const Tower&) = delete;
  Tower& operator=(const Tower&) = delete;

  // ---- K arithmetic
  void kmul_add(QuadraticElement& r, const QuadraticElement& x, const QuadraticElement& y) const;
  QuadraticElement kinv(const QuadraticElement& x) const;

  // ---- F_n arithmetic (absolute monomial basis of size N[n])
  Elt zero(unsigned n) const;
  Elt one(unsigned n) const;
  static void add_to(QuadraticElement* out, const QuadraticElement* x, size_t n);
  static void sub_from(QuadraticElement* out, const QuadraticElement* x, size_t n);
  // out += a * b in F_n.
  void mul_add(QuadraticElement* out, const QuadraticElement* a, const QuadraticElement* b,
               unsigned n) const;
  Elt mul(const Elt& a, const Elt& b, unsigned n) const;
  // Inverse in F_n.
  Elt inverse(const Elt& x, unsigned n) const;
  // Generic inverse: solve x*y = 1 over Q (matrix of multiplication by x, 2N x 2N).
  Elt inverse_general(const Elt& x, unsigned n) const;

  // ---- levels
  Elt embed(const Elt& x, unsigned to) const;
  // The generator g_i as an element of F_{i+1}.
  Elt generator(unsigned i) const;
  void set_level(unsigned i, std::vector<Elt> coefficients);

  // Element of F_n from its leaf coordinates (trace-dual basis, little-endian mixed radix).
  Elt from_leaves(const QuadraticElement* lam, unsigned n) const;

  // Ring endomorphism of F_n fixing K (or acting on K through `kmap`) that sends g_i to img[i] in
  // F_{i+1}.
  Elt apply(const Elt& x, unsigned n, const std::vector<Elt>& img, bool conjugate_k = false) const;
};

}  // namespace sic::sicx::exact
