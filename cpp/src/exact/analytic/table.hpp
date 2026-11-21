// SPDX-License-Identifier: AGPL-3.0-or-later
// Table of special values E_{J_c, eps^3}(x) for x in (1/d) J_c / J_c, x != 0.
//  - production: the reduced-coordinate product, with
//  all Faddeev
//    factors of one cycle state computed together (batch_state);
//  - validation: per-point acb_calc integrals (factor) and the single-block formula of [RW, Prop.
//  2(i)]
//    with the eta multiplier via Dedekind sums (block_value).
#pragma once
#include <flint/arb_hypgeom.h>
#include <flint/arb_mat.h>
#include <flint/arb_poly.h>
#include <flint/fmpq.h>

#include <chrono>
#include <cstdlib>
#include <map>
#include <mutex>
#include <stdexcept>
#include <tuple>

#include "exact/analytic/table/faddeev.hpp"
#include "exact/analytic/table/table_view.hpp"
#include "exact/arithmetic/arith.hpp"
#include "exact/certification/bounds/quadrature_bounds.hpp"
#include "exact/group/cycle_labels.hpp"
#include "exact/group/orbit_table.hpp"
#include "exact/runtime/parallel.hpp"
#include "sic/support/ball_vectors.hpp"
#include "sic/support/flint_raii.hpp"
#include "sic/support/kernel_settings.hpp"
#include "support/arithmetic/balanced_product.hpp"
#include "support/resources.hpp"

namespace sic::exact {

struct Table {
  const Arithmetic& ar;
  slong prec;
  long d;
  int nthreads = 1;
  bool stream_states = true;
  long node_slab = 512;                      // maximum live quadrature-node columns; 0: reference
  std::vector<std::vector<acb_struct>> val;  // val[c][i*d+j]
  std::shared_ptr<OrbitTableLayout> layout;
  std::vector<sic::support::ComplexBallVector> packed;
  bool paired_values = false;
  bool cached_enclosures = false;
  sic::support::ComplexBallVector zero_value{1};
  std::function<void(sic::support::resources::Bytes)> admit_workspace;
  std::function<void(sic::support::Index)> class_completed;
  std::atomic<sic::support::resources::Bytes> kernel_peak{0};
  std::atomic<long> maximum_nodes{0};
  // cycle states: sid = soff[c] + l
  std::vector<sic::support::Index> soff, s_c, s_l;
  // per state and grid point (x, y), x in [1, d], y in [0, d), index (x-1)*d + y:
  //   Phi_S((x tau + y)/d - 1) = exp(L) * S,  L = log Phi_S(z0) (quadrature, with its error bound),
  //   S = product of shift factors from z0 (centre of the strip) to z.
  using SData = TableStateData;
  std::vector<SData> sd;
  long n_integrals = 0;  // number of (state, point) values
  long n_nodes_total = 0;
  TableRuleCache gl_cache;  // (n, prec) -> rule
  mutable std::mutex gl_mutex;

  TableView view() {
    return {ar,  prec,     d,        nthreads,        node_slab,   sd,           s_c,
            s_l, gl_cache, gl_mutex, admit_workspace, kernel_peak, maximum_nodes};
  }
  Table(const Arithmetic& a, slong p, int nth = 1);
  ~Table();
  sic::support::Index nstates() const { return sic::support::checked_index(s_c.size()); }

  void enable_compact(const std::shared_ptr<OrbitTableLayout>& labels);
  bool compact() const { return (bool)layout; }
  struct Reference {
    acb_srcptr value;
    bool conjugate;
  };
  Reference reference(long point) const;
  void read(acb_ptr out, long point) const;
  acb_ptr writable(long point);
  template <class Function>
  void for_each_value(const Function& function);
  void pack_class(sic::support::Index c);

  // Gauss-Legendre rule on [-1, 1] with n nodes (cached; thread-safe).
  const std::vector<QuadratureNode>& gl_rule(long n, slong wp);

