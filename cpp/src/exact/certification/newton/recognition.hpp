// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/arb.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace sic::exact {
struct RecognitionLattice {
  slong k, r, wp;
  double log2Bp;
  arb_t om, omc;
  arb_t lim, dl;  // 2^B and 2^{-B-7}, B = ceil(log2Bp)
  arf_t al1,
      al2;        // Babai coefficients as real multipliers: c1 = bt al1, c2 = bt al2 (then rounded)
  slong lal = 0;  // log2 bound of |al1|, |al2|
  fmpz_t u11, u12, u21, u22, b1x, b1y, b2x, b2y;
  fmpz_t detz;
  bool detneg = false;
  RecognitionLattice(double l2Bp, const arb_t sq0, slong wp_);
  ~RecognitionLattice();
  void toint(fmpz_t out, const arb_t v) const;
  // Candidate (x, y) by Babai rounding in floating point (the exact rounding is not needed: up to
  // +-2 in each coefficient is searched, and any candidate is accepted only after the certified
  // check |x + y omega - bt| < 2^{-B-7} and |x + y omega'| <= 2^B, B = ceil(log2Bp), which
  // determines it uniquely). x + y omega' = (2x + y) - (x + y omega), as
  // omega + omega' = 1. reusable temporaries (one per thread): avoids allocating big integers and
  // balls for every leaf
  struct Work {
    fmpz_t r1, r2, k1, k2, xx, yy, tr;
    arb_t val, valc, del;
    arf_t c;
    Work();
    ~Work();
    Work(const Work&) = delete;
    Work& operator=(const Work&) = delete;
  };
  bool recognize(const arb_t bt, fmpz_t Xout, fmpz_t Yout) const {
    Work w;
    return recognize(bt, Xout, Yout, w);
  }
  bool recognize(const arb_t bt, fmpz_t Xout, fmpz_t Yout, Work& w) const;
};
}  // namespace sic::exact
