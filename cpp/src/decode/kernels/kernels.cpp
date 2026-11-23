// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decode/kernels/kernels.hpp"

#include "sic/error.hpp"

namespace sic::decode::kernels {
void precision(slong bits) {
  if (bits < 64 || bits > sic::kNativeMaximumPrecision)
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "prime transport requires supported native precision");
  if (flint_get_num_threads() != 1)
    throw sic::Error(sic::ErrorKind::invalid_argument, "prime transport requires one FLINT worker");
}

void bounded(acb_srcptr value, slong bits) {
  if (!acb_is_finite(value) || !ARB_IS_LAGOM(acb_realref(value)) ||
      !ARB_IS_LAGOM(acb_imagref(value)))
    throw sic::Error(sic::ErrorKind::invalid_format,
                     "nonfinite or unsupported prime-transport input");
  const auto maximum = sic::support::resources::add(sic::support::resources::mul(4, bits), 1024);
  if (static_cast<Count>(arf_bits(arb_midref(acb_realref(value)))) > maximum ||
      static_cast<Count>(arf_bits(arb_midref(acb_imagref(value)))) > maximum)
    throw sic::Error(sic::ErrorKind::resource,
                     "prime-transport input mantissa exceeds explicit limit");
}

Polynomial::Polynomial() { acb_poly_init(value_); }

unsigned monic_degree(const Polynomial& polynomial, slong bits) {
  precision(bits);
  const slong degree = acb_poly_degree(polynomial.get());
  if (degree < 2 || degree > 31 || !acb_is_one(acb_poly_get_coeff_ptr(polynomial.get(), degree)))
    throw sic::Error(sic::ErrorKind::invalid_format, "expected bounded monic defining polynomial");
  for (slong k = 0; k <= degree; ++k) bounded(acb_poly_get_coeff_ptr(polynomial.get(), k), bits);
  return static_cast<unsigned>(degree);
}

