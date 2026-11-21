// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/newton/newton_linear_refinement.hpp"

namespace sic::exact::inner_refinement {
bool propose_vectors(sic::support::ComplexBallVector& output, const acb_mat_t inverse,
                     const acb_mat_t jacobian, const std::vector<acb_struct>& residual,
                     const std::vector<acb_struct>& scale, slong precision, int extra_passes,
                     int owned_workers, sic::support::resources::Bytes scratch_limit,
                     const TeamFactory& factory,
                     const std::function<void(long)>& initial_row_observer) {
  const slong count = acb_mat_nrows(inverse);
  if (count <= 0 || acb_mat_ncols(inverse) != count || acb_mat_nrows(jacobian) != count ||
      acb_mat_ncols(jacobian) != count || residual.size() != static_cast<size_t>(count) ||
      scale.size() != residual.size() || &output.v == &residual || &output.v == &scale ||
      extra_passes < 0 || extra_passes > 2 || owned_workers <= 0)
    throw std::invalid_argument("invalid linear refinement proposal");
  if (precision < 2 || precision > WORD_MAX / 16) return false;
  auto check_stop = [] { sic::support::check_stop(); };
  check_stop();
  auto finite = [](acb_srcptr value) {
    return acb_is_finite(value) && ARB_IS_LAGOM(acb_realref(value)) &&
           ARB_IS_LAGOM(acb_imagref(value));
  };
  for (slong row = 0; row < count; ++row) {
    if (!finite(&residual[row]) || !finite(&scale[row])) return false;
    for (slong column = 0; column < count; ++column)
      for (acb_srcptr value :
           {acb_mat_entry(inverse, row, column), acb_mat_entry(jacobian, row, column)})
        if (!finite(value) || !acb_is_exact(value) || acb_bits(value) > precision + 64)
          return false;
  }
  const sic::support::resources::Budget budget;
  const int workers = static_cast<int>(std::min<slong>(count, owned_workers));
  sic::support::resources::Bytes bytes;
  try {
    bytes = scratch_bytes(count, precision, workers);
  } catch (const std::overflow_error&) {
    return false;
  }
  if (bytes > scratch_limit) return false;
  struct Magnitudes {
    std::vector<mag_struct> values;
    explicit Magnitudes(slong size) : values(size) {
      for (auto& value : values) mag_init(&value);
    }
    ~Magnitudes() {
      for (auto& value : values) mag_clear(&value);
    }
    mag_ptr operator[](slong index) { return &values[index]; }
  };
  struct Workspace {
    sic::support::ComplexBallVector rhs, delta, product, remainder, increment;
    Magnitudes weights, sizes;
    explicit Workspace(slong size)
        : rhs(size),
          delta(size),
          product(size),
          remainder(size),
          increment(size),
          weights(size),
          sizes(size) {}
  };
  std::unique_ptr<Workspace> work;
  std::unique_ptr<RowWorkerTeam> team;
  try {
    budget.require_additional("experimental Newton linear refinement", bytes);
    work = std::make_unique<Workspace>(count);
    for (slong row = 0; row < count; ++row) {
      acb_set_round(work->rhs[row], &residual[row], precision);
      acb_get_mid(work->rhs[row], work->rhs[row]);
      acb_get_mag(work->weights[row], &scale[row]);
      if (mag_is_zero(work->weights[row])) mag_one(work->weights[row]);
    }
    team = factory ? factory(workers) : std::make_unique<RowWorkerTeam>(workers);
    if (team) team->prepare();
  } catch (const sic::support::resources::AdmissionError&) {
    check_stop();
    return false;
  } catch (const std::system_error&) {
    check_stop();
    return false;
  } catch (const std::bad_alloc&) {
    check_stop();
    return false;
  }
  if (!team) throw std::logic_error("linear refinement factory returned no workers");
  auto multiply = [&](sic::support::ComplexBallVector& target, const acb_mat_t matrix,
                      const sic::support::ComplexBallVector& vector, bool initial = false) {
    team->for_each(count, [&](long row) {
      // FLINT approx_dot writes midpoints only and leaves radii untouched.
      acb_zero(target[row]);
      acb_approx_dot(target[row], nullptr, 0, acb_mat_entry(matrix, row, 0), 1, vector.v.data(), 1,
                     count, precision);
      acb_get_mid(target[row], target[row]);
      if (initial && initial_row_observer) initial_row_observer(row);
    });
  };
  auto norm = [&](const sic::support::ComplexBallVector& vector, mag_ptr result) {
    team->for_each(count, [&](long row) {
      acb_get_mag(work->sizes[row], vector[row]);
      mag_div(work->sizes[row], work->sizes[row], work->weights[row]);
    });
    mag_zero(result);
    for (slong row = 0; row < count; ++row) mag_max(result, result, work->sizes[row]);
  };
  auto all_finite = [&](const sic::support::ComplexBallVector& vector) {
    for (slong row = 0; row < count; ++row)
      if (!finite(vector[row]) || !acb_is_exact(vector[row])) return false;
    return true;
  };
  Magnitudes metrics(3);
  multiply(work->delta, inverse, work->rhs, true);
  if (!all_finite(work->delta)) return false;
  norm(work->delta, metrics[0]);
  for (int pass = 0; pass < extra_passes; ++pass) {
    multiply(work->product, jacobian, work->delta);
    team->for_each(count, [&](long row) {
      acb_sub(work->remainder[row], work->rhs[row], work->product[row], precision);
      acb_get_mid(work->remainder[row], work->remainder[row]);
    });
    if (!all_finite(work->remainder)) return false;
    multiply(work->increment, inverse, work->remainder);
    if (!all_finite(work->increment)) return false;
    norm(work->increment, metrics[1]);
    mag_mul_2exp_si(metrics[2], metrics[0], 2);
    if (mag_is_inf(metrics[1]) || mag_cmp(metrics[1], metrics[2]) > 0) return false;
    mag_set(metrics[0], metrics[1]);
    team->for_each(count, [&](long row) {
      acb_add(work->delta[row], work->delta[row], work->increment[row], precision);
      acb_get_mid(work->delta[row], work->delta[row]);
    });
    if (!all_finite(work->delta)) return false;
  }
  check_stop();
  team.reset();
  check_stop();
  output = std::move(work->delta);
  return true;
}

bool propose(sic::support::ComplexBallVector& output, const acb_mat_t inverse,
             const acb_mat_t jacobian, const sic::support::ComplexBallVector& residual,
             const sic::support::ComplexBallVector& scale, slong precision, int extra_passes,
             int owned_workers, sic::support::resources::Bytes scratch_limit,
             const TeamFactory& factory, const std::function<void(long)>& observer) {
  return propose_vectors(output, inverse, jacobian, residual.v, scale.v, precision, extra_passes,
                         owned_workers, scratch_limit, factory, observer);
}

}  // namespace sic::exact::inner_refinement
