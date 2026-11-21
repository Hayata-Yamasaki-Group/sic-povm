// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/bounds/overlap_bounds.hpp"

#include <flint/acb_dft.h>

#include "sic/error.hpp"
#include "sic/support/ball_vectors.hpp"
#include "support/resources.hpp"

namespace sic::bounds {
using namespace sic::support::resources;

struct WhOverlapDeviationBounds::Impl {
  BallVerificationInfo info;
  sic::support::RealBallVector bounds{2};
  explicit Impl(BallVerificationInfo metadata) : info(metadata) {}
};

namespace {
struct DftPlan {
  acb_dft_pre_t value;
  DftPlan(slong d, slong bits) { acb_dft_precomp_init(value, d, bits); }
  ~DftPlan() { acb_dft_precomp_clear(value); }
  DftPlan(const DftPlan&) = delete;
  DftPlan& operator=(const DftPlan&) = delete;
};

void require_finite(arb_srcptr value) {
  if (!arb_is_finite(value))
    throw sic::Error(sic::ErrorKind::precision, "nonfinite WH deviation enclosure");
}
}  // namespace

WhOverlapDeviationBounds::WhOverlapDeviationBounds(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
WhOverlapDeviationBounds::~WhOverlapDeviationBounds() = default;
WhOverlapDeviationBounds::WhOverlapDeviationBounds(WhOverlapDeviationBounds&&) noexcept = default;
WhOverlapDeviationBounds& WhOverlapDeviationBounds::operator=(WhOverlapDeviationBounds&&) noexcept =
    default;

const BallVerificationInfo& WhOverlapDeviationBounds::info() const {
  if (!impl_) throw sic::Error(sic::ErrorKind::invalid_argument, "moved-from WH deviation bounds");
  return impl_->info;
}
arb_srcptr WhOverlapDeviationBounds::squared_norm() const {
  (void)info();
  return impl_->bounds[0];
}
arb_srcptr WhOverlapDeviationBounds::maximum_squared_overlap_deviation() const {
  (void)info();
  return impl_->bounds[1];
}

std::uint64_t admit_wh_bounds(std::size_t dimension, const BallVerificationLimits& limits) {
  using sic::support::resources::add;
  using sic::support::resources::Bytes;
  using sic::support::resources::mul;
  const slong bits = limits.working_bits;
  const slong accuracy = limits.minimum_absolute_accuracy_bits;
  if (dimension < kMinimumBallDimension || dimension > kMaximumBallDimension ||
      bits < kMinimumBallWorkingBits || bits > sic::kNativeMaximumPrecision || accuracy < 1 ||
      accuracy > bits - 32)
    throw sic::Error(ErrorKind::invalid_argument,
                     "invalid WH verification dimension/precision/accuracy limit");
  if (dimension > limits.maximum_dimension)
    throw sic::Error(ErrorKind::resource, "WH dimension exceeds policy");
  if (bits > limits.maximum_working_bits)
    throw sic::Error(ErrorKind::precision, "WH working precision exceeds policy");
  if (flint_get_num_threads() != 1)
    throw sic::Error(ErrorKind::invalid_argument,
                     "WH ball verification requires one native FLINT worker");
  const sic::support::resources::Count d = dimension;
  const sic::support::resources::Count points = mul(d, d);
  const sic::support::resources::Count minimum_work = add(mul(points, d), mul(8, points));
  if (minimum_work > limits.maximum_work_score)
    throw AdmissionError("WH ball verification exceeds explicit work admission score");
  const Bytes workspace = add(64 * kMib, mul(mul(256, d), complex_bytes(bits + 128)));
  if (workspace > limits.maximum_workspace_bytes)
    throw AdmissionError("WH ball verification exceeds explicit workspace allowance");

  return workspace;
}

WhOverlapDeviationBounds bound_wh_overlap_deviations(acb_srcptr input, std::size_t dimension,
                                                     const BallVerificationLimits& limits) {
  using sic::support::resources::add;
  using sic::support::resources::Bytes;
  using sic::support::resources::mul;
  const slong bits = limits.working_bits;
  const slong accuracy = limits.minimum_absolute_accuracy_bits;
  if (!input) throw sic::Error(sic::ErrorKind::invalid_argument, "null WH verification input");
  const Bytes workspace = admit_wh_bounds(dimension, limits);
  const sic::support::resources::Count d = dimension;
  const sic::support::resources::Count points = mul(d, d);

  // Inspect metadata without copying arbitrarily wide input mantissas. The
  // rounded owner is allocated only after every declared/fresh budget passes.
  sic::support::resources::BitCount input_bits = 0;
  for (std::size_t j = 0; j < dimension; ++j) {
    if (!acb_is_finite(input + j) || !ARB_IS_LAGOM(acb_realref(input + j)) ||
        !ARB_IS_LAGOM(acb_imagref(input + j)))
      throw sic::Error(sic::ErrorKind::invalid_argument,
                       "WH verification input must contain finite Lagom balls");
    input_bits = add(input_bits, arf_bits(arb_midref(acb_realref(input + j))));
    input_bits = add(input_bits, arf_bits(arb_midref(acb_imagref(input + j))));
    if (input_bits > limits.maximum_input_mantissa_bits)
      throw AdmissionError("WH verification exceeds explicit input mantissa limit");
  }
  const sic::support::resources::Count work =
      add(add(mul(points, d), mul(8, points)), add(input_bits, FLINT_BITS - 1) / FLINT_BITS);
  if (work > limits.maximum_work_score)
    throw AdmissionError("WH verification exceeds explicit work admission score");
  Budget().require_additional("bounded WH ball verification", workspace);

  auto result = std::make_unique<WhOverlapDeviationBounds::Impl>(BallVerificationInfo{
      dimension, bits, accuracy, points - 1, d, points, input_bits, workspace, work});
  sic::support::ComplexBallVector bounded(dimension), row(dimension), overlaps(dimension);
  sic::support::RealBallVector real(6);
  for (std::size_t j = 0; j < dimension; ++j) {
    acb_set_round(bounded[j], input + j, bits);
    if (!acb_is_finite(bounded[j]))
      throw sic::Error(sic::ErrorKind::precision, "nonfinite rounded WH input");
    arb_sqr(real[0], acb_realref(bounded[j]), bits);
    arb_sqr(real[1], acb_imagref(bounded[j]), bits);
    arb_add(real[0], real[0], real[1], bits);
    arb_add(result->bounds[0], result->bounds[0], real[0], bits);
  }
  require_finite(result->bounds[0]);
  if (arb_is_zero(result->bounds[0]))
    throw sic::Error(ErrorKind::invalid_argument, "WH verification input norm is exactly zero");
  if (!arb_is_positive(result->bounds[0]))
    throw sic::Error(ErrorKind::precision, "WH verification norm has no positive lower bound");
  arb_sqr(real[2], result->bounds[0], bits);
  arb_inv(real[2], real[2], bits);  // reciprocal squared norm
  arb_set_ui(real[3], dimension + 1);
  arb_inv(real[3], real[3], bits);
  require_finite(real[2]);
  const DftPlan plan(static_cast<slong>(dimension), bits);
  sic::support::resources::Count checked = 0;
  for (std::size_t a = 0; a < dimension; ++a) {
    for (std::size_t j = 0; j < dimension; ++j) {
      acb_conj(row[j], bounded[j]);
      acb_mul(row[j], row[j], bounded[(j + dimension - a) % dimension], bits);
      if (!acb_is_finite(row[j]))
        throw sic::Error(sic::ErrorKind::precision, "nonfinite WH product row");
    }
    acb_dft_precomp(overlaps.data(), row.data(), plan.value, bits);
    for (std::size_t b = 0; b < dimension; ++b) {
      if (!acb_is_finite(overlaps[b]))
        throw sic::Error(sic::ErrorKind::precision, "nonfinite WH DFT output");
      if (!a && !b) continue;
      // F_a(b) is the negative DFT of conj(v_j)*v_(j-a). With omega=e^(2pi i/d),
      // <v|D_(a,-b)|v> = tau^(-ab)*omega^(ab)*F_a(b). Unit phases disappear
      // after taking squared modulus; (a,-b) runs through every WH label.
      arb_sqr(real[0], acb_realref(overlaps[b]), bits);
      arb_sqr(real[1], acb_imagref(overlaps[b]), bits);
      arb_add(real[4], real[0], real[1], bits);
      arb_mul(real[4], real[4], real[2], bits);
      arb_sub(real[4], real[4], real[3], bits);
      arb_abs(real[4], real[4]);
      require_finite(real[4]);
      arb_max(result->bounds[1], result->bounds[1], real[4], bits);
      ++checked;
    }
  }
  require_finite(result->bounds[1]);
  if (checked != points - 1) throw std::logic_error("incomplete WH nonidentity coverage");
  if (mag_cmp_2exp_si(arb_radref(result->bounds[1]), -accuracy) > 0)
    throw sic::Error(sic::ErrorKind::precision,
                     "WH deviation enclosure has insufficient absolute accuracy");
  return WhOverlapDeviationBounds(std::move(result));
}

}  // namespace sic::bounds