void select(acb_ptr output, const sic::support::ComplexBallVector& roots, acb_srcptr target,
            bool authoritative_containment, slong bits) {
  precision(bits);
  bounded(target, bits);
  if (roots.size() < 2 || roots.size() > 31)
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid root candidate count");
  unsigned matches = 0;
  size_t selected = 0;
  for (size_t j = 0; j < roots.size(); ++j) {
    bounded(roots[j], bits);
    for (size_t k = 0; k < j; ++k)
      if (acb_overlaps(roots[j], roots[k])) throw PrecisionRefusal("candidate roots not separated");
    if (acb_overlaps(roots[j], target)) {
      selected = j;
      ++matches;
    }
  }
  if (!matches)
    throw sic::Error(sic::ErrorKind::invalid_format,
                     "root label inconsistent with exact coefficients");
  if (matches != 1) throw PrecisionRefusal("ambiguous prime root label");
  if (authoritative_containment && !acb_contains(target, roots[selected]))
    throw PrecisionRefusal("root is not contained in authoritative branch rectangle");
  acb_set(output, roots[selected]);
}

}  // namespace sic::decode::kernels
namespace sic::decode {
sic::support::ComplexBallVector isolate_roots(const sic::decode::kernels::Polynomial& polynomial,
                                              slong bits, slong iterations) {
  const unsigned p = sic::decode::kernels::monic_degree(polynomial, bits);
  sic::support::ComplexBallVector roots(p);
  if (acb_poly_find_roots(roots.data(), polynomial.get(), nullptr, iterations, bits) !=
      static_cast<slong>(p))
    throw PrecisionRefusal("not all defining-polynomial roots were certified isolated");
  for (unsigned j = 0; j < p; ++j) {
    if (!acb_is_finite(roots[j])) throw PrecisionRefusal("nonfinite isolated root");
    for (unsigned k = 0; k < j; ++k)
      if (acb_overlaps(roots[j], roots[k])) throw PrecisionRefusal("root rectangles not disjoint");
  }
  return roots;
}

sic::support::ComplexBallVector euler_duals(const sic::decode::kernels::Polynomial& polynomial,
                                            acb_srcptr root, slong bits) {
  const unsigned p = sic::decode::kernels::monic_degree(polynomial, bits);
  sic::decode::kernels::bounded(root, bits);
  sic::support::ComplexBallVector c(p), work(1);
  acb_poly_evaluate(work[0], polynomial.get(), root, bits);
  if (!acb_contains_zero(work[0]))
    throw sic::Error(sic::ErrorKind::invalid_format, "selected root fails polynomial enclosure");
  acb_one(c[p - 1]);
  for (unsigned k = p - 1; k; --k) {
    acb_mul(c[k - 1], root, c[k], bits);
    acb_add(c[k - 1], c[k - 1], acb_poly_get_coeff_ptr(polynomial.get(), k), bits);
  }
  acb_zero(work[0]);
  for (unsigned k = p; k-- > 0;) {
    acb_mul(work[0], work[0], root, bits);
    acb_add(work[0], work[0], c[k], bits);
  }
  if (!acb_is_finite(work[0]) || acb_contains_zero(work[0]))
    throw PrecisionRefusal("trace-dual derivative contains zero");
  for (unsigned k = 0; k < p; ++k) {
    acb_div(c[k], c[k], work[0], bits);
    if (!acb_is_finite(c[k])) throw PrecisionRefusal("nonfinite trace dual");
  }
  return c;
}

void transport_odd_value(acb_ptr out, const sic::support::ComplexBallVector& delta_a, unsigned p,
                         acb_srcptr delta, acb_srcptr old_root, slong bits) {
  if (acb_contains_zero(delta)) throw PrecisionRefusal("odd action denominator contains zero");
  sic::support::ComplexBallVector value(1);
  for (unsigned k = p; k-- > 0;) {
    acb_mul(value[0], value[0], old_root, bits);
    acb_add(value[0], value[0], delta_a[k], bits);
  }
  acb_div(value[0], value[0], delta, bits);
  if (!acb_is_finite(value[0])) throw PrecisionRefusal("nonfinite odd transported root");
  acb_set(out, value[0]);
}

LeafConverter::LeafConverter(int64_t D0, slong prec, uint64_t max_bits)
    : prec_(prec),
      cp_(prec + static_cast<slong>(max_bits) + 64),
      imaginary_(D0 < 0),
      half_basis_(((D0 % 4) + 4) % 4 == 1) {
  arb_init(w_);
  arb_init(a_);
  if (D0 == 1) {
    arb_zero(w_);
  } else if (!imaginary_) {
    arb_sqrt_ui(w_, static_cast<ulong>(D0), cp_);
    arb_neg(w_, w_);
    if (half_basis_) {
      arb_add_ui(w_, w_, 1, cp_);
      arb_mul_2exp_si(w_, w_, -1);
    }
  } else {
    arb_sqrt_ui(w_, static_cast<ulong>(-D0), cp_);  // w = -sqrt(|D0|)/2 = Im iota(omega)
    arb_neg(w_, w_);
    arb_mul_2exp_si(w_, w_, -1);
  }
}

sic::support::ComplexBallVector normalize(std::span<const acb_struct> v, uint64_t anchor,
                                          slong prec) {
  const size_t d = v.size();
  sic::support::ComplexBallVector out(d), work(2);
  arb_t norm2, norm, magnitude, t;
  arb_init(norm2);
  arb_init(norm);
  arb_init(magnitude);
  arb_init(t);
  for (size_t j = 0; j < d; ++j) {
    acb_abs(t, &v[j], prec);
    arb_addmul(norm2, t, t, prec);
  }
  arb_sqrt(norm, norm2, prec);
  acb_abs(magnitude, &v[anchor], prec);
  if (!arb_is_positive(norm) || !arb_is_positive(magnitude)) {
    arb_clear(norm2);
    arb_clear(norm);
    arb_clear(magnitude);
    arb_clear(t);
    fail(sic::ErrorKind::precision, "zero-containing norm or anchor");
  }
  acb_conj(work[0], &v[anchor]);
  acb_div_arb(work[0], work[0], magnitude, prec);
  for (size_t j = 0; j < d; ++j) {
    if (j == anchor) {
      arb_div(acb_realref(out[j]), magnitude, norm, prec);
      arb_zero(acb_imagref(out[j]));
    } else {
      acb_mul(out[j], &v[j], work[0], prec);
      acb_div_arb(out[j], out[j], norm, prec);
    }
  }
  arb_clear(norm2);
  arb_clear(norm);
  arb_clear(magnitude);
  arb_clear(t);
  return out;
}

}  // namespace sic::decode

namespace sic::decode::kernels {
PrecisionRefusal::PrecisionRefusal(const std::string& message)
    : sic::Error(ErrorKind::precision, message) {}
}  // namespace sic::decode::kernels
