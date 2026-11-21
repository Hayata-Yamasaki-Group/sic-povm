// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/newton/parallel_ball_product.hpp"

#include <flint/thread_pool.h>
#if defined(__linux__)
#include <sched.h>
#endif
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <system_error>
#include <thread>

#include "assertions.hpp"
#include "exact/runtime/memory/product_tiles.hpp"
#include "sic/support/flint_raii.hpp"
#include "sic/support/parse_number.hpp"

namespace {
using Matrix = sic::support::flint::AcbMat;

template <class F>
bool throws(F&& action) {
  try {
    action();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

// A caller tile waits for a helper, so fast scheduling cannot hide a serial run.
struct ParallelObservation {
  const std::thread::id caller = std::this_thread::get_id();
  std::atomic<bool> borrowed{false};
  bool parallel;
  sic::exact::BallProductHooks hooks;
  explicit ParallelObservation(bool expected) : parallel(expected) {
    hooks.context = this;
    hooks.before_tile = [](void* context, long) {
      auto& state = *static_cast<ParallelObservation*>(context);
      if (std::this_thread::get_id() != state.caller) {
        state.borrowed = true;
      } else if (state.parallel) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!state.borrowed.load()) {
          if (std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("parallel case ran on one worker");
          std::this_thread::yield();
        }
      }
    };
  }
  void check() const {
#ifdef SIC_ENABLE_TEST_HOOKS
    SIC_CHECK(borrowed.load() == parallel);
#endif
  }
};

int default_workers() {
#if defined(__linux__)
  cpu_set_t allowed;
  CPU_ZERO(&allowed);
  if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0)
    throw std::runtime_error("cannot discover test process CPU allowance");
  return std::max(3, CPU_COUNT(&allowed));
#else
  return std::max(3U, std::thread::hardware_concurrency());
#endif
}

void enclosure() {
  for (long n : {2L, 9L, 33L, 129L}) {
    Matrix left(n, n), right(n, n), center(n, n), exact(n, n), serial(n, n), parallel(n, n),
        saved(n, n);
    for (long r = 0; r < n; ++r)
      for (long c = 0; c < n; ++c) {
        auto a = acb_mat_entry(left.get(), r, c);
        auto b = acb_mat_entry(center.get(), r, c);
        acb_set_si(a, (17 * r + 11 * c) % 19 - 9);
        arb_set_si(acb_imagref(a), (7 * r + 13 * c) % 17 - 8);
        acb_set_si(b, (11 * r + 5 * c) % 23 - 11);
        arb_set_si(acb_imagref(b), (19 * r + 3 * c) % 13 - 6);
        acb_mul_2exp_si(a, a, c % 2 ? 256 : -256);
        acb_mul_2exp_si(b, b, r % 2 ? -256 : 256);
      }
    acb_mat_set(saved.get(), left.get());
    acb_mat_set(right.get(), center.get());
    for (long r = 0; r < n; ++r)
      for (long c = 0; c < n; ++c) {
        auto b = acb_mat_entry(right.get(), r, c);
        arb_add_error_2exp_si(acb_realref(b), -40 + (r % 2 ? -256 : 256));
        arb_add_error_2exp_si(acb_imagref(b), -43 + (r % 2 ? -256 : 256));
      }
    acb_mat_mul(exact.get(), left.get(), center.get(), 2048);
    SIC_CHECK(acb_mat_is_exact(exact.get()));
    // For point A and rectangular B, the real and imaginary extrema are
    // exact independent linear ranges. Include the entire rectangle, not
    // merely its centre or an overlapping reference enclosure.
    arb_t re, im, t;
    arb_init(re);
    arb_init(im);
    arb_init(t);
    for (long r = 0; r < n; ++r)
      for (long c = 0; c < n; ++c) {
        arb_zero(re);
        arb_zero(im);
        for (long k = 0; k < n; ++k) {
          const auto a = acb_mat_entry(left.get(), r, k);
          for (int part = 0; part < 2; ++part) {
            const auto component = part ? acb_imagref(a) : acb_realref(a);
            arb_set_arf(t, arb_midref(component));
            arb_abs(t, t);
            const long er = -40 + (k % 2 ? -256 : 256);
            const long ei = -43 + (k % 2 ? -256 : 256);
            arb_mul_2exp_si(t, t, part ? ei : er);
            arb_add(re, re, t, 2048);
            arb_set_arf(t, arb_midref(component));
            arb_abs(t, t);
            arb_mul_2exp_si(t, t, part ? er : ei);
            arb_add(im, im, t, 2048);
          }
        }
        SIC_CHECK(arb_is_exact(re));
        SIC_CHECK(arb_is_exact(im));
        auto value = acb_mat_entry(exact.get(), r, c);
        arb_add_error(acb_realref(value), re);
        arb_add_error(acb_imagref(value), im);
      }
    arb_clear(re);
    arb_clear(im);
    arb_clear(t);
    const int before = flint_get_num_threads();
    SIC_CHECK(sic::exact::parallel_ball_product(serial.get(), left.get(), right.get(), 192, 1,
                                                UINT64_MAX));
    SIC_CHECK(flint_get_num_threads() == before);
    SIC_CHECK(sic::exact::parallel_ball_product(parallel.get(), left.get(), right.get(), 192, 4,
                                                UINT64_MAX));
    SIC_CHECK(flint_get_num_threads() == before);
    SIC_CHECK(acb_mat_equal(serial.get(), parallel.get()));
    SIC_CHECK(acb_mat_contains(parallel.get(), exact.get()));
    SIC_CHECK(acb_mat_equal(left.get(), saved.get()));
    {
      sic::exact::FlintWorkerCap inherited(1);
      SIC_CHECK(sic::exact::parallel_ball_product(parallel.get(), left.get(), right.get(), 192, 4,
                                                  UINT64_MAX));
      SIC_CHECK(flint_get_num_threads() == 1);
      SIC_CHECK(acb_mat_equal(serial.get(), parallel.get()));
    }
    SIC_CHECK(flint_get_num_threads() == before);
  }
}

void tiles(bool full) {
  constexpr long n = 257, kPrecision = 192;
  Matrix left(n, n), right(n, n), reference(n, n), output(n, n);
  for (long r = 0; r < n; ++r)
    for (long c = 0; c < n; ++c) {
      auto a = acb_mat_entry(left.get(), r, c);
      auto b = acb_mat_entry(right.get(), r, c);
      acb_set_si(a, (17 * r + 5 * c) % 31 - 15);
      arb_set_si(acb_imagref(a), (3 * r + 7 * c) % 29 - 14);
      acb_set_si(b, (11 * r + 13 * c) % 37 - 18);
      arb_set_si(acb_imagref(b), (19 * r + c) % 41 - 20);
      arb_add_error_2exp_si(acb_realref(b), -80);
      arb_add_error_2exp_si(acb_imagref(b), -81);
    }
  const int before = flint_get_num_threads();
  SIC_CHECK(sic::exact::parallel_ball_product(reference.get(), left.get(), right.get(), kPrecision,
                                              1, UINT64_MAX));
  using namespace sic::support::resources;
  // Four simultaneous certification owners leave room for exactly one
  // tile crew here. A smaller planner grant must still execute identical
  // windows at every requested worker count, rather than refuse the helper.
  const auto one_tile = add(mul(4, mul(mul(n, n), complex_bytes(newton_matrix_bits(kPrecision)))),
                            sic::exact::product_tiles::scratch(n, 1, kPrecision));
  for (int workers : {1, 2, 8, 16, 32}) {
    for (auto budget : {one_tile, UINT64_MAX}) {
      const bool parallel = workers > 1 && budget == UINT64_MAX;
      ParallelObservation observation(parallel);
      if (parallel) {
        thread_pool_handle* handles = nullptr;
        const long available = flint_request_threads(&handles, workers);
        flint_give_back_threads(handles, available);
        SIC_CHECK(available > 0);  // Also enforced in builds with hooks compiled out.
      }
      acb_mat_one(output.get());  // Stale output must not influence any tile.
      SIC_CHECK(sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), kPrecision,
                                                  workers, budget, false, &observation.hooks));
      observation.check();
      SIC_CHECK(acb_mat_equal(reference.get(), output.get()));
      SIC_CHECK(flint_get_num_threads() == before);
    }
  }
  acb_mat_one(output.get());
  SIC_CHECK(throws([&] {
    sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), kPrecision, 32,
                                      one_tile - 1);
  }));
  SIC_CHECK(acb_mat_is_zero(output.get()));
  SIC_CHECK(flint_get_num_threads() == before);
  if (!full) {
#ifndef SIC_ENABLE_TEST_HOOKS
    sic::exact::BallProductHooks ignored;
    ignored.team_start = [](void*, int) { throw std::runtime_error("OFF team callback"); };
    ignored.before_tile = [](void*, long) { throw std::runtime_error("OFF before callback"); };
    ignored.after_tile = [](void*, long) { throw std::runtime_error("OFF after callback"); };
    SIC_CHECK(sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), kPrecision,
                                                8, UINT64_MAX, false, &ignored));
    SIC_CHECK(acb_mat_equal(reference.get(), output.get()));
    Matrix small(2, 2), result(2, 2);
    acb_mat_one(small.get());
    SIC_CHECK(sic::exact::parallel_ball_product(result.get(), small.get(), small.get(), kPrecision,
                                                2, UINT64_MAX, false, &ignored));
    SIC_CHECK(acb_mat_equal(result.get(), small.get()));
