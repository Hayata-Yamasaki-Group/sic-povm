// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/reconstruction/ghost.hpp"

namespace sic::exact {
GhostRefiner::CacheClear::~CacheClear() {
  for (auto value : shifts)
    if (value) _acb_vec_clear(value, length);
  for (auto value : frequencies)
    if (value) _acb_vec_clear(value, length);
}

GhostRefiner::Ctx::Ctx(const Ctx& hi, slong p_) : p(p_), wd(hi.wd.size()), w2d(hi.w2d.size()) {
  for (size_t k = 0; k < wd.size(); ++k) {
    acb_init(&wd[k]);
    acb_set_round(&wd[k], &hi.wd[k], p);
  }
  for (size_t k = 0; k < w2d.size(); ++k) {
    acb_init(&w2d[k]);
    acb_set_round(&w2d[k], &hi.w2d[k], p);
  }
  acb_set_round(cst, hi.cst, p);
  Kc = hi.Kc;
}

GhostRefiner::Ctx::Ctx(long d, int chi, slong p_) : p(p_), wd(d), w2d(2 * d) {
  sic::support::flint::Arb x;
  for (long k = 0; k < d; ++k) {
    acb_init(&wd[k]);
    arb_set_si(x, k);
    arb_div_si(x, x, d, p);
    arb_const_pi(acb_realref(&wd[k]), p);
    arb_mul(x, x, acb_realref(&wd[k]), p);
    arb_mul_2exp_si(x, x, 1);
    arb_sin_cos(acb_imagref(&wd[k]), acb_realref(&wd[k]), x, p);
  }
  for (long k = 0; k < 2 * d; ++k) {
    acb_init(&w2d[k]);
    arb_set_si(x, k);
    arb_div_si(x, x, 2 * d, p);
    arb_const_pi(acb_realref(&w2d[k]), p);
    arb_mul(x, x, acb_realref(&w2d[k]), p);
    arb_mul_2exp_si(x, x, 1);
    arb_sin_cos(acb_imagref(&w2d[k]), acb_realref(&w2d[k]), x, p);
  }
  arb_sqrt_ui(acb_realref(cst), d + 1, p);
  if (chi > 0) acb_neg(cst, cst);
}

GhostRefiner::Ctx::~Ctx() {
  for (auto& z : wd) acb_clear(&z);
  for (auto& z : w2d) acb_clear(&z);
}

void GhostRefiner::expand(const std::vector<acb_struct>& z, std::vector<acb_struct>& v,
                          std::vector<acb_struct>& u) const {
  long q = 0;
  for (long a = 0; a < d; ++a) {
    if (a == k) {
      acb_one(&v[a]);
      acb_one(&u[a]);
      continue;
    }
    acb_set(&v[a], &z[q]);
    acb_set(&u[a], &z[d - 1 + q]);
    ++q;
  }
}

void GhostRefiner::residual_serial(std::vector<acb_struct>& F, const Ctx& C,
                                   const std::vector<acb_struct>& z) const {
  sic::support::ComplexBallVector owned_v(d), owned_u(d);
  auto& v = owned_v.v;
  auto& u = owned_u.v;
  expand(z, v, u);
  sic::support::flint::Acb s, A, V1, V2, Ky, c;

  acb_dot(s, NULL, 0, u.data(), 1, v.data(), 1, d, C.p);
  acb_inv(s, s, C.p);
  std::vector<acb_ptr> Tj(d, nullptr), Wi(d, nullptr);
  CacheClear clear_caches{Tj, Wi, d};
  auto need = [&](long y) {
    const long i = y / d, j = y % d;
    if (!Tj[j]) {
      Tj[j] = _acb_vec_init(d);
      for (long a = 0; a < d; ++a) acb_mul(Tj[j] + a, &v[a], &u[(a + j) % d], C.p);
    }
    if (!Wi[i]) {
      Wi[i] = _acb_vec_init(d);
      for (long a = 0; a < d; ++a) acb_set(Wi[i] + a, &C.wd[(d - (i * a) % d) % d]);
    }
  };
  auto Vof = [&](acb_t V, long y) {  // V_y = coef_y A_y
    const long i = y / d, j = y % d;
    acb_dot(A, NULL, 0, Tj[j], 1, Wi[i], 1, d, C.p);
    acb_mul(A, A, s, C.p);
    coef(c, C, y);
    acb_mul(V, A, c, C.p);
  };
  for (size_t r = 0; r < S.size(); ++r) {
    const long y = S[r];
    need(y);
    need(neg(y));
    Vof(V1, y);
    Vof(V2, neg(y));
    if (C.Kc)
      acb_set_round(Ky, &(*C.Kc)[r], C.p);
    else
      root_k(Ky, C.p, Kexp[y]);
    acb_mul(&F[r], V1, V2, C.p);
    acb_sub(&F[r], &F[r], Ky, C.p);
  }
  phase_memory::BallStorage traced_storage;
  if (memory_trace) {
    for (long q = 0; q < d; ++q) {
      if (Tj[q])
        traced_storage.append(phase_memory::inspect_storage(std::span<const acb_struct>(Tj[q], d)));
      if (Wi[q])
        traced_storage.append(phase_memory::inspect_storage(std::span<const acb_struct>(Wi[q], d)));
    }
  }
  if (memory_trace) memory_trace->cache("residual caches", traced_storage, C.p);
}

void GhostRefiner::release_vector_workers() const { vector_workspace.reset(); }

RowWorkerTeam* GhostRefiner::vector_workers(size_t rows, std::optional<slong> precision) const {
  check_vector_stop();
  // A nested request or readmission may have acquired storage under another
  // limit. Returning to the original context must not revive its shortcut.
  if (vector_attempt && (!vector_attempt->settings ||
                         vector_attempt->settings != sic::support::current_kernel_settings ||
                         vector_attempt->workspace != workspace_limit ||
                         vector_attempt->callback != callback_workspace_limit))
    vector_attempt.reset();
  if (rows < 64 || kernel_threads <= 1) {
    release_vector_workers();
    return nullptr;
  }
  try {
#ifdef SIC_ENABLE_TEST_HOOKS
    if (before_operation) before_operation(failure_context, "vector_team");
#endif
    if (vector_workspace && vector_workspace->team->size() != kernel_threads)
      release_vector_workers();
    if (!vector_workspace) {
      auto workspace = std::make_unique<VectorWorkspace>();
      workspace->team = std::make_unique<RowWorkerTeam>(kernel_threads);
      vector_workspace = std::move(workspace);
    } else if (!vector_attempt) {
      // Retained stacks are already reflected in RSS, including when a new
      // request installs a smaller global budget before reusing this owner.
      // Within an unchanged refinement request no team storage is acquired
      // by reuse. Fresh crew and scalar-capacity admission remain below.
      sic::support::resources::Budget().require_additional("retained row worker team", 0);
    }
    if (precision && vector_workspace->precision != precision) {
      // Discard the old precision's limb capacity before admitting new slots.
      // Every use assigns both midpoint and radius before reading a slot.
      vector_workspace->scratch = sic::support::ComplexBallVector();
      vector_workspace->precision.reset();
      const auto slots = sic::support::resources::mul(kernel_threads, kRowScratchStride);
      sic::support::resources::Budget().require_additional(
          "Newton row scalar slots",
          sic::support::resources::mul(
              slots, sic::support::resources::complex_bytes(
                         sic::support::resources::newton_matrix_bits(*precision))));
      vector_workspace->scratch = sic::support::ComplexBallVector(slots);
      vector_workspace->precision = precision;
    }
    return vector_workspace->team.get();
  } catch (const sic::support::resources::AdmissionError&) {
    release_vector_workers();
    check_vector_stop();
    return nullptr;
  } catch (const std::system_error&) {
    release_vector_workers();
    check_vector_stop();
    return nullptr;
  }
}

void GhostRefiner::residual(std::vector<acb_struct>& F, const Ctx& C,
                            const std::vector<acb_struct>& z) const {
  auto* team = vector_workers(S.size(), C.p);
  if (!team) {
    residual_serial(F, C, z);
    return;
  }
  sic::support::ComplexBallVector v(d), u(d), inverse(1);
  expand(z, v.v, u.v);
  acb_dot(inverse[0], nullptr, 0, u.v.data(), 1, v.v.data(), 1, d, C.p);
  acb_inv(inverse[0], inverse[0], C.p);
  std::vector<unsigned char> need_shift(d, 0), need_frequency(d, 0);
  for (long y : S)
    for (long label : {y, neg(y)}) {
      need_shift[label % d] = 1;
      need_frequency[label / d] = 1;
    }
  std::vector<std::unique_ptr<sic::support::ComplexBallVector>> shifts(d), frequencies(d);
  team->for_each(d, [&](long q) {
    if (need_shift[q]) {
      auto values = std::make_unique<sic::support::ComplexBallVector>(d);
      for (long a = 0; a < d; ++a) acb_mul((*values)[a], v[a], u[(a + q) % d], C.p);
      shifts[q] = std::move(values);
    }
    if (need_frequency[q]) {
      auto values = std::make_unique<sic::support::ComplexBallVector>(d);
      for (long a = 0; a < d; ++a) acb_set((*values)[a], &C.wd[(d - (q * a) % d) % d]);
      frequencies[q] = std::move(values);
    }
  });
  if (memory_trace) {
    phase_memory::BallStorage storage;
    for (long q = 0; q < d; ++q) {
      if (shifts[q]) storage.append(phase_memory::inspect_storage(shifts[q]->v));
      if (frequencies[q]) storage.append(phase_memory::inspect_storage(frequencies[q]->v));
    }
    memory_trace->cache("residual caches", storage, C.p);
  }
  team->for_each_indexed((long)S.size(), [&](long r, int worker) {
    const auto work = vector_workspace->scratch.v.data() + worker * kRowScratchStride;
    auto value = [&](acb_ptr out, long y) {
      const long i = y / d, j = y % d;
      acb_dot(work, nullptr, 0, shifts[j]->v.data(), 1, frequencies[i]->v.data(), 1, d, C.p);
      acb_mul(work, work, inverse[0], C.p);
      coef(work + 4, C, y);
      acb_mul(out, work, work + 4, C.p);
    };
    const long y = S[r];
    value(work + 1, y);
    value(work + 2, neg(y));
    if (C.Kc)
      acb_set_round(work + 3, &(*C.Kc)[r], C.p);
    else
      root_k(work + 3, C.p, Kexp[y]);
    acb_mul(&F[r], work + 1, work + 2, C.p);
    acb_sub(&F[r], &F[r], work + 3, C.p);
  });
}

double GhostRefiner::correction(const acb_mat_t inverse, std::vector<acb_struct>& F,
                                std::vector<acb_struct>& z, std::vector<acb_struct>& dz,
                                slong precision, slong correction_precision) const {
  const long count = 2 * d - 2;
  auto team = vector_workers(count);
  auto round = [&](long r) { acb_set_round(&F[r], &F[r], correction_precision); };
  auto update = [&](long r) {
    acb_approx_dot(&dz[r], nullptr, 0, acb_mat_entry(inverse, r, 0), 1, F.data(), 1, count,
                   correction_precision);
    acb_sub(&z[r], &z[r], &dz[r], precision);
    acb_get_mid(&z[r], &z[r]);
    struct Magnitudes {
      sic::support::flint::Mag delta, coordinate;
      Magnitudes() {}
      ~Magnitudes() {}
    } magnitude;
    acb_get_mag(magnitude.delta, &dz[r]);
    acb_get_mag(magnitude.coordinate, &z[r]);
    double result = -1e9;
    if (!mag_is_zero(magnitude.delta) && !mag_is_zero(magnitude.coordinate))
      result = std::max(result, mag_get_d_log2_approx(magnitude.delta) -
                                    mag_get_d_log2_approx(magnitude.coordinate));
    return result;
  };
  double maximum = -1e9;
  if (!team) {
    for (long r = 0; r < count; ++r) round(r);
    for (long r = 0; r < count; ++r) maximum = std::max(maximum, update(r));
  } else {
    std::vector<double> updates(count);
    team->for_each(count, round);
    team->for_each(count, [&](long r) { updates[r] = update(r); });
    for (long r = 0; r < count; ++r) maximum = std::max(maximum, updates[r]);
  }
  return maximum;
}

double GhostRefiner::commit_correction(const sic::support::ComplexBallVector& delta,
                                       std::vector<acb_struct>& z, slong precision) const {
  const long count = 2 * d - 2;
  if (delta.size() != (size_t)count || z.size() != (size_t)count)
    throw std::invalid_argument("invalid correction commit dimensions");
  auto team = vector_workers(count);
  std::vector<double> updates(count, -1e9);
  auto update = [&](long row) {
    acb_sub(&z[row], &z[row], delta[row], precision);
    acb_get_mid(&z[row], &z[row]);
    sic::support::flint::Mag change, coordinate;

    acb_get_mag(change, delta[row]);
    acb_get_mag(coordinate, &z[row]);
    if (!mag_is_zero(change) && !mag_is_zero(coordinate))
      updates[row] =
          std::max(updates[row], mag_get_d_log2_approx(change) - mag_get_d_log2_approx(coordinate));
  };
  if (team)
    team->for_each(count, update);
  else
    for (long row = 0; row < count; ++row) update(row);
  double maximum = -1e9;
  for (double value : updates) maximum = std::max(maximum, value);
  return maximum;
}

void GhostRefiner::root_k(acb_t r, slong p, long e) const {  // e(e / (4 d^2))
  sic::support::flint::Arb x;
  arb_set_si(x, e);
  arb_div_si(x, x, 4 * d * d, p);
  sic::support::flint::Arb pi;
  arb_const_pi(pi, p);
  arb_mul(x, x, pi, p);
  arb_mul_2exp_si(x, x, 1);
  arb_sin_cos(acb_imagref(r), acb_realref(r), x, p);
}

void GhostRefiner::jacobian_serial(acb_mat_t J, const Ctx& C, const std::vector<acb_struct>& z,
                                   const std::vector<long>& ys) const {
  sic::support::ComplexBallVector owned_v(d), owned_u(d);
  auto& v = owned_v.v;
  auto& u = owned_u.v;
  expand(z, v, u);
  sic::support::flint::Acb s, A1, V1, A2, V2;

  acb_dot(s, NULL, 0, u.data(), 1, v.data(), 1, d, C.p);
  acb_inv(s, s, C.p);
  std::vector<acb_ptr> shifts(d, nullptr), frequencies(d, nullptr);
  CacheClear clear_caches{shifts, frequencies, d};
  auto value = [&](acb_t A, acb_t V, long y) {
    const long i = y / d, j = y % d;
    if (!shifts[j]) {
      shifts[j] = _acb_vec_init(d);
      for (long a = 0; a < d; ++a) acb_mul(shifts[j] + a, &v[a], &u[(a + j) % d], C.p);
    }
    if (!frequencies[i]) {
      frequencies[i] = _acb_vec_init(d);
      for (long a = 0; a < d; ++a) acb_set(frequencies[i] + a, &C.wd[(d - (i * a) % d) % d]);
    }
    acb_dot(A, NULL, 0, shifts[j], 1, frequencies[i], 1, d, C.p);
    acb_mul(A, A, s, C.p);
    coef(V, C, y);
    acb_mul(V, V, A, C.p);
  };
  for (size_t r = 0; r < ys.size(); ++r) {
    long y = ys[r];
    for (long c = 0; c < 2 * d - 2; ++c) acb_zero(acb_mat_entry(J, r, c));
    value(A1, V1, y);
    value(A2, V2, neg(y));
    addgrad(acb_mat_entry(J, r, 0), C, y, V2, A1, v, u, s);
    addgrad(acb_mat_entry(J, r, 0), C, neg(y), V1, A2, v, u, s);
  }
  phase_memory::BallStorage traced_storage;
  if (memory_trace) {
    for (long q = 0; q < d; ++q) {
      if (shifts[q])
        traced_storage.append(
            phase_memory::inspect_storage(std::span<const acb_struct>(shifts[q], d)));
      if (frequencies[q])
        traced_storage.append(
            phase_memory::inspect_storage(std::span<const acb_struct>(frequencies[q], d)));
    }
  }
  if (memory_trace) memory_trace->cache("Jacobian caches", traced_storage, C.p);
}

void GhostRefiner::jacobian(acb_mat_t J, const Ctx& C, const std::vector<acb_struct>& z,
                            const std::vector<long>& ys) const {
  auto check_stop = [] { sic::support::check_stop(); };
  auto serial = [&] {
    check_stop();
    jacobian_serial(J, C, z, ys);
  };
  check_stop();
  auto* team = vector_workers(ys.size(), C.p);
  if (!team) {
    serial();
    return;
  }

  const auto started = std::chrono::steady_clock::now();
  const bool progress = sic::support::kernel_setting("SIC_PROGRESS") != nullptr;
  if (progress)
    sic::support::progressf("Newton Jacobian start: rows=%zu columns=%ld bits=%ld workers=%d\n",
                            ys.size(), 2 * d - 2, (long)C.p, team->size());
  sic::support::ComplexBallVector v(d), u(d), inverse(1);
  expand(z, v.v, u.v);
  acb_dot(inverse[0], nullptr, 0, u.v.data(), 1, v.v.data(), 1, d, C.p);
  acb_inv(inverse[0], inverse[0], C.p);
  std::vector<unsigned char> need_shift(d, 0), need_frequency(d, 0);
  for (long y : ys)
    for (long label : {y, neg(y)}) {
      need_shift[label % d] = 1;
      need_frequency[label / d] = 1;
    }
  std::vector<std::unique_ptr<sic::support::ComplexBallVector>> shifts(d), frequencies(d);
  team->for_each(d, [&](long q) {
    if (need_shift[q]) {
      auto values = std::make_unique<sic::support::ComplexBallVector>(d);
      for (long a = 0; a < d; ++a) acb_mul((*values)[a], v[a], u[(a + q) % d], C.p);
      shifts[q] = std::move(values);
    }
    if (need_frequency[q]) {
      auto values = std::make_unique<sic::support::ComplexBallVector>(d);
      for (long a = 0; a < d; ++a) acb_set((*values)[a], &C.wd[(d - (q * a) % d) % d]);
      frequencies[q] = std::move(values);
    }
  });
  // Caches are immutable after the dispatch barrier. Each worker owns its
  // output rows and temporaries; every row keeps the serial operation order.
  if (memory_trace) {
    phase_memory::BallStorage storage;
    for (long q = 0; q < d; ++q) {
      if (shifts[q]) storage.append(phase_memory::inspect_storage(shifts[q]->v));
      if (frequencies[q]) storage.append(phase_memory::inspect_storage(frequencies[q]->v));
    }
    memory_trace->cache("Jacobian caches", storage, C.p);
  }
  team->for_each_indexed((long)ys.size(), [&](long r, int worker) {
    const auto work = vector_workspace->scratch.v.data() + worker * kRowScratchStride;
    auto value = [&](acb_ptr A, acb_ptr V, long y) {
      const long i = y / d, j = y % d;
      acb_dot(A, nullptr, 0, shifts[j]->v.data(), 1, frequencies[i]->v.data(), 1, d, C.p);
      acb_mul(A, A, inverse[0], C.p);
      coef(V, C, y);
      acb_mul(V, V, A, C.p);
    };
    const long y = ys[r];
    for (long c = 0; c < 2 * d - 2; ++c) acb_zero(acb_mat_entry(J, r, c));
    value(work, work + 1, y);
    value(work + 2, work + 3, neg(y));
    addgrad_with_scratch(acb_mat_entry(J, r, 0), C, y, work + 3, work, v.v, u.v, inverse[0],
                         work + 5, work + 6);
    addgrad_with_scratch(acb_mat_entry(J, r, 0), C, neg(y), work + 1, work + 2, v.v, u.v,
                         inverse[0], work + 5, work + 6);
  });
  if (progress)
    sic::support::progressf(
        "Newton Jacobian done: rows=%zu elapsed=%.2fs\n", ys.size(),
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
}

bool GhostRefiner::refined_correction(sic::support::ComplexBallVector& proposal,
                                      const acb_mat_t inverse, const std::vector<acb_struct>& F,
                                      const std::vector<acb_struct>& point, const Ctx& high,
                                      slong precision, int passes,
                                      sic::support::resources::Bytes available,
                                      double* jacobian_seconds, double* inner_seconds) const {
  check_vector_stop();
  const long count = 2 * d - 2;
  if (F.size() != (size_t)count || point.size() != (size_t)count || S.size() != (size_t)count ||
      acb_mat_nrows(inverse) != count || acb_mat_ncols(inverse) != count)
    throw std::invalid_argument("invalid current-Jacobian correction dimensions");
  if (precision < 2 || precision > WORD_MAX / 16 || precision > high.p) return false;
  sic::support::resources::Bytes total, vectors;
  try {
#ifdef SIC_ENABLE_TEST_HOOKS
    if (before_operation) before_operation(failure_context, "linear_forecast");
#endif
    total = sic::support::resources::linear_correction_workspace(d, precision,
                                                                 std::max(1, kernel_threads));
    vectors = inner_refinement::scratch_bytes(count, precision, std::max(1, kernel_threads));
  } catch (const std::overflow_error&) {
    return false;
  }
  if (total > available) return false;
  std::unique_ptr<sic::support::ComplexBallVector> rounded;
  std::unique_ptr<Ctx> context;
  std::unique_ptr<sic::support::flint::AcbMat> jac;
  const auto started = std::chrono::steady_clock::now();
  try {
#ifdef SIC_ENABLE_TEST_HOOKS
    if (before_operation) before_operation(failure_context, "linear_storage");
#endif
    sic::support::resources::Budget().require_additional("current-Jacobian Newton correction",
                                                         total);
    rounded = std::make_unique<sic::support::ComplexBallVector>(count);
    for (long row = 0; row < count; ++row) {
      if (!acb_is_finite(&point[row]) || !acb_is_finite(&F[row])) return false;
      acb_set_round((*rounded)[row], &point[row], precision);
      acb_get_mid((*rounded)[row], (*rounded)[row]);
    }
    if (!selection_normalization_resolved(rounded->v, precision, false)) return false;
    context = std::make_unique<Ctx>(high, precision);
    jac = std::make_unique<sic::support::flint::AcbMat>(count, count);
  } catch (const sic::support::resources::AdmissionError&) {
    check_vector_stop();
    return false;
  } catch (const std::system_error&) {
    check_vector_stop();
    return false;
  } catch (const std::bad_alloc&) {
    check_vector_stop();
    return false;
  }
  // Jacobian construction and proposal worker exceptions propagate.
  const auto jacobian_start = std::chrono::steady_clock::now();
  jacobian(jac->get(), *context, rounded->v, S);
  // Inner refinement owns another team. Release this row crew before its
  // admission and dispatch, retaining the completed Jacobian only.
  release_vector_workers();
  for (long row = 0; row < count; ++row)
    for (long column = 0; column < count; ++column)
      if (!acb_is_finite(acb_mat_entry(jac->get(), row, column))) return false;
  if (memory_trace) memory_trace->matrix("correction J", jac->get(), precision, false);
  acb_mat_get_mid(jac->get(), jac->get());
  if (jacobian_seconds)
    *jacobian_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - jacobian_start).count();
  const auto other = total - vectors;
  const auto inner_start = std::chrono::steady_clock::now();
  const bool accepted =
      inner_refinement::propose_vectors(proposal, inverse, jac->get(), F, point, precision, passes,
                                        std::max(1, kernel_threads), available - other);
  if (inner_seconds)
    *inner_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - inner_start).count();
  if (sic::support::kernel_setting("SIC_PROGRESS"))
    sic::support::progressf(
        "Newton correction refinement: passes=%d bits=%ld accepted=%d workspace=%llu "
        "elapsed=%.3fs\n",
        passes, (long)precision, accepted, (unsigned long long)total,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
  return accepted;
}

}  // namespace sic::exact

