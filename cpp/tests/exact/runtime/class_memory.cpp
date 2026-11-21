#include <array>
#include <atomic>
#include <barrier>

#include "assertions.hpp"
#include "exact/runtime/newton_class_memory.hpp"

namespace phase_memory = sic::exact::phase_memory;
using sic::support::resources::Bytes;

namespace {
struct Seed {
  sic::support::Reservations& pool;
  Bytes grant;
  int& destructions;
  bool& invalid;
  std::array<unsigned char, 16> data{};
  Seed(sic::support::Reservations& owner, Bytes bytes, int& count, bool& failed)
      : pool(owner), grant(bytes), destructions(count), invalid(failed) {}
  ~Seed() {
    if (pool.used() < grant) invalid = true;
    ++destructions;
  }
};

void prepared_lifetime() {
  sic::support::Reservations pool(200);
  int destroyed = 0;
  bool invalid = false;
  auto make = [&](Bytes bytes) {
    phase_memory::PreparedSeed<Seed> result;
    auto grant = pool.try_acquire(bytes);
    auto value = std::make_unique<Seed>(pool, bytes, destroyed, invalid);
    result.assign(std::move(value), std::move(grant));
    return result;
  };
  auto first = make(30), second = make(50);
  SIC_CHECK(pool.used() == 80);
  first = std::move(second);
  SIC_CHECK(destroyed == 1 && pool.used() == 50);
  SIC_CHECK(!second && first.granted() == 50);
  first = std::move(first);
  SIC_CHECK(pool.used() == 50 && destroyed == 1);
  first.reset();
  SIC_CHECK(!invalid && destroyed == 2 && pool.used() == 0);
}

void batch_and_drain() {
  sic::support::Reservations pool(180);
  int destroyed = 0;
  bool invalid = false;
  std::array<phase_memory::PreparedSeed<Seed>, 2> seeds;
  std::array<const Seed*, 2> original{};
  for (size_t c = 0; c < seeds.size(); ++c) {
    auto grant = pool.try_acquire(20);
    auto value = std::make_unique<Seed>(pool, 20, destroyed, invalid);
    value->data.fill(0x6a);
    seeds[c].assign(std::move(value), std::move(grant));
    original[c] = &*seeds[c];
  }
  const std::array<long, 2> classes{0, 1};
  std::array<phase_memory::ClassOutcome, 2> outcomes{};
  std::array<std::vector<unsigned char>, 2> outputs;
  std::array<int, 2> calls{}, published{};
  auto callback = [&](long c, std::optional<sic::support::Reservations::Lease> grant) {
    if (!grant || &*seeds[c] != original[c] || seeds[c]->data[0] != 0x6a)
      throw std::runtime_error("seed identity/storage lost");
    ++calls[c];
    phase_memory::CallbackRollback rollback([&] { std::vector<unsigned char>().swap(outputs[c]); });
    outputs[c].assign(40, 0x11);
    // Expected deferral after allocation. The rejected work is destroyed,
    // but this same prepared seed survives until the drained serial attempt.
    if (c == 0 && calls[c] == 1) {
      outcomes[c] = phase_memory::ClassOutcome::resource_deferred;
      return;
    }
    ++published[c];
    outcomes[c] = phase_memory::ClassOutcome::complete;
  };
  phase_memory::dispatch_callbacks(
      classes, 2, &pool, [](long) { return Bytes(100); },
      [&](long c, Bytes) { outcomes[c] = phase_memory::ClassOutcome::resource_deferred; },
      callback);
  SIC_CHECK(pool.used() == 40);
  SIC_CHECK(outcomes[0] == phase_memory::ClassOutcome::resource_deferred);
  SIC_CHECK(outcomes[1] == phase_memory::ClassOutcome::resource_deferred);
  phase_memory::drain_callbacks(
      classes, [&](long c) { return outcomes[c]; },
      [&](long c) {
        const std::array<long, 1> singleton{c};
        phase_memory::dispatch_callbacks(
            singleton, 1, &pool, [](long) { return Bytes(110); },
            [](long, Bytes) { throw std::runtime_error("serial grant unexpectedly refused"); },
            callback);
      });
  SIC_CHECK(calls[0] == 2 && calls[1] == 1);
  SIC_CHECK(published[0] == 1 && published[1] == 1);
  SIC_CHECK(outcomes[0] == phase_memory::ClassOutcome::complete &&
            outcomes[1] == phase_memory::ClassOutcome::complete);
  SIC_CHECK(outputs[0].capacity() == 0 && outputs[1].capacity() == 0);
  for (auto& seed : seeds) seed.reset();
  SIC_CHECK(pool.used() == 0 && destroyed == 2 && !invalid);
  int retries = 0;
  phase_memory::drain_callbacks(
      classes, [](long) { return phase_memory::ClassOutcome::resource_deferred; },
      [&](long) { ++retries; });
  SIC_CHECK(retries == 2);  // no implicit loop when retry remains deferred
}

void exception_teardown() {
  sic::support::Reservations pool(200);
  const std::array<long, 3> classes{0, 1, 2};
  std::barrier rendezvous(2);
  std::atomic<bool> invalid{false};
  std::array<std::vector<unsigned char>, 3> outputs;
  bool threw = false;
  try {
    phase_memory::dispatch_callbacks(
        classes, 2, &pool, [](long) { return Bytes(40); },
        [](long, Bytes) { throw std::runtime_error("unexpected admission refusal"); },
        [&](long c, std::optional<sic::support::Reservations::Lease> grant) {
          phase_memory::CallbackRollback rollback([&] {
            if (!grant || pool.used() < grant->bytes()) invalid = true;
            std::vector<unsigned char>().swap(outputs[c]);
          });
          outputs[c].assign(40, 0x33);
          if (c < 2) rendezvous.arrive_and_wait();
          if (c == 0) throw std::runtime_error("injected callback stop");
        });
  } catch (const std::runtime_error&) {
    threw = true;
  }
  SIC_CHECK(threw && !invalid);
  SIC_CHECK(pool.used() == 0);
  for (const auto& output : outputs) SIC_CHECK(output.capacity() == 0);
  SIC_CHECK(phase_memory::callback_workspace(64, 800000000, 1000000, 4, false) >= 801000000);
  SIC_CHECK(phase_memory::callback_workspace(8, 600000000, 1000000, 4, true) > 601000000);
}
}  // namespace

int main() {
  setenv("SIC_PARALLEL_GRAIN", "1", 1);
  prepared_lifetime();
  batch_and_drain();
  exception_teardown();
  flint_cleanup_master();
  return sic_test::report();
}
