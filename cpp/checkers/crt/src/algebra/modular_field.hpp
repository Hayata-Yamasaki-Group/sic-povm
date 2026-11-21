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

#include "../format/sicx_input.hpp"
namespace sic_check::crt {
using namespace sic_check::crt::sicx;

// ------------------------------------------------------------------------------------------------
// exact data
struct Coords {
  vector<fmpz> X, Y;  // stored coordinates over the sub-layout (original (x, y) basis)
  fmpz_t u;           // scale: leaf = u * (x + y omega)
  slong v2 = 0;       // 2-adic valuation of u

  Coords();

  ~Coords();

  Coords(const Coords&) = delete;
  Coords& operator=(const Coords&) = delete;
};

struct Ctx {
  Input in;
  int t = 0;
  Uint64 d = 0, D = 0;
  Int64 D0 = 0;
  vector<Uint64> deg, N;     // N[j] = [F_j : K], j = 0..t
  vector<Uint64> smith, Ms;  // mixed-radix multipliers of Galois words
  size_t naxes = 0;
  vector<std::unique_ptr<Coords>> co;
  vector<size_t> wsq;                     // per level (quadratic)
  vector<vector<size_t>> fcoef;           // per level (odd): k
  vector<size_t> actdelta;                // per level (odd) or SIZE_MAX
  vector<vector<size_t>> act;             // [level][axis] quadratic record or SIZE_MAX
  vector<vector<vector<size_t>>> actodd;  // [level][axis][k] or empty
  vector<Int64> reppos;                   // 0, 1, 2, 4, ..., d/2
  vector<size_t> repid;                   // element ids, same order
  vector<int> rep_of_m;                   // m -> index into reppos
  vector<vector<Uint64>> wordm;           // entry word digits of m
  vector<Uint64> wc;                      // entry word, linear index
  vector<Uint64> cstar;                   // conjugation word digits (found by Arch)

  Uint64 lin(const vector<Uint64>& w) const;

  vector<Uint64> digits(Uint64 c) const;

  vector<Uint64> add(const vector<Uint64>& a, const vector<Uint64>& b) const;

  // linear index of (word with linear index c) + (word with digits w), no allocation

  Uint64 addw(Uint64 c, const vector<Uint64>& w) const;

  vector<Uint64> scale(const vector<Uint64>& a, Uint64 k) const;

  // Stabilizer generators below are units in the supported power-of-two certificate scope.

  vector<Uint64> word_of_odd(Uint64 k) const;

  void load(const string& path, const Trust& trust = {});
};

// ================================================================================================
// modular engine
struct ModP {
  const Ctx& C;
  Uint64 p = 0;
  nmod_t mod{};
  Uint64 om[2]{};
  Uint64 inv2 = 0;
  vector<vector<Uint64>> R;          // R[j][psi + P*rho], P = 2 N[j]
  vector<vector<vector<Uint64>>> B;  // B[j][k][psi + P*rho] dual basis values
  int built = 0;                     // levels built

  explicit ModP(const Ctx& c);

  Uint64 mul(Uint64 a, Uint64 b) const;

  Uint64 add(Uint64 a, Uint64 b) const;

  Uint64 sub(Uint64 a, Uint64 b) const;

  Uint64 inv(Uint64 a) const;

  Uint64 red(const fmpz_t x) const;

  // values of element id at all 2 N[field] points of its field

  vector<Uint64> transform(size_t id) const;

  bool init(Uint64 prime, bool one_place = false);

  // build level j; returns 1 split, 0 not split, 2 not split with an irreducibility witness at
  // level j

  int build_level(int j);
};

// candidate prime -> split test (all levels); records irreducibility witnesses
struct Witnesses {
  std::mutex mu;
  vector<Uint64> prime;
};

int try_prime(ModP& M, Uint64 p, Witnesses& W, bool one_place = false, int maxlevel = -1);
}  // namespace sic_check::crt
