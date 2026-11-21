// SPDX-License-Identifier: AGPL-3.0-or-later
// Certified Newton refinement of a conjugate ghost (replaces the high-precision table).
//
// For sigma in G, the conjugate ghost X = sigma(Pi~) is a rank-one idempotent. sigma acts on the
// entries of q by zeta_d -> zeta_d^n, so sigma(q) = q^n and
//   X = (1/d) sum_{i,j} A_{ni,j} q^{ni} p^j,  A_00 = 1,  A_{ni,j} = -V_ij ph_ij / (chi sqrt(d+1)),
//   ph_ij = (-1)^{i+j} e_{2d}(n (i^2 + j^2)),  V_ij = sigma(F((i eps + j)/d)),
// where chi = sigma(sqrt eps)/sqrt eps and zeta_{2d} -> zeta_{2d}^n. Internally the values are
// indexed by the position y' = (n i mod d, j) of their coefficient. Write X = v u^T / (u^T v) with
// v_k = u_k = 1 (2d - 2 unknowns z). By [RW, Prop. 1], V_y V_{-y} = sigma(<y>^{-1}) =: K_y is a
// root of unity of order dividing 4 d^2. The equations V_y(z) V_{-y}(z) = K_y pin down X locally;
// we use a square subsystem of 2d - 2 of them.
//
// Certification. The low-precision table gives certified balls for all V_y, hence for z* (the true
// ghost) and, together with the order of <y>, the exact K_y. Simplified Newton with a fixed
// approximate inverse Y gives a high- precision z~. Two Krawczyk tests K(B) = z~ - Y F(z~) + (I - Y
// J(B))(B - z~) subset int(B) then prove: (1) the subsystem has a unique root in a box B0
// containing the ball of z*, which is therefore z*; (2) z* lies in a tiny box B1 subset B0. All
// quantities entering the tests are computed in ball arithmetic.
#pragma once
#include <flint/acb.h>
#include <flint/acb_dft.h>
#include <flint/acb_mat.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

#include "exact/certification/bounds/root_unity_isolation.hpp"
#include "exact/certification/newton/krawczyk_exclusion.hpp"
#include "exact/certification/newton/newton_linear_refinement.hpp"
#include "exact/certification/newton/parallel_ball_product.hpp"
#include "exact/certification/newton/parallel_inverse.hpp"
#include "exact/certification/selection/modular_row_selection.hpp"
#include "exact/certification/selection/selection_arithmetic.hpp"
#include "exact/certification/selection/selection_float.hpp"
#include "exact/runtime/newton_memory_admission.hpp"
#include "exact/runtime/newton_memory_trace.hpp"
#include "exact/runtime/row_worker_team.hpp"
#include "ghost_reconstruction.hpp"
#include "sic/support/flint_raii.hpp"
#include "sic/support/kernel_settings.hpp"
#include "sic/support/stop.hpp"
#include "support/resources.hpp"
#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace sic::exact {

