// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/acb.h>
#include <flint/acb_poly.h>
#include <flint/arb.h>
#include <flint/fmpz_poly.h>
#include <flint/mag.h>
#include <flint/nmod.h>
#include <flint/nmod_poly.h>
#include <flint/nmod_poly_factor.h>
#include <flint/ulong_extras.h>
#include <sys/resource.h>

#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <functional>
#include <mutex>
#include <random>
#include <thread>

#include "../algebra/modular_field.hpp"
#include "../format/sicx_input.hpp"
namespace sic_check::crt {
using namespace sic_check::crt::sicx;

// ================================================================================================
// ball engine
struct AcbVec {
  acb_ptr v = nullptr;
  size_t n = 0;
  AcbVec() = default;

  explicit AcbVec(size_t k);

  void resize(size_t k);

  ~AcbVec();

  AcbVec(const AcbVec&) = delete;
  AcbVec& operator=(const AcbVec&) = delete;

  AcbVec(AcbVec&& o) noexcept;

  AcbVec& operator=(AcbVec&& o) noexcept;

  acb_ptr operator[](size_t i) const;
};

// Compile-time exact-zero sentinel used by the bound initialization.
constexpr slong kNegLog = -(slong(1) << 30);

slong mag_exp_upper(const mag_t m);

void parse_arbdump(arb_t out, const string& s);

struct Arch {
  const Ctx& C;
  slong prec;
  AcbVec om;                  // 2 values: kappa 0 physical (1 - sqrt D0)/2, kappa 1
  vector<AcbVec> R;           // R[j][psi + P rho]
  vector<vector<AcbVec>> Bd;  // Bd[j][k][psi + P rho]

  Arch(const Ctx& c, slong pr);

  AcbVec transform(size_t id) const;

  // isolating balls of the roots of monic X^p + sum a_k X^k (ball coefficients); empty on failure

  static bool isolate(acb_srcptr coef, Uint64 pj, acb_ptr roots, slong prec);

  // build all levels; also per-level stats
  vector<slong> Tc;   // quadratic: upper bound of sum over Hom(F_j) of log2 |phi(c_j)|
  vector<slong> Tfp;  // odd: upper bound of sum over Hom(F_{j+1}) of log2 |phi(f_j'(g_j))|
  vector<slong> Bg;   // max log2 |root|

  bool build();

  // max log2 |value| (upper, integer) and sum log2 (upper, integer) over a value vector

  static void stats(const AcbVec& v, slong& Bmax, bool& finite);
};

// ================================================================================================
// bounds Lemma D (README): for x in F_n with integral leaves, level set J and scale u, the
// denominator ideal of x divides prod_{j<n} Bfrak_{j, e_j}. Values below are log2 of ideal norms
// over L (totals, not per conjugate).
struct Bounds {
  Int64 n = 0;               // [L:Q]
  vector<Int64> TB0, TBmax;  // per level: dual bounds for digit 0 and for any digit
  vector<int> quad_before;   // number of quadratic levels below j

  Int64 elem(const Ctx& C, size_t id) const;
};
// need sum log2 p > B + T/n

Int64 cdiv(Int64 a, Int64 b);

Int64 log2sum(Int64 a, Int64 b);
}  // namespace sic_check::crt
