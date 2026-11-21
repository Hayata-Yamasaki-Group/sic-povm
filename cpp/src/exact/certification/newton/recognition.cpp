// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/newton/recognition.hpp"

#include "sic/error.hpp"

namespace sic::exact {
RecognitionLattice::RecognitionLattice(double l2Bp, const arb_t sq0, slong wp_)
    : wp(wp_), log2Bp(l2Bp) {
  if (!std::isfinite(log2Bp) || log2Bp < 0 || log2Bp != std::floor(log2Bp) ||
      log2Bp >= std::min(std::ldexp(1.0, 50), static_cast<double>(WORD_MAX / 16)))
    throw sic::Error(sic::ErrorKind::unsupported,
                     "recognition requires a supported certified integer height exponent");
  k = (slong)std::ceil(2 * log2Bp) + 8;
  r = std::max<slong>(k + 64, wp - k / 2 + 72);
  arb_init(om);
  arb_init(omc);
  arb_init(lim);
  arb_init(dl);
  arb_one(lim);
  arb_mul_2exp_si(lim, lim, (slong)std::ceil(log2Bp));
  arb_one(dl);
  arb_mul_2exp_si(dl, dl, -(slong)std::ceil(log2Bp) - 7);
  arb_add_ui(om, sq0, 1, wp);
  arb_mul_2exp_si(om, om, -1);
  arb_sub_ui(omc, sq0, 1, wp);
  arb_neg(omc, omc);
  arb_mul_2exp_si(omc, omc, -1);
  fmpz_init_set_ui(u11, 1);
  fmpz_init(u12);
  fmpz_init(u21);
  fmpz_init_set_ui(u22, 1);
  fmpz_init(b1x);
  fmpz_init(b1y);
  fmpz_init(b2x);
  fmpz_init(b2y);
  arb_t tmp;
  arb_init(tmp);
  arb_one(tmp);
  arb_mul_2exp_si(tmp, tmp, k);
  toint(b1x, tmp);
  arb_one(tmp);
  toint(b1y, tmp);
  arb_mul_2exp_si(tmp, om, k);
  toint(b2x, tmp);
  toint(b2y, omc);
  arb_clear(tmp);
  fmpz_t n1, n2, ip, q, tz;
  fmpz_init(n1);
  fmpz_init(n2);
  fmpz_init(ip);
  fmpz_init(q);
  fmpz_init(tz);
  auto dot = [&](fmpz_t res, const fmpz_t ax, const fmpz_t ay, const fmpz_t bx, const fmpz_t by) {
    fmpz_mul(res, ax, bx);
    fmpz_addmul(res, ay, by);
  };
  for (int it = 0; it < 100000; ++it) {
    dot(n1, b1x, b1y, b1x, b1y);
    dot(n2, b2x, b2y, b2x, b2y);
    if (fmpz_cmp(n2, n1) < 0) {
      fmpz_swap(b1x, b2x);
      fmpz_swap(b1y, b2y);
      fmpz_swap(u11, u21);
      fmpz_swap(u12, u22);
      fmpz_swap(n1, n2);
    }
    dot(ip, b1x, b1y, b2x, b2y);
    fmpz_mul_2exp(tz, ip, 1);
    fmpz_add(tz, tz, n1);
    fmpz_mul_2exp(q, n1, 1);
    fmpz_fdiv_q(q, tz, q);
    if (fmpz_is_zero(q)) break;
    fmpz_submul(b2x, q, b1x);
    fmpz_submul(b2y, q, b1y);
    fmpz_submul(u21, q, u11);
    fmpz_submul(u22, q, u12);
  }
  fmpz_clear(n1);
  fmpz_clear(n2);
  fmpz_clear(ip);
  fmpz_clear(q);
  fmpz_clear(tz);
  fmpz_init(detz);
  fmpz_mul(detz, b1x, b2y);
  fmpz_submul(detz, b2x, b1y);
  if (fmpz_sgn(detz) < 0) {
    detneg = true;
    fmpz_neg(detz, detz);
  }
  arf_init(al1);
  arf_init(al2);
  {
    const slong pa = wp + 64;
    arb_t num, den;
    arb_init(num);
    arb_init(den);
    arb_set_fmpz(den, detz);
    if (detneg) arb_neg(den, den);
    arb_set_fmpz(num, b2y);
    arb_mul_2exp_si(num, num, k + r);
    arb_div(num, num, den, pa);
    arf_set(al1, arb_midref(num));
    arb_set_fmpz(num, b1y);
    arb_neg(num, num);
    arb_mul_2exp_si(num, num, k + r);
    arb_div(num, num, den, pa);
    arf_set(al2, arb_midref(num));
    arb_clear(num);
    arb_clear(den);
    for (arf_srcptr a : {(arf_srcptr)al1, (arf_srcptr)al2})
      if (!arf_is_zero(a)) lal = std::max(lal, arf_abs_bound_lt_2exp_si(a));
  }
}

RecognitionLattice::~RecognitionLattice() {
  arb_clear(om);
  arb_clear(omc);
  arb_clear(lim);
  arb_clear(dl);
  arf_clear(al1);
  arf_clear(al2);
  fmpz_clear(u11);
  fmpz_clear(u12);
  fmpz_clear(u21);
  fmpz_clear(u22);
  fmpz_clear(b1x);
  fmpz_clear(b1y);
  fmpz_clear(b2x);
  fmpz_clear(b2y);
  fmpz_clear(detz);
}

void RecognitionLattice::toint(fmpz_t out, const arb_t v) const {
  arf_t a;
  arf_init(a);
  arf_mul_2exp_si(a, arb_midref(v), r);
  arf_get_fmpz(out, a, ARF_RND_NEAR);
  arf_clear(a);
}

RecognitionLattice::Work::Work() {
  fmpz_init(r1);
  fmpz_init(r2);
  fmpz_init(k1);
  fmpz_init(k2);
  fmpz_init(xx);
  fmpz_init(yy);
  fmpz_init(tr);
  arb_init(val);
  arb_init(valc);
  arb_init(del);
  arf_init(c);
}

RecognitionLattice::Work::~Work() {
  fmpz_clear(r1);
  fmpz_clear(r2);
  fmpz_clear(k1);
  fmpz_clear(k2);
  fmpz_clear(xx);
  fmpz_clear(yy);
  fmpz_clear(tr);
  arb_clear(val);
  arb_clear(valc);
  arb_clear(del);
  arf_clear(c);
}

bool RecognitionLattice::recognize(const arb_t bt, fmpz_t Xout, fmpz_t Yout, Work& w) const {
  fmpz* r1 = w.r1;
  fmpz* r2 = w.r2;
  fmpz* k1 = w.k1;
  fmpz* k2 = w.k2;
  fmpz* xx = w.xx;
  fmpz* yy = w.yy;
  fmpz* tr = w.tr;
  {
    // the coefficients have about log2|bt| - k/2 bits; compute them with 64 guard bits
    slong lb = arf_is_zero(arb_midref(bt)) ? 0 : arf_abs_bound_lt_2exp_si(arb_midref(bt));
    slong pc = std::max<slong>(64, lb + std::max<slong>(0, lal) + 64);
    arf_mul(w.c, arb_midref(bt), al1, pc, ARF_RND_NEAR);
    arf_get_fmpz(r1, w.c, ARF_RND_NEAR);
    arf_mul(w.c, arb_midref(bt), al2, pc, ARF_RND_NEAR);
    arf_get_fmpz(r2, w.c, ARF_RND_NEAR);
  }
  arb_ptr val = w.val, valc = w.valc, del = w.del;
  bool found = false;
  static const int ord[25][2] = {{0, 0},  {1, 0},   {-1, 0},  {0, 1},  {0, -1}, {1, 1},  {1, -1},
                                 {-1, 1}, {-1, -1}, {2, 0},   {-2, 0}, {0, 2},  {0, -2}, {2, 1},
                                 {2, -1}, {-2, 1},  {-2, -1}, {1, 2},  {1, -2}, {-1, 2}, {-1, -2},
                                 {2, 2},  {2, -2},  {-2, 2},  {-2, -2}};
  for (int q = 0; q < 25 && !found; ++q) {
    fmpz_add_si(k1, r1, ord[q][0]);
    fmpz_add_si(k2, r2, ord[q][1]);
    fmpz_mul(xx, k1, u11);
    fmpz_addmul(xx, k2, u21);
    fmpz_mul(yy, k1, u12);
    fmpz_addmul(yy, k2, u22);
    arb_set_fmpz(val, yy);
    arb_mul(val, val, om, wp);
    arb_add_fmpz(val, val, xx, wp);
    arb_sub(del, val, bt, wp);
    arb_abs(del, del);
    if (!arb_lt(del, dl)) continue;
    fmpz_mul_2exp(tr, xx, 1);
    fmpz_add(tr, tr, yy);
    arb_set_fmpz(valc, tr);
    arb_sub(valc, valc, val, wp);
    arb_abs(valc, valc);
    if (arb_le(valc, lim)) {
      fmpz_set(Xout, xx);
      fmpz_set(Yout, yy);
      found = true;
    }
  }
  return found;
}

}  // namespace sic::exact