  // Heuristics propose panel geometry and node counts. ContourBounds then
  // proves the pole exclusions and error budget using the actual Arb b and
  // exact dyadic contour height/endpoints. Only the resulting mag is used.
  double gl_nodes(std::vector<QuadratureNode>& nodes, const arb_t b, const arb_t eta, slong wp,
                  double umax, mag_t error, const std::function<void(size_t)>& admit_nodes = {});

  sic::support::resources::Bytes kernel_bytes(size_t nodes, long rows, size_t differences,
                                              slong wp) const;

  // exp(pi i k / 4)
  static void eighth_root(acb_t r, long k, slong p);

  // ---------------- batched evaluation of one cycle state ----------------
  // For the state (c, l) with tau = tau_{c,l}, b = sqrt(tau), centre cen = (tau - 1)/2, the grid
  // arguments are z = (x tau + y)/d - 1, x in [1, d], y in [0, d). Write z = z0 + k with k integer
  // and z0 = cen + u, |u| <= 1/2:
  //   log Phi_S(z0) = int_{Im w = eta} G(w) e^{2 w u / b} dw  ~  sum_k c_k e^{2 w_k t / b},  t = z0
  //   + 1,
  // with c_k = weight_k G(w_k) e^{-2 w_k (1 + cen)/b} = weight_k / (w_k (E1^2 - 1)(E2^2 - 1)), E1 =
  // e^{b w}, E2 = e^{w/b}. Row x uses Y = Ymin(x) + j, j in [0, d), with u = (x tau + Y)/d - 1 -
  // cen in [-1/2, 1/2): the values for all (x, j) are the matrix product A B, A[x][k] = c_k e^{2
  // w_k t_x / b}, B[k][j] = Q_k^j, Q_k = e^{2 w_k/(d b)}. Rows x = 1..d/2 and x = d are computed;
  // rows d - x follow from the reflection (x, y) -> (d - x, -y), u -> -u, and the residue identity
  // I(u) + I(-u) = -pi i u^2 / tau + pi i (tau + 1/tau) / 12.
  void batch_state(sic::support::Index sid);

  // Phi_S at state sid and grid point (x, y), from the batched data
  void state_factor(acb_t res, sic::support::Index sid, long x, long y, slong p) const;

  // reduced-coordinate product for point (i, j) of class c:  mu * prod_{l < 3r} Phi_{l mod r}(x_l,
  // y_l)
  void reduced_value(acb_t res, sic::support::Index c, long i, long j) const;

  // Class by class: the states of one class are computed (in parallel), its values formed by the
  // reduced-coordinate product, and the state data freed. Peak memory is that of the largest cycle
  // instead of all states.
  void build_batched();
  // the values of the classes with want[c] only (the others stay zero)
  void build_selected(const std::vector<char>& want);
  // Allocate only the requested class for isolated seeds and retries. Full
  // tables still use alloc_values(), including zero-filled derived classes.
  void build_class(sic::support::Index c);
  void build_one_class(sic::support::Index c);

  // Reorder the same 3r factors by state. The visits of state l are
  // P_l M^k (i,j), k=0,1,2, where P_l=A_{l-1}...A_0 and M=P_r.
  // Addition and multiplication commute in C; Arb encloses the regrouped
  // expression without choosing logarithm branches for the shift factors.
  // Only the current L/S grids and two accumulators are retained.
  void build_one_class_streamed(sic::support::Index c);

  // allocate an empty value table (filled by other means, e.g. Newton refinement of the ghosts)
  void alloc_values();

  // free the per-state data once the table is built
  void release_states();

  // ---------------- validation: single block formula ----------------
  void block_value(acb_t res, sic::support::Index c, long i, long j, slong p);
};

}  // namespace sic::exact

#include "exact/analytic/table.inl.hpp"
#include "exact/analytic/table/table_values.tpp"
