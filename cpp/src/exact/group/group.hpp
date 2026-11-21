// SPDX-License-Identifier: AGPL-3.0-or-later
// The group G' of Artin actions on table points, its abelian structure (Smith form) and characters.
//
// Memory-light representation: only the generators carry full permutations of the points. G' is
// abelian, so an element is determined by its images of one base point per G'-orbit (together with
// its action on zeta_{2d} and sqrt(eps)); elements are stored by those images and indexed by the
// mixed-radix value of their coordinates in prod Z/n_l, so that multiplication is digit-wise
// addition.
#pragma once
#include <flint/fmpz_mat.h>

#include <functional>
#include <limits>
#include <numeric>
#include <unordered_map>

#include "exact/arithmetic/arith.hpp"
#include "support/index.hpp"
#include "support/resources.hpp"

namespace sic::exact {

struct GElem {
  std::vector<sic::support::Index> perm;  // action on points X = {(c, i, j)}: index c*d^2 + i*d + j
                                          // (images are eps-orbit representatives)
  long n;                                 // action on zeta_{2d}: zeta -> zeta^n
  int chi;  // action on sqrt(eps): +-1 (also the global sign on table values)
};

long kronecker_si(long a, long n);

#ifdef SIC_ENABLE_TEST_HOOKS
// Optional owned snapshots for checking discovery order before Smith reindexing.
struct GroupClosureTrace {
  std::vector<sic::support::Index> images, exponents, neighbours, destination;
  std::vector<long> norms;
  std::vector<int> signs;
  std::vector<std::vector<long>> relations, smith, transform;
  size_t max_level_tiles = 0;
  // Called around a stride stop check in the expanding team, before/after polling.
  std::function<void(size_t, int, bool)> worker_stop_check;
};
#endif

struct Group {
  const Arithmetic& ar;
  long d, npts;
  long D1;  // fundamental discriminant of Q(sqrt(d+1)), 1 if square
  std::vector<GElem> gens;

  bool orbit_ready = false;
  std::vector<sic::support::Index>
      orbit;  // canonical representative of the eps-orbit of each point

  // structure (after close())
  std::vector<long> invf;                 // invariant factors n_l (> 1)
  std::vector<long> stride;               // mixed-radix strides: index = sum_l coord_l * stride_l
  long N = 0;                             // |G'|
  std::vector<sic::support::Index> base;  // one canonical point per G'-orbit
  std::vector<sic::support::Index> img;   // img[g * nb + b] = g(base[b])
  std::vector<long> nn;                   // nn[g]: zeta_{2d} -> zeta_{2d}^nn
  std::vector<int> chi;                   // chi[g] = +-1
  std::vector<sic::support::Index> rep_elem,
      rep_base;  // for a canonical point x: x = rep_elem[x](base[rep_base[x]])
#ifdef SIC_ENABLE_TEST_HOOKS
  GroupClosureTrace* closure_trace = nullptr;  // borrowed only during close()
#endif

  explicit Group(const Arithmetic& a);

  int chi_of_norm(long Nm) const;

  // Deterministic generator specification, kept with the group source so
  // checkpoint identities cover every choice used to construct the action.
  void generate_standard();

  long pidx(sic::support::Index c, long i, long j) const {
    return (long)c * d * d + ((i % d + d) % d) * d + ((j % d + d) % d);
  }

  // The ghost uses (i epsilon+j)/d, whereas the maximal-order table uses
  // (i tau+j)/d. epsilon = order_index*tau + (t+order_index*B)/2.
  sic::support::Index ghost_point(long i, long j) const {
    const long shift = (ar.t + ar.order_index * ar.cls[0].cycle[0].B) / 2;
    return orbit[pidx(0, ar.order_index * i, j + shift * i)];
  }

  // point map x -> m * x where m in K maps J_c to J_{c2}. The map is linear: x = (i tau_c + j)/d
  // goes to u tau_{c2} + v with (u, v) = i (u_tau, v_tau) + j (u_1, v_1), the coordinates of m
  // tau_c / d and m / d, which lie in (1/d)Z; so the residues are (i U_tau + j U_1, i V_tau + j
  // V_1) mod d.
  void fill_map(GElem& g, sic::support::Index c, sic::support::Index c2,
                const QuadraticNumber& m) const;

  GElem principal_gen(long a, long b) const;

  GElem prime_gen(long p) const;

  // ---------------- group operations on indices ----------------
  long digit(long g, size_t l) const { return (g / stride[l]) % invf[l]; }
  std::vector<long> coord(long g) const;
  long index_of(const std::vector<long>& c) const;
  long mul(long a, long b) const;
  long pow(long a, long k) const;
  long order(long a) const;
  size_t nb() const { return base.size(); }
  // index of the element acting like g (linear scan; used for a few elements only)
  long element_of(const GElem& g) const;
  // image of a canonical point x under g
  sic::support::Index act(long g, sic::support::Index x) const {
    return img[mul(g, rep_elem[x]) * (long)nb() + rep_base[x]];
  }
  size_t size() const { return (size_t)N; }

  // ---------------- closure ----------------
  void close(int workers, long element_limit = std::numeric_limits<sic::support::Index>::max(),
             const std::function<void(size_t, size_t)>& before_bfs = {},
             const std::function<void(size_t)>& progress = {});

  // index of the generator gi (used only for the consistency check)
  long index_of_gen(size_t gi, const std::vector<std::vector<long>>& V,
                    const std::vector<size_t>& keep) const;

  // Smith normal form of the k x k relation matrix M (rows = relations); V tracks column
  // operations, and the coordinates of an exponent vector e are e V mod n_l.
  static void smith(std::vector<std::vector<long>>& M, std::vector<std::vector<long>>& V);
};

}  // namespace sic::exact