struct GhostRefiner {
  enum class SelectionKind { native, modular_midpoint };
  struct CacheClear {
    std::vector<acb_ptr>& shifts;
    std::vector<acb_ptr>& frequencies;
    long length;
    ~CacheClear();
  };
  long d;
  int chi;
  long n;                  // zeta_{2d} -> zeta_{2d}^n
  int k = 0;               // normalisation index
  std::vector<long> S;     // equations: indices y = i d + j (paired with -y)
  std::vector<long> Kexp;  // K_y = e(Kexp[y] / (4 d^2)) for all y != 0
  // statistics
  double cond_est = 0, log2_rad0 = 0, log2_rad1 = 0, kr_ratio = 0, kr_mnorm = 0, kr_dev = 0;
  SelectionKind selection_kind = SelectionKind::native;
  slong inverse_precision_proposal = 0;
  int newton_iters = 0;
  slong achieved_bits = 0, final_prec = 0;
  bool seed_inverse = false;  // The caller may request a warm inverse precision hint.
  sic::support::resources::Bytes workspace_limit =
      std::numeric_limits<sic::support::resources::Bytes>::max();
  slong matrix_precision_hint = 0;  // slot proposal, never a cap on actual pl
  int kernel_threads = 1;           // explicit per-class share, independently owned
  // Scalar workspace only: no roots, contexts, equations or borrowed views.
  // The row team dies before its scratch, and both die at every refine exit.
  static constexpr size_t kRowScratchSlots = 7;
  // The pinned host has 64-byte coherence lines. An unused full-line gap
  // prevents neighboring workers' frequently written scalar slots sharing one,
  // without requiring stronger alignment from ComplexBallVector's allocator.
  static constexpr size_t kRowScratchStride =
      kRowScratchSlots + (64 + sizeof(acb_struct) - 1) / sizeof(acb_struct);
  struct VectorWorkspace {
    sic::support::ComplexBallVector scratch;
    std::optional<slong> precision;
    std::unique_ptr<RowWorkerTeam> team;
  };
  mutable std::unique_ptr<VectorWorkspace> vector_workspace;
  struct VectorAttemptContext {
    const sic::support::KernelSettings* settings;
    sic::support::resources::Bytes workspace, callback;
  };
  mutable std::optional<VectorAttemptContext> vector_attempt;
  int correction_passes = 2;  // Explicit kernel controls are used by numerical unit tests.
  phase_memory::Refusal memory_refusal = phase_memory::Refusal::none;
  sic::support::resources::Bytes required_workspace =
      0;  // zero: unknown for helper/thread-startup refusal
  phase_memory::Observer memory_observer;
  phase_memory::Trace* memory_trace = nullptr;  // active only within this refiner invocation
  bool phase_admission = false;
  std::optional<phase_memory::Ceilings> retained_memory;
  phase_memory::Publication memory_publication;
  sic::support::resources::Bytes callback_workspace_limit =
      std::numeric_limits<sic::support::resources::Bytes>::max();
  long correction_attempts = 0, correction_accepted = 0;
  double high_newton_seconds = 0, high_residual_seconds = 0, high_correction_seconds = 0;
  double correction_jacobian_seconds = 0, correction_inner_seconds = 0;
  // Read-only calibration seam, called once after B0 and before high Newton.
  std::function<void(const acb_mat_struct*, const std::vector<acb_struct>&, slong, slong, slong)>
      after_b0;
#ifdef SIC_ENABLE_TEST_HOOKS
  // Cold operation boundaries for exception-routing controls; absent in production.
  void* failure_context = nullptr;
  void (*before_operation)(void*, const char*) = nullptr;
#endif
  bool retain_certified_root = false;
  std::unique_ptr<CertifiedGhostRoot> certified_root;

  std::vector<long> pos, ipos;  // y = (i, j) -> y' = (n i mod d, j) and back
  GhostRefiner(long d_, int chi_, long n_);

  long neg(long y) const;

  struct Ctx {  // constants at a given precision
    slong p;
    std::vector<acb_struct> wd, w2d;
    sic::support::flint::Acb cst;  // wd[k] = e(k/d), w2d[k] = e(k/(2d)), cst = -chi sqrt(d+1)
    const std::vector<acb_struct>* Kc =
        nullptr;  // optional: K_y of the equations S (in order) at precision >= p
    // the same constants rounded from a context at a higher precision (no transcendental functions)
    Ctx(const Ctx& hi, slong p_);
    Ctx(long d, int chi, slong p_);
    ~Ctx();
  };
  // ph_y^{-1} * cst = -chi sqrt(d+1) / ph_y
  void coef(acb_t r, const Ctx& C, long yp) const;
  // z -> full v, u
  void expand(const std::vector<acb_struct>& z, std::vector<acb_struct>& v,
              std::vector<acb_struct>& u) const;
  long zidx(long a) const;  // position of v_a (a != k) in z; u_a at + d - 1
  // A_y and V_y for one y, given v, u, 1/s
  void complex_ball_vector_of(acb_t A, acb_t V, const Ctx& C, long y,
                              const std::vector<acb_struct>& v, const std::vector<acb_struct>& u,
                              const acb_t sinv) const;
  // F_S(z). The products t_j[a] = v_a u_{a+j} are shared by all equations with the same shift j (y
  // and -y), and each A_y = (1/s) sum_a w^{-ia} t_j[a] is one acb_dot against the row (w^{-ia})_a,
  // built once per frequency i.
  void residual_serial(std::vector<acb_struct>& F, const Ctx& C,
                       const std::vector<acb_struct>& z) const;
  static void check_vector_stop();
  RowWorkerTeam* vector_workers(size_t rows, std::optional<slong> precision = {}) const;
  void release_vector_workers() const;
  void residual(std::vector<acb_struct>& F, const Ctx& C, const std::vector<acb_struct>& z) const;