#endif
    return;
  }
  sic::exact::BallProductHooks hooks;
  hooks.team_start = [](void*, int) {
    throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
  };
  SIC_CHECK(sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), kPrecision, 8,
                                              UINT64_MAX, false, &hooks));
  SIC_CHECK(acb_mat_equal(reference.get(), output.get()));
  SIC_CHECK(flint_get_num_threads() == before);
  hooks.team_start = nullptr;
  hooks.before_tile = [](void*, long task) {
    if (task == 1) throw std::runtime_error("injected tile failure");
  };
  SIC_CHECK(throws([&] {
    sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), kPrecision, 8,
                                      UINT64_MAX, false, &hooks);
  }));
  SIC_CHECK(acb_mat_is_zero(output.get()));
  SIC_CHECK(flint_get_num_threads() == before);
  hooks.before_tile = nullptr;
  hooks.context = output.get();
  hooks.after_tile = [](void* state, long task) {
    if (task == 0) acb_indeterminate(acb_mat_entry(static_cast<acb_mat_struct*>(state), 0, 0));
  };
  SIC_CHECK(!sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), kPrecision, 8,
                                               UINT64_MAX, false, &hooks));
  SIC_CHECK(acb_mat_is_zero(output.get()));
  SIC_CHECK(flint_get_num_threads() == before);
  char path[] = "/tmp/sic-tile-stop-XXXXXX";
  const int descriptor = mkstemp(path);
  SIC_CHECK(descriptor >= 0);
  if (descriptor >= 0) {
    close(descriptor);
    unlink(path);
    hooks.context = path;
    hooks.after_tile = [](void* state, long task) {
      if (task == 0) {
        FILE* file = fopen(static_cast<char*>(state), "w");
        if (!file) throw std::runtime_error("stop fixture creation failed");
        fclose(file);
      }
    };
    sic::support::ScopedStopFile stop_scope(path);
    SIC_CHECK(throws([&] {
      sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), kPrecision, 8,
                                        UINT64_MAX, false, &hooks);
    }));
    SIC_CHECK(acb_mat_is_zero(output.get()));
    SIC_CHECK(flint_get_num_threads() == before);
    unlink(path);
  }
}

