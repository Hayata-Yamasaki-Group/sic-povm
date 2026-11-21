// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::decode::kernels {}  // namespace sic::decode::kernels
namespace sic::decode {
inline void LeafConverter::operator()(acb_t out, const fmpz_t x, const fmpz_t y) {
  if (imaginary_) {
    arb_set_fmpz(a_, y);
    arb_mul_2exp_si(a_, a_, -1);
    arb_add_fmpz(a_, a_, x, cp_);
    arb_set_round(acb_realref(out), a_, prec_);
    arb_mul_fmpz(a_, w_, y, cp_);
    arb_set_round(acb_imagref(out), a_, prec_);
    return;
  }
  arb_mul_fmpz(a_, w_, y, cp_);
  arb_add_fmpz(a_, a_, x, cp_);
  arb_set_round(acb_realref(out), a_, prec_);
  arb_zero(acb_imagref(out));
}

}  // namespace sic::decode
