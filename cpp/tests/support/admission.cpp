// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/admission.hpp"

#include <atomic>
#include <chrono>
#include <future>

#include "assertions.hpp"
#include "decode/pool.hpp"
#include "sic/support/thread_group.hpp"

int main() {
  namespace admission = sic::support::admission;
  using namespace std::chrono_literals;
  // Reservation refusal must leave accounting unchanged. A waiting task must
  // wake when the admitted owner releases its grant, and oversized tasks refuse.
  sic::decode::MemoryBudget budget(4096, 2);
  budget.rebase(128);
  SIC_CHECK(budget.try_reserve(256));
  const auto before = budget.available();
  SIC_CHECK(!budget.try_reserve(UINT64_MAX));
  SIC_CHECK(budget.available() == before);
  budget.acquire(3000, 0);
  std::promise<void> entering;
  auto entered = entering.get_future();
  auto waiter = std::async(std::launch::async, [&] {
    entering.set_value();
    budget.acquire(512, 1);
    budget.release(512, 1);
  });
  entered.wait();
  SIC_CHECK(waiter.wait_for(20ms) == std::future_status::timeout);
  budget.release(3000, 0);
  SIC_CHECK(waiter.wait_for(5s) == std::future_status::ready);
  waiter.get();
  SIC_CHECK(budget.available() == before);
  bool refused = false;
  try {
    budget.acquire(4096, 0);
  } catch (const std::runtime_error&) {
    refused = true;
  }
  SIC_CHECK(refused);
  budget.unreserve(256);
  SIC_CHECK(budget.fixed() == 128);
  SIC_CHECK(!admission::fits(UINT64_MAX, 1, UINT64_MAX));
  SIC_CHECK(admission::fits(UINT64_MAX, 0, UINT64_MAX));
  SIC_CHECK(!admission::fits(UINT64_MAX, 0, 4096));
  bool overflow = false;
  try {
    (void)admission::add(UINT64_MAX, 1);
  } catch (const std::overflow_error&) {
    overflow = true;
  }
  SIC_CHECK(overflow);
  // The shared group must join owned workers when an owner scope unwinds.
  std::atomic<unsigned> completed{0};
  try {
    sic::support::ThreadGroup group;
    for (unsigned i = 0; i < 4; ++i) group.start([&] { ++completed; });
    throw std::runtime_error("owner unwind");
  } catch (const std::runtime_error&) {
  }
  SIC_CHECK(completed == 4);
  // Exercise the decoder's persistent workers, nested inline dispatch and
  // callback exception/reuse with live FLINT cache allocations under LSan.
  {
    sic::decode::Pool pool(4, sic::decode::Placement::detect(false));
    bool caught = false;
    try {
      pool.run(64, [](std::size_t i) {
        sic::support::flint::Arb value;
        arb_const_pi(value, 1024);
        if (i == 5) throw std::runtime_error("decoder job unwind");
      });
    } catch (const std::runtime_error&) {
      caught = true;
    }
    SIC_CHECK(caught);
    std::atomic<unsigned> nested{0};
    pool.run(16, [&](std::size_t) { pool.run(2, [&](std::size_t) { ++nested; }); });
    SIC_CHECK(nested == 32);
  }
  flint_cleanup_master();
  return sic_test::report();
}