void small_budget_cap() {
  constexpr long n = 9, kPrecision = 64;
  Matrix left(n, n), right(n, n), output(n, n);
  acb_mat_one(left.get());
  acb_mat_one(right.get());
  using namespace sic::support::resources;
  const auto limit = add(mul(4, mul(mul(n, n), complex_bytes(newton_matrix_bits(kPrecision)))),
                         sic::exact::product_tiles::scratch(n, 1, kPrecision));
  int observed = 0;
  sic::exact::BallProductHooks hooks;
  hooks.context = &observed;
  hooks.before_tile = [](void* state, long) {
    *static_cast<int*>(state) = flint_get_num_threads();
  };
  const int before = flint_get_num_threads();
  SIC_CHECK(sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), kPrecision, 32,
                                              limit, false, &hooks));
  SIC_CHECK(observed == 1);
  SIC_CHECK(flint_get_num_threads() == before);
  SIC_CHECK(acb_mat_equal(output.get(), left.get()));
}

void linear_certificates() {
  // Independent linear-system oracle: F(x)=J*x-c, Y=I. At a proposed
  // center, deviation is exactly |YF|. Both identification and nested tight
  // boxes must satisfy the same strict componentwise sufficient inequality.
  constexpr long n = 129;
  Matrix inverse(n, n), jacobian(n, n), product(n, n);
  acb_mat_one(inverse.get());
  auto accepted = [&](long radius_exponent, long deviation_exponent) {
    SIC_CHECK(sic::exact::parallel_ball_product(product.get(), inverse.get(), jacobian.get(), 192,
                                                8, UINT64_MAX));
    sic::support::flint::Mag radius, deviation, row_sum, entry, bound;
    mag_set_ui_2exp_si(radius, 1, radius_exponent);
    if (deviation_exponent == WORD_MIN)
      mag_zero(deviation);
    else
      mag_set_ui_2exp_si(deviation, 1, deviation_exponent);
    bool strict = true;
    for (long r = 0; r < n; ++r) {
      mag_zero(row_sum);
      for (long c = 0; c < n; ++c) {
        auto value = acb_mat_entry(product.get(), r, c);
        acb_neg(value, value);
        if (r == c) acb_add_ui(value, value, 1, 192);
        acb_get_mag(entry, value);
        mag_add(row_sum, row_sum, entry);
      }
      mag_mul(bound, row_sum, radius);
      mag_mul_2exp_si(bound, bound, 1);
      mag_add(bound, bound, deviation);
      strict = strict && mag_cmp(bound, radius) < 0;
    }
    return strict;
  };
  acb_mat_one(jacobian.get());
  for (long r = 0; r < n; ++r)
    arb_add_error_2exp_si(acb_realref(acb_mat_entry(jacobian.get(), r, r)), -8);
  SIC_CHECK(accepted(0, -2));  // B0 contains the complete linear root enclosure.
  SIC_CHECK(!accepted(0, 0));  // B0 center deviation reaches its boundary.
  acb_mat_one(jacobian.get());
  SIC_CHECK(accepted(-16, -18));   // A strictly nested tiny B1.
  SIC_CHECK(!accepted(-16, -16));  // Equality is never a strict B1 certificate.
  acb_mat_zero(jacobian.get());
  SIC_CHECK(!accepted(0, WORD_MIN));  // Singular constant system is not certified.
  acb_mat_one(jacobian.get());
  acb_mat_scalar_mul_2exp_si(jacobian.get(), jacobian.get(), 1);
  SIC_CHECK(!accepted(0, WORD_MIN));  // YJ=2I is noncontractive for this Y.
}

