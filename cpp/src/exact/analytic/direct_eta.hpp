// SPDX-License-Identifier: AGPL-3.0-or-later
// Direct representative entries from a certified ghost root.
//
// Implements the finite Fourier chirp identities for representative extraction.
// For a stored class e with root vectors v,u (bilinear, S = sum_a u_a v_a) and a
// caller g whose relative map h = e^-1 g acts on ghost coordinates by the
// multiplication matrix H = [A B; C D] (A = (d-1)B + D, C = -B mod d),
//
//   eta_m(g) = chi_g q0/S sum_{i=0}^{d-1} (-1)^(i-m+i'+j')
//              z^(ng(i+m)^2 - n(i'^2+j'^2)) C(n i', j'),   (i',j') = H (i,-m),
//
// with C(f,j) = sum_a v_a u_(a+j) zeta^(-f a), z = exp(pi i/d), zeta = z^2. The
// full-line sum collapses to one chirped DFT entry times one Gauss sum:
//
//   C != 0:  eta = chi_g k q0/S z^P Vhat_(k,a0,gamma)(f) Ugauss_(k,gamma),
//   C == 0:  eta = chi_g d q0/S (-1)^(lm m) v_(a*) u_(b*).
//
// Applying this to a stored analytic class requires the analytic identification
// of the certified root with the class at EVERY raw lift (epsilon invariance),
// not only at the last-writer lift that the packed table stores. The planner
// below is pure modular arithmetic and refuses (empty plan + reason) whenever a
// required congruence fails; callers then use the literal table path.
#pragma once
#include <flint/acb_dft.h>
#include <flint/ulong_extras.h>

#include <algorithm>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "exact/runtime/parallel.hpp"
#include "sic/support/ball_vectors.hpp"
#include "sic/support/stop.hpp"

namespace sic::exact {

class DirectEtaRows;

__extension__ typedef __int128 EtaInt128;  // exact modular intermediates

struct DirectEtaPlan {
  long position = -1;      // caller-owned output index
  bool conjugate = false;  // output is conj(eta_m(g')) for the physical caller g'
  int chi = 1;             // chi of the physical caller g'
  bool scalar = false;     // C == 0 stratum
  long astar = 0, bstar = 0;
  int scalar_sign = 1;                                 // (-1)^(lm m)
  long k = 0, L = 0, a0 = 0, gamma = 0, f = 0, P = 0;  // C != 0 stratum
};

namespace direct_eta_detail {
long mod(EtaInt128 x, long m);
long inverse_mod(long a, long m);
}  // namespace direct_eta_detail

// Pure modular planning. Returns an empty string on success, otherwise the
// refusal reason. d must be a power of two in [4, 2^20].
std::string plan_direct_eta(long d, long n, long ng, long m, long A, long B, long C, long D,
                            int chi, bool conjugate, DirectEtaPlan& plan);

// A borrowed, authenticated root at one precision plus the shared constants.
class DirectEtaRoot {
 public:
  // v,u full length-d vectors (normalization component already inserted),
  // w2d[r] = exp(2 pi i r/(2d)) at >= precision. Throws if S contains zero.
  DirectEtaRoot(long d, slong precision, sic::support::ComplexBallVector v,
                sic::support::ComplexBallVector u, const std::vector<acb_struct>& w2d);
  DirectEtaRoot(const DirectEtaRoot&) = delete;
  DirectEtaRoot& operator=(const DirectEtaRoot&) = delete;
  ~DirectEtaRoot();

  long dimension() const { return d_; }
  slong precision() const { return p_; }

  // Test-only control: when set, the row loop throws
  // a non-stop runtime_error at row census.rows/2 after other rows may already
  // have been evaluated, so callers must unwind and refuse the whole class.
#ifdef SIC_ENABLE_TEST_HOOKS
  bool test_fault_mid_rows = false;
  void* failure_context = nullptr;
  void (*before_row)(void*, long) = nullptr;
#endif

  struct Census {
    size_t rows = 0, scalar = 0, row_entries = 0;
    unsigned long long row_cells = 0;
    size_t hits = 0, evictions = 0;
  };

  // Evaluates every plan into out[i] (rounded to out_precision). Plans are
  // grouped by their transform key; each key's chirped DFT row is computed once.
  Census evaluate(const std::vector<DirectEtaPlan>& plans, sic::support::ComplexBallVector& out,
                  slong out_precision, int threads, DirectEtaRows* cache = nullptr,
                  const std::string& domain = {});

  // Workspace upper bound for one evaluate() call excluding its output.
  static unsigned long long workspace_bytes(long d, slong precision, int threads, size_t plans);

 private:
  struct Plan {
    acb_dft_pre_t value;
  };

  void finish(acb_ptr value, const DirectEtaPlan& p, acb_ptr out, slong out_precision) const;

  void prepare_stratum(long k);

  long d_;
  slong p_;
  sic::support::ComplexBallVector v_, u_, w2d_, k0_;
  std::map<long, std::unique_ptr<Plan>> plans_;
  std::map<long, sic::support::ComplexBallVector> gauss_;
};

}  // namespace sic::exact