namespace sic::exact {
bool GhostRefiner::selection_normalization_resolved(const std::vector<acb_struct>& point,
                                                    slong precision, bool verbose) const {
  sic::support::ComplexBallVector v(d), u(d), denominator(1);
  expand(point, v.v, u.v);
  acb_dot(denominator[0], nullptr, 0, u.v.data(), 1, v.v.data(), 1, d, precision);
  const bool finite = acb_is_finite(denominator[0]);
  const bool contains_zero = acb_contains_zero(denominator[0]);
  if (verbose) {
    auto exponent = [](arf_srcptr value) {
      return arf_is_zero(value)     ? 0L
             : arf_is_finite(value) ? (long)arf_abs_bound_lt_2exp_si(value)
                                    : WORD_MAX;
    };
    auto radius = [](mag_srcptr value) {
      return mag_is_zero(value) ? -std::numeric_limits<double>::infinity()
                                : mag_get_d_log2_approx(value);
    };
    sic::support::progressf(
        "selection denominator: bits=%ld finite=%d contains_zero=%d real_mid_exp=%ld "
        "real_rad_log2=%g imag_mid_exp=%ld imag_rad_log2=%g\n",
        (long)precision, finite, contains_zero, exponent(arb_midref(acb_realref(denominator[0]))),
        radius(arb_radref(acb_realref(denominator[0]))),
        exponent(arb_midref(acb_imagref(denominator[0]))),
        radius(arb_radref(acb_imagref(denominator[0]))));
  }
  return finite && !contains_zero;
}
}  // namespace sic::exact
