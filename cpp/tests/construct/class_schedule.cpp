// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/stages/table/class_schedule.hpp"

#include <future>
#include <latch>

#include "assertions.hpp"
#include "cpu_allowance_expectation.hpp"
#include "support/resources/platform_snapshot.hpp"

using sic::construct::ClassAllocation;
using sic::construct::dispatch_live_classes;
using sic::support::resources::Bytes;
namespace {
void live_admission() {
  sic::support::Reservations pool(100);
  std::latch first_started(1), third_started(1);
  std::atomic<int> active{0}, peak{0}, completed{0}, refused{0};
  const std::array<long, 3> classes{0, 1, 2};
  dispatch_live_classes(
      classes, 2, &pool,
      [](long, size_t, int live, int available) -> std::optional<ClassAllocation> {
        if (!available || live >= 2) return {};
        return ClassAllocation{2, 1, 50};
      },
      [](long, const ClassAllocation&) { return true; },
      [](long, const ClassAllocation&) { return Bytes(50); }, [&](long, Bytes) { ++refused; },
      [&](long c, const ClassAllocation& a,
          std::optional<sic::support::Reservations::Lease> grant) {
        if (!grant || grant->bytes() != 50 || a.workers != 1) throw std::runtime_error("grant");
        const int live = ++active;
        int old = peak.load();
        while (old < live && !peak.compare_exchange_weak(old, live)) {
        }
        if (c == 0) {
          first_started.count_down();
          third_started.wait();
        }
        if (c == 1) first_started.wait();
        if (c == 2) third_started.count_down();
        --active;
        ++completed;
      });
  SIC_CHECK(completed == 3 && refused == 0 && peak == 2 && pool.used() == 0);
}
void refusal_and_teardown() {
  const std::array<long, 3> classes{0, 1, 2};
  sic::support::Reservations pool(1);
  int refused = 0, callbacks = 0;
  dispatch_live_classes(
      classes, 1, &pool,
      [](long, size_t, int, int) {
        return std::optional(ClassAllocation{1, 1, 2});
      },
      [](long, const ClassAllocation&) { return true; },
      [](long, const ClassAllocation&) { return Bytes(2); }, [&](long, Bytes) { ++refused; },
      [&](long, const ClassAllocation&, auto) { ++callbacks; });
  SIC_CHECK(refused == 3 && callbacks == 0 && pool.used() == 0);
  std::atomic<int> running{0};
  bool caught = false;
  try {
    dispatch_live_classes(
        classes, 2, &pool,
        [](long, size_t, int, int free) -> std::optional<ClassAllocation> {
          if (!free) return {};
          return ClassAllocation{2, 1, 1};
        },
        [](long, const ClassAllocation&) { return true; },
        [](long, const ClassAllocation&) { return Bytes(1); }, [](long, Bytes) {},
        [&](long, const ClassAllocation&, auto grant) {
          ++running;
          SIC_CHECK(grant.has_value());
          --running;
          throw std::runtime_error("callback stop");
        });
  } catch (const std::runtime_error&) {
    caught = true;
  }
  SIC_CHECK(caught && running == 0 && pool.used() == 0);
  caught = false;
  try {
    dispatch_live_classes(
        classes, 2, &pool,
        [](long, size_t, int, int free) -> std::optional<ClassAllocation> {
          if (!free) return {};
          return ClassAllocation{2, 1, 1};
        },
        [](long c, const ClassAllocation&) {
          if (c == 1) throw std::runtime_error("seed");
          return true;
        },
        [](long, const ClassAllocation&) { return Bytes(1); }, [](long, Bytes) {},
        [&](long, const ClassAllocation&, auto) {
          ++running;
          --running;
        });
  } catch (const std::runtime_error&) {
    caught = true;
  }
  SIC_CHECK(caught && running == 0 && pool.used() == 0);
}
void pressure_and_tokens() {
  const std::array<long, 4> classes{0, 1, 2, 3};
  std::latch first_started(1), refused_attempt(1);
  bool signalled = false;
  sic::support::Reservations pool(100, [&](Bytes aggregate) {
    if (aggregate > 60) {
      if (!signalled) {
        signalled = true;
        refused_attempt.count_down();
      }
      return false;
    }
    return true;
  });
  std::array<std::unique_ptr<int>, 4> seeds;
  std::array<const int*, 4> identities{};
  int retries = 0;
  std::atomic<int> tokens{0};
  dispatch_live_classes(
      classes, 6, &pool,
      [](long, size_t remaining, int live, int free) {
        return sic::construct::class_allocation(6, std::min<int>(3, live + remaining), live, free,
                                                6);
      },
      [&](long c, const ClassAllocation& a) {
        const int total = tokens.fetch_add(a.workers) + a.workers;
        if (total > 6) throw std::runtime_error("seed CPU tokens");
        if (!seeds[c]) {
          seeds[c] = std::make_unique<int>(17);
          identities[c] = seeds[c].get();
        } else {
          ++retries;
          if (seeds[c].get() != identities[c]) throw std::runtime_error("seed moved");
        }
        tokens.fetch_sub(a.workers);
        return true;
      },
      [&](long c, const ClassAllocation&) {
        if (c == 1) {
          first_started.wait();
        }
        return Bytes(40);
      },
      [](long, Bytes) { throw std::runtime_error("unexpected final refusal"); },
      [&](long c, const ClassAllocation& a, auto grant) {
        if (!grant || seeds[c].get() != identities[c]) throw std::runtime_error("lost seed/grant");
        const int total = tokens.fetch_add(a.workers) + a.workers;
        if (total > 6) throw std::runtime_error("callback CPU tokens");
        {
          sic::exact::ClassNativeShare share(a.workers, 6, a.classes);
          if (flint_get_num_threads() != a.workers || sic::exact::nested_loop_budget != a.workers)
            throw std::runtime_error("TLS share");
          if (c == 0) {
            first_started.count_down();
            refused_attempt.wait();
          }
        }
        if (flint_get_num_threads() != 1) throw std::runtime_error("TLS restoration");
        tokens.fetch_sub(a.workers);
        seeds[c].reset();
      });
  SIC_CHECK(tokens == 0 && retries >= 1 && pool.used() == 0);
}
void live_seed_exception_and_cancellation() {
  const std::array<long, 2> classes{0, 1};
  for (const bool cancel : {false, true}) {
    std::latch started(1), release(1);
    std::atomic<int> running{0}, ended{0};
    sic::support::StopToken stop;
    sic::support::StopScope stop_scope(&stop);
    bool caught = false;
    try {
      dispatch_live_classes(
          classes, 2, nullptr,
          [](long, size_t, int, int free) -> std::optional<ClassAllocation> {
            if (!free) return {};
            return ClassAllocation{2, 1, 1};
          },
          [&](long c, const ClassAllocation&) {
            if (c == 1) {
              started.wait();
              if (cancel) stop.request_stop();
              release.count_down();
              if (!cancel) throw std::runtime_error("seed failure with live callback");
            }
            return true;
          },
          [](long, const ClassAllocation&) { return Bytes(1); }, [](long, Bytes) {},
          [&](long c, const ClassAllocation&, auto) {
            if (c == 0) {
              ++running;
              started.count_down();
              release.wait();
              --running;
              ++ended;
            }
            sic::support::check_stop();
          });
    } catch (const std::exception&) {
      caught = true;
    }
    SIC_CHECK(caught && running == 0 && ended == 1);
  }
}

void mixed_live_shares() {
  const std::array<long, 3> classes{0, 1, 2};
  std::latch first_started(1), last_started(1);
  std::atomic<int> tokens{0}, completed{0};
  dispatch_live_classes(
      classes, 6, nullptr,
      [](long c, size_t, int, int free) -> std::optional<ClassAllocation> {
        const ClassAllocation a = c == 2 ? ClassAllocation{2, 3, 0} : ClassAllocation{3, 2, 0};
        if (a.workers > free) return {};
        return a;
      },
      [&](long, const ClassAllocation& a) {
        if (tokens.load() + a.workers > 6) throw std::runtime_error("mixed seed CPU");
        return true;
      },
      [](long, const ClassAllocation&) { return Bytes(1); },
      [](long, Bytes) { throw std::runtime_error("mixed admission refused"); },
      [&](long c, const ClassAllocation& a, auto) {
        const int total = tokens.fetch_add(a.workers) + a.workers;
        if (total > 6) throw std::runtime_error("mixed callback CPU");
        {
          sic::exact::ClassNativeShare share(a.workers, 6, a.classes);
          thread_pool_handle* handles = nullptr;
          const slong count = flint_request_threads(&handles, a.workers);
          if (count != a.workers - 1) throw std::runtime_error("mixed native handles");
          if (c == 0) {
            first_started.count_down();
            last_started.wait();
          }
          if (c == 1) first_started.wait();
          if (c == 2) last_started.count_down();
          flint_give_back_threads(handles, count);
        }
        tokens.fetch_sub(a.workers);
        ++completed;
      });
  SIC_CHECK(tokens == 0 && completed == 3 && flint_get_num_threads() == 6);
}

}  // namespace
int main() {
  flint_set_num_threads(6);
  live_admission();
  pressure_and_tokens();
  mixed_live_shares();
  live_seed_exception_and_cancellation();
  refusal_and_teardown();
  SIC_CHECK(sic::support::resources::process_cpu_allowance() == sic_test::expected_cpu_allowance());
  flint_cleanup_master();
  return sic_test::report();
}
