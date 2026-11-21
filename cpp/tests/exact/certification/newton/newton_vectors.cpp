// SPDX-License-Identifier: AGPL-3.0-or-later
#include <unistd.h>

#include <cstdint>

#include "assertions.hpp"
#include "exact/certification/reconstruction/ghost.hpp"

namespace {
void residual_case(long d, slong bits, int sign, long k, bool cached) {
  sic::exact::GhostRefiner serial(d, sign, 3), parallel(d, sign, 3);
  serial.k = parallel.k = k;
  serial.kernel_threads = 1;
  parallel.kernel_threads = 4;
  const long rows = std::max(97L, 2 * d - 2);
  serial.S.resize(rows);
  for (long r = 0; r < rows; ++r) serial.S[r] = (r * 17 + r / 7) % (d * d - 1) + 1;
  parallel.S = serial.S;
  serial.Kexp.resize(d * d);
  for (long y = 0; y < d * d; ++y) serial.Kexp[y] = (y * 37) % (4 * d * d);
  parallel.Kexp = serial.Kexp;
  sic::support::ComplexBallVector state(2 * d - 2), expected(rows), actual(rows), roots(rows);
  for (size_t q = 0; q < state.size(); ++q) {
    acb_set_si(state[q], (long)(q % 13) - 6);
    arb_set_si(acb_imagref(state[q]), (long)(q % 7) - 3);
    acb_mul_2exp_si(state[q], state[q], -8);
    arb_add_error_2exp_si(acb_realref(state[q]), -bits + 5);
    arb_add_error_2exp_si(acb_imagref(state[q]), -bits + 7);
  }
  sic::exact::GhostRefiner::Ctx high(d, sign, bits + 64), context(high, bits);
  if (cached) {
    for (long r = 0; r < rows; ++r) serial.root_k(roots[r], bits + 64, serial.Kexp[serial.S[r]]);
    context.Kc = &roots.v;
  }
  serial.residual(expected.v, context, state.v);
  parallel.residual(actual.v, context, state.v);
  for (long r = 0; r < rows; ++r) {
    SIC_CHECK(acb_equal(expected[r], actual[r]));
    SIC_CHECK(acb_is_finite(actual[r]));
  }
}

double original_correction(const acb_mat_t inverse, sic::support::ComplexBallVector& F,
                           sic::support::ComplexBallVector& z, sic::support::ComplexBallVector& dz,
                           slong precision, slong q) {
  const long count = z.size();
  double maximum = -1e9;
  for (long r = 0; r < count; ++r) acb_set_round(F[r], F[r], q);
  for (long r = 0; r < count; ++r) {
    acb_approx_dot(dz[r], nullptr, 0, acb_mat_entry(inverse, r, 0), 1, F.v.data(), 1, count, q);
    acb_sub(z[r], z[r], dz[r], precision);
    acb_get_mid(z[r], z[r]);
    mag_t m, mz;
    mag_init(m);
    mag_init(mz);
    acb_get_mag(m, dz[r]);
    acb_get_mag(mz, z[r]);
    if (!mag_is_zero(m) && !mag_is_zero(mz))
      maximum = std::max(maximum, mag_get_d_log2_approx(m) - mag_get_d_log2_approx(mz));
    mag_clear(m);
    mag_clear(mz);
  }
  return maximum;
}

void correction_case(long d, slong bits, int kind, int workers) {
  const long count = 2 * d - 2;
  sic::exact::GhostRefiner refiner(d, 1, 1);
  refiner.kernel_threads = workers;
  sic::support::ComplexBallVector F(count), z(count), dz_expected(count), dz_actual(count);
  sic::support::flint::AcbMat inverse(count, count);
  for (long r = 0; r < count; ++r) {
    acb_set_si(F[r], r % 9 - 4);
    arb_set_si(acb_imagref(F[r]), r % 5 - 2);
    arb_add_error_2exp_si(acb_realref(F[r]), -bits + 5);
    acb_set_si(z[r], r % 11 - 5);
    arb_set_si(acb_imagref(z[r]), r % 3 - 1);
    arb_add_error_2exp_si(acb_imagref(z[r]), -bits + 7);
    if (kind == 1) acb_zero(F[r]);
    if (kind == 2) acb_zero(z[r]);
    for (long c = 0; c < count; ++c) {
      auto value = acb_mat_entry(inverse, r, c);
      if (kind == 2 || r % 13 == 0) continue;
      acb_set_si(value, (r * 17 + c * 19) % 23 - 11);
      arb_set_si(acb_imagref(value), (r * 7 + c) % 13 - 6);
      acb_mul_2exp_si(value, value, -6);
    }
  }
  sic::support::ComplexBallVector expected_F(F), actual_F(F), expected_z(z), actual_z(z);
  const slong q = std::min<slong>(bits, 160);
  const auto expected = original_correction(inverse, expected_F, expected_z, dz_expected, bits, q);
  const auto actual = refiner.correction(inverse, actual_F.v, actual_z.v, dz_actual.v, bits, q);
  SIC_CHECK(expected == actual);
  for (long r = 0; r < count; ++r) {
    SIC_CHECK(acb_equal(expected_F[r], actual_F[r]));
    SIC_CHECK(acb_equal(expected_z[r], actual_z[r]));
    SIC_CHECK(acb_equal(dz_expected[r], dz_actual[r]));
  }
}

void stop_cases() {
  char path[] = "/tmp/sic-vector-stop-XXXXXX";
  const int descriptor = mkstemp(path);
  SIC_CHECK(descriptor >= 0);
  if (descriptor < 0) return;
  close(descriptor);
  sic::support::ScopedStopFile stop_scope(path);
  sic::exact::GhostRefiner refiner(64, 1, 1);
  sic::exact::GhostRefiner::Ctx context(64, 1, 80);
  sic::support::ComplexBallVector F(126), z(126), dz(126);
  sic::support::flint::AcbMat inverse(126, 126);
  refiner.S.assign(126, 1);
  for (int workers : {1, 4}) {
    refiner.kernel_threads = workers;
    bool residual_stopped = false, correction_stopped = false;
    try {
      refiner.residual(F.v, context, z.v);
    } catch (const std::runtime_error&) {
      residual_stopped = true;
    }
    try {
      refiner.correction(inverse, F.v, z.v, dz.v, 80, 80);
    } catch (const std::runtime_error&) {
      correction_stopped = true;
    }
    SIC_CHECK(residual_stopped);
    SIC_CHECK(correction_stopped);
    for (size_t r = 0; r < F.size(); ++r) {
      SIC_CHECK(acb_is_zero(F[r]));
      SIC_CHECK(acb_is_zero(z[r]));
      SIC_CHECK(acb_is_zero(dz[r]));
    }
  }
  unlink(path);
}

void reused_scalar_workspace() {
  constexpr long d = 32, kRows = 97, kColumns = 2 * d - 2;
  sic::exact::GhostRefiner serial(d, -1, 3), parallel(d, -1, 3);
  serial.S.resize(kRows);
  parallel.S.resize(kRows);
  serial.Kexp.resize(d * d);
  parallel.Kexp.resize(d * d);
  sic::support::ComplexBallVector state(kColumns), expected(kRows), actual(kRows), roots(kRows);
  sic::support::flint::AcbMat expected_jacobian(kRows, kColumns), actual_jacobian(kRows, kColumns);
  const int workers[] = {4, 4, 4, 4, 2, 1, 4};
  const slong bits[] = {80, 80, 512, 80, 80, 80, 512};
  const auto compare = [&] {
    for (long row = 0; row < kRows; ++row) SIC_CHECK(acb_equal(expected[row], actual[row]));
  };
  const auto poison = [&] {
    if (parallel.vector_workspace)
      for (auto& value : parallel.vector_workspace->scratch.v) acb_indeterminate(&value);
  };
  sic::exact::RowWorkerTeam* previous_crew = nullptr;
  int previous_workers = 0;
  for (int pass = 0; pass < 7; ++pass) {
    serial.k = parallel.k = (pass * 3) % d;
    parallel.kernel_threads = workers[pass];
    for (long row = 0; row < kRows; ++row)
      serial.S[row] = parallel.S[row] = (row * 17 + pass * 13) % (d * d - 1) + 1;
    for (long y = 0; y < d * d; ++y)
      serial.Kexp[y] = parallel.Kexp[y] = (y * 37 + pass) % (4 * d * d);
    for (long q = 0; q < kColumns; ++q) {
      acb_set_si(state[q], (q % 11) - 5 + pass);
      arb_set_si(acb_imagref(state[q]), (q % 7) - 3);
      acb_mul_2exp_si(state[q], state[q], -12);
      arb_add_error_2exp_si(acb_realref(state[q]), -bits[pass] + 5);
      arb_add_error_2exp_si(acb_imagref(state[q]), -bits[pass] + 7);
    }
    sic::exact::GhostRefiner::Ctx context(d, -1, bits[pass]);
    if (pass % 2) {
      for (long row = 0; row < kRows; ++row)
        serial.root_k(roots[row], bits[pass] + 64, serial.Kexp[serial.S[row]]);
      context.Kc = &roots.v;
    }
    serial.residual_serial(expected.v, context, state.v);
    parallel.residual(actual.v, context, state.v);
    compare();
    auto* crew = parallel.vector_workspace ? parallel.vector_workspace->team.get() : nullptr;
    SIC_CHECK((crew != nullptr) == (workers[pass] > 1));
    if (crew && workers[pass] == previous_workers) SIC_CHECK(crew == previous_crew);
    previous_crew = crew;
    previous_workers = workers[pass];
    poison();
    parallel.residual(actual.v, context, state.v);
    compare();
    poison();
    serial.jacobian_serial(expected_jacobian, context, state.v, serial.S);
    parallel.jacobian(actual_jacobian, context, state.v, parallel.S);
    SIC_CHECK(acb_mat_equal(expected_jacobian, actual_jacobian));
    if (crew) {
      SIC_CHECK(parallel.vector_workspace->team.get() == crew);
      SIC_CHECK(parallel.vector_workspace->precision == bits[pass]);
      SIC_CHECK(parallel.vector_workspace->scratch.size() ==
                workers[pass] * sic::exact::GhostRefiner::kRowScratchStride);
      const auto* data = parallel.vector_workspace->scratch.v.data();
      for (int worker = 0; worker + 1 < workers[pass]; ++worker) {
        const auto last = reinterpret_cast<std::uintptr_t>(
                              data + worker * sic::exact::GhostRefiner::kRowScratchStride +
                              sic::exact::GhostRefiner::kRowScratchSlots) -
                          1;
        const auto next = reinterpret_cast<std::uintptr_t>(
            data + (worker + 1) * sic::exact::GhostRefiner::kRowScratchStride);
        SIC_CHECK(last / 64 < next / 64);
      }
    }
    poison();
    parallel.residual(actual.v, context, state.v);
    compare();
    // Context and cached-root views die here; only scalar slots/crew persist.
  }
  parallel.kernel_threads = 4;
  sic::exact::GhostRefiner::Ctx context(d, -1, 80);
  sic::support::KernelSettings normal{{"SIC_MEMORY_MB", "100000"}};
  sic::support::KernelSettingsScope normal_scope(&normal);
  serial.residual_serial(expected.v, context, state.v);
  parallel.residual(actual.v, context, state.v);
  parallel.vector_attempt = sic::exact::GhostRefiner::VectorAttemptContext{
      sic::support::current_kernel_settings, parallel.workspace_limit,
      parallel.callback_workspace_limit};
  parallel.residual(actual.v, context, state.v);
  compare();
  {
    sic::support::KernelSettings larger{{"SIC_MEMORY_MB", "200000"}};
    sic::support::KernelSettingsScope scope(&larger);
    sic::exact::GhostRefiner::Ctx higher(d, -1, 512);
    serial.residual_serial(expected.v, higher, state.v);
    parallel.residual(actual.v, higher, state.v);
    compare();
    SIC_CHECK(!parallel.vector_attempt);
  }
  // A -> B -> A must keep the invalidated authority disabled.
  serial.residual_serial(expected.v, context, state.v);
  parallel.residual(actual.v, context, state.v);
  compare();
  SIC_CHECK(!parallel.vector_attempt);
  {
    sic::support::KernelSettings small{{"SIC_MEMORY_MB", "1"}};
    sic::support::KernelSettingsScope scope(&small);
    parallel.residual(actual.v, context, state.v);
    SIC_CHECK(!parallel.vector_workspace);
    compare();
  }
  parallel.vector_attempt.reset();
  parallel.residual(actual.v, context, state.v);
  SIC_CHECK(parallel.vector_workspace != nullptr);
  parallel.correction_passes = -1;
  std::vector<acb_struct> output;
  std::string why;
  bool caught = false;
  try {
    parallel.refine({}, 128, output, why);
  } catch (const std::invalid_argument&) {
    caught = true;
  }
  SIC_CHECK(caught);
  SIC_CHECK(!parallel.vector_workspace);
  SIC_CHECK(!parallel.vector_attempt);
  parallel.correction_passes = 2;
  parallel.residual(actual.v, context, state.v);
  SIC_CHECK(!parallel.refine({}, 0, output, why));
  SIC_CHECK(!parallel.vector_workspace);
  SIC_CHECK(!parallel.vector_attempt);
}
}  // namespace

int main() {
  for (long d : {8, 16, 64})
    for (slong bits : {80, 512, 2048})
      for (int sign : {-1, 1})
        for (long k : {0L, d / 2, d - 1})
          for (bool cached : {false, true}) residual_case(d, bits, sign, k, cached);
  for (long d : {8, 64, 128})
    for (slong bits : {80, 512, 2048})
      for (int kind = 0; kind < 3; ++kind)
        for (int workers : {1, 4}) correction_case(d, bits, kind, workers);
  stop_cases();
  reused_scalar_workspace();
  return sic_test::report();
}