  // Complete rounding before any correction reads F. Rows update disjoint
  // coordinates; the final scalar maximum retains increasing-row order.
  double correction(const acb_mat_t inverse, std::vector<acb_struct>& F, std::vector<acb_struct>& z,
                    std::vector<acb_struct>& dz, slong precision, slong correction_precision) const;
  double commit_correction(const sic::support::ComplexBallVector& delta, std::vector<acb_struct>& z,
                           slong precision) const;

  void root_k(acb_t r, slong p, long e) const;
  // gradient of V_y with respect to z, written into row (length 2d-2) scaled by fac and added
  void addgrad(acb_ptr row, const Ctx& C, long y, const acb_t fac, const acb_t A,
               const std::vector<acb_struct>& v, const std::vector<acb_struct>& u,
               const acb_t sinv) const;
  void addgrad_with_scratch(acb_ptr row, const Ctx& C, long y, const acb_t fac, const acb_t A,
                            const std::vector<acb_struct>& v, const std::vector<acb_struct>& u,
                            const acb_t sinv, acb_ptr coefficient, acb_ptr term) const;
  // Jacobian rows for the equations ys (dE_y = V_{-y} dV_y + V_y dV_{-y}) at z (balls allowed)
  void jacobian_serial(acb_mat_t J, const Ctx& C, const std::vector<acb_struct>& z,
                       const std::vector<long>& ys) const;

  void jacobian(acb_mat_t J, const Ctx& C, const std::vector<acb_struct>& z,
                const std::vector<long>& ys) const;

  // Main entry. V0in: certified balls of V_y (index i*d+j, y != 0) at low precision. Returns false
  // if the certification fails (the caller then falls back to the quadrature table). On success
  // Vout holds certified balls of all V_y with relative accuracy at least `target` bits. Pivoted
  // Gram-Schmidt on the rows of J (R x n2) with row and column equilibration, in the real type Rt
  // (long double or __float128); on success S holds the chosen equations and cond_est the ratio of
  // the extreme pivots.
  template <class Rt>
  struct ComplexBall {
    Rt re, im;
  };
  template <class Rt>
  static Rt to_real(const arf_t x);
  template <class Rt>
  static Rt rsqrt(Rt x);
  template <class Rt>
  bool select_rows(const acb_mat_t J, long R, long n2, const std::vector<long>& rowsy);
  // S_hint: a square subsystem (and its condition estimate) from an earlier attempt on the same
  // ghost, reused when the seed is recomputed at a higher precision (the selection depends only on
  // the seed's midpoint)
  std::vector<long> S_hint;
  double cond_hint = 0;
  SelectionKind hint_kind = SelectionKind::native;
  slong inverse_precision_hint = 0;
  bool has_complete_selection() const;
  bool selection_normalization_resolved(const std::vector<acb_struct>& point, slong precision,
                                        bool verbose) const;
  // Finite point proposal only. The caller retains B0/B1 and commits once.
  bool refined_correction(sic::support::ComplexBallVector& proposal, const acb_mat_t inverse,
                          const std::vector<acb_struct>& F, const std::vector<acb_struct>& point,
                          const Ctx& high, slong precision, int passes,
                          sic::support::resources::Bytes available,
                          double* jacobian_seconds = nullptr,
                          double* inner_seconds = nullptr) const;

  bool refine(const std::vector<acb_struct>& V0in, slong target, std::vector<acb_struct>& Vout,
              std::string& why);

  bool refine_impl(const std::vector<acb_struct>& V0in, slong target, std::vector<acb_struct>& Vout,
                   std::string& why, bool short_inverse, bool& used_short_inverse);
};

}  // namespace sic::exact

#include "exact/certification/reconstruction/ghost_equations.inl.hpp"
