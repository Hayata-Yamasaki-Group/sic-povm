// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decode/kernels/kernels.hpp"

#include <iostream>
#include <string_view>

using sic::support::ComplexBallVector;

namespace {
template <class Exception = std::runtime_error, class F>
void refuses(std::string_view message, F run) {
  try {
    run();
  } catch (const Exception& error) {
    if (std::string_view(error.what()).find(message) != std::string_view::npos) return;
    throw;
  }
  throw std::runtime_error("missing refusal: " + std::string(message));
}
}  // namespace

int main() {
  try {
    flint_set_num_threads(1);
    sic::support::ComplexBallVector roots(2), target(1), out(1);
    acb_set_si(roots[0], -1);
    acb_one(roots[1]);
    acb_one(target[0]);
    sic::decode::kernels::select(out[0], roots, target[0], true, 128);
    if (!acb_is_one(out[0])) throw std::runtime_error("wrong selected root");
    refuses("invalid root candidate count", [&] {
      sic::decode::kernels::select(out[0], sic::support::ComplexBallVector(1), target[0], false,
                                   128);
    });
    refuses("invalid root candidate count", [&] {
      sic::decode::kernels::select(out[0], sic::support::ComplexBallVector(32), target[0], false,
                                   128);
    });
    acb_zero(target[0]);
    refuses("root label inconsistent",
            [&] { sic::decode::kernels::select(out[0], roots, target[0], false, 128); });
    arb_add_error_2exp_si(acb_realref(target[0]), 1);
    refuses<sic::decode::kernels::PrecisionRefusal>("ambiguous prime root label", [&] {
      sic::decode::kernels::select(out[0], roots, target[0], false, 128);
    });
    acb_one(target[0]);
    arb_add_error_2exp_si(acb_realref(roots[1]), -20);
    refuses<sic::decode::kernels::PrecisionRefusal>("not contained in authoritative", [&] {
      sic::decode::kernels::select(out[0], roots, target[0], true, 128);
    });
    sic::decode::kernels::select(out[0], roots, target[0], false, 128);
    acb_set(roots[0], roots[1]);
    refuses<sic::decode::kernels::PrecisionRefusal>("candidate roots not separated", [&] {
      sic::decode::kernels::select(out[0], roots, target[0], false, 128);
    });
    acb_indeterminate(target[0]);
    refuses("nonfinite or unsupported", [&] { sic::decode::kernels::bounded(target[0], 128); });
    acb_one(target[0]);
    arf_mul_2exp_si(arb_midref(acb_realref(target[0])), arb_midref(acb_realref(target[0])),
                    LONG_MAX);
    refuses("nonfinite or unsupported", [&] { sic::decode::kernels::bounded(target[0], 128); });
    acb_one(target[0]);
    arf_mul_2exp_si(arb_midref(acb_realref(target[0])), arb_midref(acb_realref(target[0])), 2048);
    arf_add_ui(arb_midref(acb_realref(target[0])), arb_midref(acb_realref(target[0])), 1,
               ARF_PREC_EXACT, ARF_RND_NEAR);
    refuses("mantissa exceeds explicit limit",
            [&] { sic::decode::kernels::bounded(target[0], 128); });
    // A cubic tower step over Q: isolate x^3 - 2 and select its real branch.
    sic::decode::kernels::Polynomial cubic;
    acb_poly_set_coeff_si(cubic.get(), 3, 1);
    acb_poly_set_coeff_si(cubic.get(), 0, -2);
    auto cubic_roots = sic::decode::isolate_roots(cubic, 128, 0);
    if (cubic_roots.size() != 3) throw std::runtime_error("wrong cubic root count");
    ComplexBallVector cubic_target(1), cubic_out(1);
    acb_set_si(cubic_target[0], 1);
    arb_add_error_2exp_si(acb_realref(cubic_target[0]), -1);
    sic::decode::kernels::select(cubic_out[0], cubic_roots, cubic_target[0], false, 128);
    auto duals = sic::decode::euler_duals(cubic, cubic_out[0], 128);
    if (duals.size() != 3 || !acb_is_finite(duals[0]))
      throw std::runtime_error("invalid cubic tower trace duals");
    sic::decode::kernels::Polynomial polynomial;
    refuses("bounded monic", [&] { sic::decode::kernels::monic_degree(polynomial, 128); });
    acb_poly_set_coeff_si(polynomial.get(), 1, 1);
    refuses("bounded monic", [&] { sic::decode::kernels::monic_degree(polynomial, 128); });
    acb_poly_set_coeff_si(polynomial.get(), 2, 2);
    refuses("bounded monic", [&] { sic::decode::kernels::monic_degree(polynomial, 128); });
    acb_poly_set_coeff_si(polynomial.get(), 2, 1);
    acb_poly_set_coeff_si(polynomial.get(), 0, -1);
    if (sic::decode::kernels::monic_degree(polynomial, 128) != 2)
      throw std::runtime_error("wrong polynomial degree");
    acb_poly_set_coeff_acb(polynomial.get(), 0, target[0]);
    refuses("mantissa exceeds explicit limit",
            [&] { sic::decode::kernels::monic_degree(polynomial, 128); });
    acb_poly_set_coeff_si(polynomial.get(), 32, 1);
    refuses("bounded monic", [&] { sic::decode::kernels::monic_degree(polynomial, 128); });
    refuses("requires supported native precision", [&] { sic::decode::kernels::precision(63); });
    sic::decode::kernels::precision(1048577);  // Raised computational policies are supported.
    refuses("requires supported native precision",
            [&] { sic::decode::kernels::precision(sic::kNativeMaximumPrecision + 1); });
    flint_set_num_threads(2);
    bool categorized = false;
    try {
      sic::decode::kernels::precision(128);
    } catch (const sic::Error& error) {
      categorized = error.kind() == sic::ErrorKind::invalid_argument;
    }
    if (!categorized) throw std::runtime_error("worker precondition category");
    flint_set_num_threads(1);
    std::cout << "PASS: decoder kernel refusal branches\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