void native_leases_and_scopes(bool full) {
  constexpr long n = 129;
  Matrix left(n, n), right(n, n), reference(n, n), output(n, n);
  acb_mat_one(left.get());
  acb_mat_one(right.get());
  SIC_CHECK(sic::exact::parallel_ball_product(reference.get(), left.get(), right.get(), 192, 1,
                                              UINT64_MAX));
  // Hold every available helper, then leave exactly two for the nested request.
  // Neither availability condition may change any output arithmetic.
  for (int request : {flint_get_num_threads(), std::max(1, flint_get_num_threads() - 2)}) {
    thread_pool_handle* handles = nullptr;
    const long held = flint_request_threads(&handles, request);
    struct Lease {
      thread_pool_handle* handles;
      long count;
      ~Lease() { flint_give_back_threads(handles, count); }
    } lease{handles, held};
    SIC_CHECK(held == request - 1);
    const bool parallel = request < flint_get_num_threads();
    ParallelObservation observation(parallel);
    SIC_CHECK(sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), 192, 8,
                                                UINT64_MAX, false, &observation.hooks));
    observation.check();
    SIC_CHECK(acb_mat_equal(reference.get(), output.get()));
  }
  if (!full) return;
  sic::support::StopToken token;
  sic::support::KernelSettings settings{{"tile-scope", "present"}};
  sic::support::ProgressSink progress{[](const std::string&) {}, {}};
  struct Expected {
    sic::support::StopToken* token;
    const sic::support::KernelSettings* settings;
    sic::support::ProgressSink* progress;
    std::thread::id caller = std::this_thread::get_id();
    std::atomic<bool> borrowed_started{false}, borrowed_finished{false};
    std::atomic<bool> correct{true};
    bool borrowing = flint_get_num_threads() > 1;
  } state{&token, &settings, &progress};
  SIC_CHECK(state.borrowing);  // Borrowed-worker/TLS assertions must never be vacuous.
  sic::exact::BallProductHooks hooks;
  hooks.context = &state;
  hooks.before_tile = [](void* context, long) {
    auto& expected = *static_cast<Expected*>(context);
    if (std::this_thread::get_id() != expected.caller) {
      expected.borrowed_started = true;
      return;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (expected.borrowing && !expected.borrowed_started.load()) {
      if (std::chrono::steady_clock::now() > deadline)
        throw std::runtime_error("borrowed tile worker did not enter scope fixture");
      std::this_thread::yield();
    }
  };
  hooks.after_tile = [](void* context, long) {
    auto& expected = *static_cast<Expected*>(context);
    if (std::this_thread::get_id() != expected.caller) expected.borrowed_finished = true;
    if (sic::support::current_stop_token != expected.token ||
        sic::support::current_kernel_settings != expected.settings ||
        sic::support::current_progress_sink != expected.progress ||
        !sic::support::admission::logging || flint_get_num_threads() != 1)
      expected.correct = false;
  };
  const auto* old_settings = sic::support::current_kernel_settings;
  auto* old_stop = sic::support::current_stop_token;
  auto* old_progress = sic::support::current_progress_sink;
  const bool old_logging = sic::support::admission::logging;
  {
    sic::support::StopScope stop(&token);
    sic::support::KernelSettingsScope selected(&settings);
    sic::support::ProgressScope sink(&progress);
    sic::support::admission::LogScope logging(true);
    SIC_CHECK(sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), 192, 8,
                                                UINT64_MAX, false, &hooks));
    SIC_CHECK(state.correct.load());
    SIC_CHECK(state.borrowed_finished.load() == state.borrowing);
    SIC_CHECK(acb_mat_equal(reference.get(), output.get()));
  }
  SIC_CHECK(sic::support::current_stop_token == old_stop);
  SIC_CHECK(sic::support::current_kernel_settings == old_settings);
  SIC_CHECK(sic::support::current_progress_sink == old_progress);
  SIC_CHECK(sic::support::admission::logging == old_logging);
  // Observe every borrowed worker after return to detect dangling per-request
  // TLS pointers; the raw FLINT callback deliberately installs no project scopes.
  thread_pool_handle* handles = nullptr;
  const long held = flint_request_threads(&handles, flint_get_num_threads());
  auto inspect = +[](void* context) {
    auto& expected = *static_cast<Expected*>(context);
    if (sic::support::current_stop_token == expected.token ||
        sic::support::current_kernel_settings == expected.settings ||
        sic::support::current_progress_sink == expected.progress ||
        sic::support::admission::logging)
      expected.correct = false;
  };
  for (long index = 0; index < held; ++index)
    thread_pool_wake(global_thread_pool, handles[index], 0, inspect, &state);
  for (long index = 0; index < held; ++index) thread_pool_wait(global_thread_pool, handles[index]);
  flint_give_back_threads(handles, held);
  SIC_CHECK(state.correct.load());
}

