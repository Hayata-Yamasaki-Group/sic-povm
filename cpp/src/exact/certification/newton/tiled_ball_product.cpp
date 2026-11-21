// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/newton/tiled_ball_product.hpp"

#include <flint/thread_pool.h>

#include <atomic>
#include <exception>
#include <mutex>

#include "exact/runtime/memory/product_tiles.hpp"
#include "exact/runtime/parallel.hpp"
#include "sic/support/flint_raii.hpp"
#include "support/runtime/worker_context.hpp"

namespace sic::exact {
void tiled_ball_product(acb_mat_t output, const acb_mat_t left, const acb_mat_t right,
                        slong precision, int workers, const BallProductHooks* hooks) {
#ifndef SIC_ENABLE_TEST_HOOKS
  (void)hooks;
#endif
  const long n = acb_mat_nrows(left), axis = 1 + (n - 1) / product_tiles::kSide;
  const long tasks = product_tiles::count(n);
  FlintWorkerCap admitted(workers);
  thread_pool_handle* handles = nullptr;
  long native_workers = 0;
  try {
#ifdef SIC_ENABLE_TEST_HOOKS
    if (hooks && hooks->team_start) hooks->team_start(hooks->context, workers);
#endif
    native_workers = flint_request_threads(&handles, workers);
  } catch (const std::system_error&) {
    // Startup has not published any tile. Execute the same windows serially.
  }
  struct Lease {
    thread_pool_handle* handles;
    long count;
    ~Lease() { flint_give_back_threads(handles, count); }
  } lease{handles, native_workers};
  auto tile = [&](long task) {
    sic::support::check_stop();
#ifdef SIC_ENABLE_TEST_HOOKS
    if (hooks && hooks->before_tile) hooks->before_tile(hooks->context, task);
#endif
    sic::support::check_stop();
    const long row = task / axis * product_tiles::kSide;
    const long column = task % axis * product_tiles::kSide;
    const long end_row = std::min(n, row + product_tiles::kSide);
    const long end_column = std::min(n, column + product_tiles::kSide);
    acb_mat_t a, b, c;
    acb_mat_window_init(a, left, row, 0, end_row, n);
    acb_mat_window_init(b, right, 0, column, n, end_column);
    acb_mat_window_init(c, output, row, column, end_row, end_column);
    struct Windows {
      acb_mat_struct *a, *b, *c;
      ~Windows() {
        acb_mat_window_clear(c);
        acb_mat_window_clear(b);
        acb_mat_window_clear(a);
      }
    } windows{a, b, c};
    FlintWorkerCap one(1);
    acb_mat_mul(c, a, b, precision);
#ifdef SIC_ENABLE_TEST_HOOKS
    if (hooks && hooks->after_tile) hooks->after_tile(hooks->context, task);
#endif
    sic::support::check_stop();
  };
  // Lease only workers within the inherited class share. Their existing
  // allocator arenas are reused rather than creating a second native team
  // for each product. An unavailable lease changes scheduling, never tiles.
  struct Dispatch {
    decltype(tile)& operation;
    long tasks;
    std::atomic<long> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr failure;
    std::mutex mutex;
    sic::support::WorkerContext context = sic::support::WorkerContext::capture();
  } dispatch{tile, tasks};
  auto execute = +[](void* argument) {
    auto& state = *static_cast<Dispatch*>(argument);
    sic::support::WorkerContextScope context(state.context);
    sic::support::flint::WorkerCleanup cleanup;
    try {
      while (!state.failed.load(std::memory_order_relaxed)) {
        const long task = state.next.fetch_add(1, std::memory_order_relaxed);
        if (task >= state.tasks) break;
        state.operation(task);
      }
    } catch (...) {
      std::lock_guard lock(state.mutex);
      if (!state.failure) state.failure = std::current_exception();
      state.failed.store(true, std::memory_order_relaxed);
    }
  };
  for (long worker = 0; worker < native_workers; ++worker)
    thread_pool_wake(global_thread_pool, handles[worker], 0, execute, &dispatch);
  execute(&dispatch);
  for (long worker = 0; worker < native_workers; ++worker)
    thread_pool_wait(global_thread_pool, handles[worker]);
  if (dispatch.failure) std::rethrow_exception(dispatch.failure);
}
}  // namespace sic::exact
