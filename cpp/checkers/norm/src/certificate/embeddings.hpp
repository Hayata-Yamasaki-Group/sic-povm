// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/acb_poly.h>
#include <flint/fmpz.h>
#include <flint/fmpz_factor.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../algebra/algebra.hpp"
namespace norm = sic_check::norm;
namespace sic_check::norm {

// Rigorous integer upper bound e with |z| < 2^e (LONG_MIN for an exact zero).
slong log2_upper(const acb_t z);

double log2_approx(const acb_t z);

slong ceil_log2_u(uint64_t x);

void omega_at(acb_t out, const norm::Tower& T, int64_t D0, bool other, slong prec);

// Horner evaluation of x in F_n (power basis) at the roots r[0..n-1]; no monomial table.
void horner(acb_t out, const norm::Tower& T, const QuadraticElement* x, unsigned n,
            const std::vector<ComplexBall>& r, const acb_t omega, slong prec);

// Isolating balls of the roots of a monic polynomial with ball coefficients (each ball contains
// exactly one root of every polynomial in the coefficient family). Empty on failure.
std::vector<ComplexBall> isolate(const acb_poly_t P, unsigned p, slong prec);

struct Ctx {
  const norm::Reader& reader;
  const norm::Meta& meta;
  norm::Builder& B;
  const norm::Tower& T;
  unsigned threads = 1;

  explicit Ctx(const norm::Reader& r, norm::Builder& b);

  // monic level polynomial f_i with coefficients evaluated (Horner) at the given lower roots

  void level_poly(acb_poly_t P, unsigned i, const std::vector<ComplexBall>& r, const acb_t omega,
                  slong prec) const;
};

// ---------------------------------------------------------------- all embeddings (Step 3)
// Values of power-basis elements at all 2 N_n algebra homomorphisms F_n -> C.
struct AllEmbeddings {
  const norm::Tower& T;
  slong prec;
  ComplexBall om[2];
  std::vector<std::vector<ComplexBall>> roots;  // roots[i][psi * p_i + j], psi an embedding of F_i

  AllEmbeddings(const norm::Tower& t, int64_t D0, slong pr);

  size_t count(unsigned n) const;

  std::vector<ComplexBall> values(const QuadraticElement* x, unsigned n) const;

  bool build();
};

// ---------------------------------------------------------------- leaf-based estimate (no exact
// arithmetic) Values of elements given by trace-dual leaves at all algebra homomorphisms, through
// the Euler duals b_k = q_k(r)/f'(r). Used for feasibility estimates at large d (h of the unscaled
// eta, the norm of W = prod f_i'(g_i)); it does not give the integral scaling q, which needs the
// exact power basis.
struct LeafEmbeddings {
  const norm::Reader& reader;
  const norm::Tower& T;
  slong prec;
  ComplexBall om[2];
  std::vector<std::vector<ComplexBall>> roots, duals;  // roots[i][psi*p+j]; duals[i][(psi*p+j)*p+k]
  std::vector<double> lvl_max, lvl_min,
      lvl_logabs_sum;  // per level: log2 |phi(c_i)| stats, sum log2|f'(r)| over all roots

  LeafEmbeddings(const norm::Reader& r, const norm::Tower& t, slong pr);

  size_t count(unsigned n) const;

  std::vector<ComplexBall> values(const QuadraticElement* lam, unsigned n) const;

  size_t find(const std::string& kind, unsigned level, int64_t k) const;

  bool build();
};
}  // namespace sic_check::norm