void failures() {
  Matrix left(3, 3), right(3, 3), output(3, 3), saved(3, 3);
  acb_mat_one(left.get());
  acb_mat_one(right.get());
  acb_mat_set(saved.get(), left.get());
  const int before = flint_get_num_threads();
  SIC_CHECK(throws([&] {
    sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), 192, 2, 1);
  }));
  SIC_CHECK(acb_mat_is_zero(output.get()));
  SIC_CHECK(flint_get_num_threads() == before);
  SIC_CHECK(throws([&] {
    sic::exact::parallel_ball_product(left.get(), left.get(), right.get(), 192, 2, UINT64_MAX);
  }));
  SIC_CHECK(acb_mat_equal(left.get(), saved.get()));
  acb_mat_t alias;
  acb_mat_window_init(alias, left.get(), 0, 0, 3, 3);
  SIC_CHECK(throws([&] {
    sic::exact::parallel_ball_product(alias, left.get(), right.get(), 192, 2, UINT64_MAX);
  }));
  acb_mat_window_clear(alias);
  SIC_CHECK(acb_mat_equal(left.get(), saved.get()));
  SIC_CHECK(throws([&] {
    sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), 192, 0, UINT64_MAX);
  }));
  acb_indeterminate(acb_mat_entry(right.get(), 0, 0));
  SIC_CHECK(!sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), 192, 2,
                                               UINT64_MAX));
  SIC_CHECK(acb_mat_is_zero(output.get()));
  SIC_CHECK(flint_get_num_threads() == before);
  acb_mat_one(right.get());
  fmpz_set_ui(ARF_EXPREF(arb_midref(acb_realref(acb_mat_entry(right.get(), 0, 0)))), 1);
  fmpz_mul_2exp(ARF_EXPREF(arb_midref(acb_realref(acb_mat_entry(right.get(), 0, 0)))),
                ARF_EXPREF(arb_midref(acb_realref(acb_mat_entry(right.get(), 0, 0)))), 100);
  SIC_CHECK(throws([&] {
    sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), 192, 2, UINT64_MAX);
  }));
  SIC_CHECK(acb_mat_is_zero(output.get()));
  SIC_CHECK(flint_get_num_threads() == before);
  acb_mat_one(right.get());
  arb_const_pi(acb_realref(acb_mat_entry(right.get(), 0, 0)), 2048);
  SIC_CHECK(throws([&] {
    sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), 64, 2, UINT64_MAX);
  }));
  SIC_CHECK(acb_mat_is_zero(output.get()));
  SIC_CHECK(flint_get_num_threads() == before);
  acb_mat_one(right.get());
  char path[] = "/tmp/sic-ball-product-stop-XXXXXX";
  const int descriptor = mkstemp(path);
  SIC_CHECK(descriptor >= 0);
  if (descriptor >= 0) {
    close(descriptor);
    sic::support::ScopedStopFile stop_scope(path);
    SIC_CHECK(throws([&] {
      sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), 192, 2, UINT64_MAX);
    }));
    SIC_CHECK(acb_mat_is_zero(output.get()));
    SIC_CHECK(flint_get_num_threads() == before);

    unlink(path);
  }
}

