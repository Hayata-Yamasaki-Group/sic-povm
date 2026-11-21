// SPDX-License-Identifier: AGPL-3.0-or-later
// One active real component per analytic quadratic sign pair. Physical
// conjugates below the top remain complex and use separate storage.
#pragma once

#include <functional>
#include <stdexcept>

#include "exact/storage/ball_file.hpp"
#include "sic/support/ball_vectors.hpp"
#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace sic::exact {

int eighth_character(long norm);

struct AnalyticGenerator {
  mutable sic::support::ComplexBallVector full;
  mutable sic::support::RealBallVector scalar;
  sic::support::RealBallVector periodic;
  std::function<size_t(long)> periodic_index;
  std::shared_ptr<BallFile> backing;
  std::vector<std::shared_ptr<BallFile>> pieces;
  size_t piece_size = 0;
  mutable size_t resident_first = 0;
  sic::support::ComplexBallVector root;
  long count = 0;
  bool paired = false, imaginary = false, character = false;
  std::function<int(long)> sign;

  void assign(sic::support::ComplexBallVector&& values, bool quadratic, bool realtail, bool top);

  void assign_scalar(sic::support::RealBallVector&& values, bool top);

  size_t size() const { return count; }

  void assign_periodic(sic::support::RealBallVector&& values, long logical_count,
                       std::function<size_t(long)> index);

  void get(acb_ptr out, long index) const;

  // Borrowed Arb view: owner must remain alive and unchanged. load/unload/park,
  // spill, assignment and destruction invalidate it. Read only while the owner
  // thread has frozen this generator for a worker phase; never retain across calls.
  arb_srcptr real_even(long pair) const;

  void imaginary_even(arb_ptr out, long pair) const;

  // The caller verifies the selected zeta_8 identity and exact label character.
  // The supplied isolated root retains the analytic branch.
  void assign_character(acb_srcptr root_value, std::function<int(long)> signs);

  // Call before/after a parallel transform level; workers only read the
  // resident array. The immutable backing preserves every Arb bit exactly.
  void prepare() const;
  void prepare_piece(size_t first, size_t size) const;
  void park() const;
  void spill(size_t block_size = 262144);
};

}  // namespace sic::exact
