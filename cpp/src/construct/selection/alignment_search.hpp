// SPDX-License-Identifier: AGPL-3.0-or-later
// Canonical conjugate alignment:
// spike-aware conjugate alignment of the quadratic tower generators.
//
// Full max-product model: a leaf
//   lambda_e(x) = Tr_{F_L/K}(x g^e),   e = (e_0, ..., e_{L-1}),  e_i < p_i,
// of an element x of the level-L field has size about
//   A(e) = max_c [ l_x(c) + sum_i e_i l_{g_i}(c) ],   l = log2|.|,
// the maximum over the embeddings c of F_L above the computed embedding of K (the producer's
// coset labels at level L). The other embedding of K carries small values (the producer's certified
// bound Cg); bare conjugates permute them, so they are not modelled here.
//
// Admissible changes: for a quadratic step with
// sigma_s(w_s) = -w_s, every conjugate tau(w_s), tau in G = Gal(L/K) (abelian), is again
// trace-zero over F_s, equals b w_s with b in F_s, generates F_{s+1} over F_s and keeps the
// stabilizer bands. Here only bare conjugates are candidates (GaloisCombination with one term,
// a0 = 1, a1 = 0); they also keep the producer's certified conjugate bound Cg and the side
// maximum log2U, so every certified bound downstream is unchanged.
//
// One-sided search: coordinate descent over the searchable levels,
// candidates = all conjugates up to sign when [F_s:K] <= cand, otherwise the identity, the
// translations from the largest position to the 512 next largest, and random conjugates up to
// cand; score = weighted sum over sampled training leaves of max(0, A(e)); a level moves only if
// the score decreases. The model ignores the tower/action elements, the constant factors of the
// channels and the 2-adic shifts J (offsets that matter only through the clip at 0).
// The result is a description (the Galois element per level), certified and evaluated by the
// producer at every precision. Deterministic: fixed seeds, per-candidate sequential sums, ties
// to the lowest index; independent of the thread count.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "construct/model/alignment_options.hpp"
#include "exact/runtime/parallel.hpp"
#include "sic/error.hpp"
#include "support/index.hpp"
#include "support/resources.hpp"

namespace sic::construct::alignment {

struct AlignInput {
  int t = 0;               // tower levels
  std::vector<long> Ncos;  // [F_i : K], size t + 1
  std::vector<long> ps;    // step degrees
  // lg[i]: log2|g_i| at the level-(i+1) coset labels (size Ncos[i+1]); empty: level not modelled
  // (constant magnitude, e.g. the top step, or not part of any modelled leaf)
  std::vector<std::vector<float>> lg;
  std::vector<char> searchable;  // quadratic levels whose generator may become a conjugate
  struct Rep {
    int level = 0;          // the element lies in F_level; leaves have digits at levels < level
    std::vector<float> lx;  // log2|x| at the level-`level` coset labels
    std::string name;
  };
  std::vector<Rep> reps;
  // perm(s, v, out): out[u] = coset_s(rep_s(u) * rep_s(v)) for u < Ncos[s], i.e. the pair label of
  // the conjugate tau_v(w_s) at pair u is the pair label of w_s at out[u].
  std::function<void(int, long, std::vector<sic::support::Index>&)> perm;
  uint64_t seed = kAlignmentSeed;
  size_t train = kAlignmentTrain;  // training leaves per (representative, channel)
  size_t eval = 64;                // independent fixed diagnostic evaluation sample count
  size_t cand = kAlignmentCandidates;
  int sweeps = kAlignmentSweeps;  // fixed canonical sweep count
  int threads = 1;
  // Resource policy only: changing these limits must not change a single score or choice.
  std::function<void(int&, const std::function<void()>&)>
      rebalance;                         // quiescent search boundaries: evict F, lower workers
  size_t cache_bytes = size_t{1} << 30;  // retain F only when the entire table fits
  size_t batch_bytes = kAlignmentBatchBytes;
  size_t worker_bytes = kAlignmentWorkerBytes;
  size_t tile = 65536;  // floats per reconstruction/evaluation worker
  std::function<void(const char*)> trace;
};

struct AlignMemory {
  uint64_t inputs = 0, cache = 0, journal = 0, generators = 0;
  uint64_t batch = 0, candidate = 0, workers = 0, metadata = 0, workspace = 0;
};

namespace detail {
uint64_t add(uint64_t a, uint64_t b);
uint64_t mul(uint64_t a, uint64_t b);
int workers(long tasks, int requested, uint64_t per_worker, uint64_t cap);
}  // namespace detail

// Shape-only forecast: lx/lg need not be allocated yet. Includes retained inputs separately
// from search workspace. Deliberately sums mutually exclusive phases and reserves allocator,
// log/handle overhead and 8 MiB stacks per requested worker. Not a ball-arithmetic bound.
AlignMemory align_memory(const AlignInput& in);

struct AlignResult {
  std::vector<long> choice;  // per level: pair label v of the chosen conjugate (0 = identity)
  std::vector<double>
      level_gain;  // model change (bits per sampled training pair) per accepted move
  double eval_before = 0, eval_after = 0;  // weighted mean model bits per leaf (evaluation set)
  double eval_max_before = 0, eval_max_after = 0;
  double seconds = 0;
  std::string log;
};

namespace detail {
template <class F>
void par(long n, int threads, F&& body);
}  // namespace detail

AlignResult align_search(const AlignInput& in);

}  // namespace sic::construct::alignment

#include "construct/selection/alignment_search.tpp"