void wider_point() {
  Matrix left(3, 3), right(3, 3), output(3, 3), reference(3, 3);
  acb_mat_one(left.get());
  acb_mat_one(right.get());
  arb_const_pi(acb_realref(acb_mat_entry(left.get(), 0, 0)), 484);
  acb_get_mid(acb_mat_entry(left.get(), 0, 0), acb_mat_entry(left.get(), 0, 0));
  SIC_CHECK(acb_bits(acb_mat_entry(left.get(), 0, 0)) > 423);
  arb_add_error_2exp_si(acb_realref(acb_mat_entry(right.get(), 0, 0)), -200);
  SIC_CHECK(
      sic::exact::parallel_ball_product(output.get(), left.get(), right.get(), 423, 4, UINT64_MAX));
  acb_mat_mul(reference.get(), left.get(), right.get(), 423);
  SIC_CHECK(acb_mat_contains(output.get(), reference.get()));
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3 || (std::string(argv[1]) != "full" && std::string(argv[1]) != "arithmetic"))
    return 2;
  const bool full = std::string(argv[1]) == "full";
#ifndef SIC_ENABLE_TEST_HOOKS
  if (full) return 2;
#endif
  const int workers = std::string(argv[2]) == "auto"
                          ? default_workers()
                          : sic::support::parse_number<int>(argv[2], "test workers",
                                                            sic::ErrorKind::invalid_argument, 1);
  flint_set_num_threads(workers);
  std::printf("Parallel-product fixture: native workers=%d\n", flint_get_num_threads());
  if (flint_get_num_threads() < 3) {
    std::fprintf(stderr,
                 "Parallel-product coverage requires at least 3 native workers; "
                 "explicit SIC_TEST_WORKERS was honoured unchanged\n");
    return 1;
  }
  enclosure();
  tiles(full);
  if (full) small_budget_cap();
  linear_certificates();
  native_leases_and_scopes(full);
  wider_point();
  failures();
  return sic_test::report();
}
