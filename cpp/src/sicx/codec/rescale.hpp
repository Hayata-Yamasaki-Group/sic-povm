// SPDX-License-Identifier: AGPL-3.0-or-later
// Canonical 2-adic generator rescaling of SICX (docs/sicx.md section 3.7 for the
// quadratic and general cases).
//
// Every tower step F_{s+1} = F_s(g_s) uses the generator g_s' = 2^{-J_s} g_s (J_s >= 0 an integer)
// of the producer's g_s. Since 2^{-J_s} is rational it is fixed by every automorphism, so every
// stored object changes by an exact power of two per leaf. With p = p_s and C = p(p-1)/2:
//   * an element x of F_n stored as the same field element (rep; and the lower-level part of every
//   other
//     element) has new leaves lambda'_e(x) = Tr(x prod_i g_i'^{e_i}) = 2^{-sum_{i<n} e_i J_i}
//     lambda_e(x);
//   * the element of its own level s changes as follows ("own weight" w, leaf factor 2^{-w J_s}):
//       wsq s:        c' = g'^2 = 4^{-J} c                                          w = 2
//       act s l:      A' = tau_l(g') g' = 4^{-J} A                                  w = 2
//       fcoef s k:    g' is a root of X^p + sum_k a_k 2^{-J(p-k)} X^k               w = p - k
//       actdelta s:   delta' = prod_{j<j'} (v'_j' - v'_j) = 2^{-J C} delta             w = C
//                     (Vandermonde of the conjugates v_j of g, as the producer defines it)
//       actodd s l k: tau_l(g') = sum_k a_k 2^{J(k-1)} g'^k, so (delta a_k)' = 2^{-J(C+1-k)} delta
//       a_k
//                                                                                    w = C + 1 - k
//                                                                                    (>= 1)
//   * the branch rectangle of level s is multiplied by 2^{-J_s} (exact exponent shift of the Arb
//   balls):
//     the roots of the new defining polynomial are the old roots times 2^{-J_s}.
// All weights are >= 0, so a choice J is admissible iff every leaf stays integral.
//
// The rule (identical in every route, because it is a function of the stored leaves only): for
// every nonzero leaf of every stored element, let a in N^t be its weight vector (its digits e_i at
// the levels below the element's field level, plus the own weight w at its own level) and v its
// 2-adic valuation (min of v_2(x), v_2(y) over the nonzero coordinates of x + y omega). J must
// satisfy a.J <= v for every leaf. The levels s with gain_s = sum over leaves of a_s > 0 are
// ordered by decreasing gain_s (ties: smaller s first); in that order J_s is set to the largest
// value keeping a.J <= v for every leaf, given the earlier choices (later levels still 0). Levels
// with gain 0 get J_s = 0. On quadratic levels this is equivalent to raising the feasible level of
// largest gain by one until none is feasible: gains are fixed and feasibility only decreases.
#pragma once
#include <flint/arb.h>

#include <atomic>
#include <limits>
#include <thread>

#include "sic/sicx/rescaling_rule.hpp"
#include "sic/support/flint_raii.hpp"
#include "sicx/codec/block.hpp"

namespace sic::sicx {

// The leaf stream of the element after rescaling: each leaf divided exactly by
// 2^(sum_{i<n} e_i J_i + w J_n), w the own weight. A leaf that is not divisible is a hard failure
// (it cannot happen for J computed from the same leaves).
LeafSource rescaled_source(const Meta& m, const ElementId& id, LeafSource source,
                           const GeneratorRescaling& r);

// Branch rectangles of the rescaled generators: exact multiplication by 2^{-J_s}.
void rescale_branches(Meta& m, const GeneratorRescaling& r);

// ------------------------------------------------ the rule applied to a written file
// The leaf stream of a stored element: full leaves lambda = u c at full positions.

// Shared block decoder for complete Readers and authenticated durable resume spools.

ElementId element_id_of(const Meta& meta, const ElementInfo& e);

// The rule applied to the stored data of a file. A file in canonical form gives J = 0 at every
// level: after the greedy, no level with positive gain can be raised by one (feasibility only
// decreases), and rescaling keeps the nonzero pattern and hence the gains.

}  // namespace sic::sicx
