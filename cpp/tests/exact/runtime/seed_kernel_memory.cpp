#include <barrier>
#include <chrono>
#include <thread>

#include "assertions.hpp"
#include "construct/runtime/resources/table_memory.hpp"
#include "exact/runtime/newton_class_memory.hpp"

namespace phase_memory = sic::exact::phase_memory;

namespace {
void refusal_cleanup(bool streamed, unsigned refusal) {
  using namespace sic::exact;
  Arithmetic arithmetic(3);
  sic::support::Reservations pool(256 * sic::support::resources::kMib);
  phase_memory::PreparedSeed<Table> seed;
  auto source = pool.try_acquire(64 * sic::support::resources::kMib);
  auto scratch = pool.try_acquire(64 * sic::support::resources::kMib);
  SIC_CHECK(source && scratch);
  auto table = std::make_unique<Table>(arithmetic, 1024, 1);
  table->stream_states = streamed;
  unsigned calls = 0;
  table->admit_workspace = [&](sic::support::resources::Bytes bytes) {
    SIC_CHECK(bytes > 0);
    if (++calls == refusal)
      throw sic::support::resources::AdmissionError("seed kernel refusal fixture");
  };
  seed.assign(std::move(table), std::move(source));
  bool caught = false;
  try {
    seed->build_class(0);
  } catch (const sic::support::resources::AdmissionError& error) {
    caught = std::string(error.what()) == "seed kernel refusal fixture";
    seed.reset();  // all Table and stack-local Arb storage must have died
  }
  SIC_CHECK(caught && calls == refusal);
  SIC_CHECK(pool.used() == 64 * sic::support::resources::kMib);
  scratch.reset();
  SIC_CHECK(pool.used() == 0);
}

void owned_refusal(bool streamed, unsigned refusal, bool prior_refuses) {
  using namespace sic::exact;
  Arithmetic arithmetic(3);
  constexpr sic::support::resources::Bytes kBase = 8 * sic::support::resources::kMib,
                                           source_bytes = 64 * sic::support::resources::kMib;
  unsigned growths = 0, prior_calls = 0;
  bool armed = false;
  sic::support::Reservations pool(256 * sic::support::resources::kMib,
                                  [&](sic::support::resources::Bytes) {
                                    if (!armed || prior_refuses) return true;
                                    return ++growths < refusal;
                                  });
  auto source = pool.try_acquire(source_bytes);
  auto scratch = pool.try_acquire(kBase);
  SIC_CHECK(source && scratch);
  phase_memory::PreparedSeed<Table> seed;
  auto table = std::make_unique<Table>(arithmetic, 1024, 1);
  table->stream_states = streamed;
  table->admit_workspace = [&](sic::support::resources::Bytes bytes) {
    SIC_CHECK(scratch->bytes() >= sic::support::resources::add(kBase, bytes));
    if (++prior_calls == refusal && prior_refuses)
      throw sic::support::resources::AdmissionError("original fresh gate refused");
  };
  seed.assign(std::move(table), std::move(source));
  armed = true;
  bool caught = false;
  try {
    sic::construct::resources::SeedKernelMemory kernels(*seed, *scratch, kBase, 1);
    seed->build_class(0);
  } catch (const sic::support::resources::AdmissionError& error) {
    caught =
        std::string(error.what()) == (prior_refuses ? "original fresh gate refused"
                                                    : "owned prepared-seed kernel growth refused");
  }
  SIC_CHECK(caught);
  SIC_CHECK(prior_refuses ? prior_calls == refusal : growths == refusal);
  if (!prior_refuses) SIC_CHECK(prior_calls + 1 == refusal);
  SIC_CHECK(pool.used() == source_bytes + scratch->bytes());
  seed.reset();  // binding has restored the original callback before destruction
  SIC_CHECK(pool.used() == scratch->bytes());
  scratch.reset();
  SIC_CHECK(pool.used() == 0);
}

void accepted(bool streamed, long slab) {
  using namespace sic::exact;
  Arithmetic arithmetic(3);
  constexpr sic::support::resources::Bytes kBase = 8 * sic::support::resources::kMib;
  sic::support::Reservations pool(256 * sic::support::resources::kMib);
  auto scratch = pool.try_acquire(kBase);
  SIC_CHECK(scratch);
  Table reference(arithmetic, 256, 2), owned(arithmetic, 256, 2);
  for (auto* table : {&reference, &owned}) {
    table->stream_states = streamed;
    table->node_slab = slab;
  }
  reference.build_class(0);
  unsigned calls = 0;
  const int active = streamed ? 1 : std::min<int>(2, arithmetic.cls[0].digit.size());
  owned.admit_workspace = [&](sic::support::resources::Bytes bytes) {
    SIC_CHECK(scratch->bytes() >=
              sic::support::resources::add(kBase, sic::support::resources::mul(bytes, active)));
    ++calls;
  };
  {
    sic::construct::resources::SeedKernelMemory kernels(owned, *scratch, kBase, active);
    owned.build_class(0);
    SIC_CHECK(kernels.kernel_peak() > 0);
    SIC_CHECK(kernels.required() == scratch->bytes());
    SIC_CHECK(calls > 3);
  }
  const unsigned built_calls = calls;
  owned.admit_workspace(1);
  SIC_CHECK(calls == built_calls + 1);  // original callback was restored
  SIC_CHECK(reference.n_nodes_total == owned.n_nodes_total);
  SIC_CHECK(reference.val[0].size() == owned.val[0].size());
  for (size_t q = 0; q < reference.val[0].size(); ++q)
    SIC_CHECK(acb_equal(&reference.val[0][q], &owned.val[0][q]));
  SIC_CHECK(pool.used() == scratch->bytes());
  // Scratch must outlive the Tables. This test retains it until function exit.
}

void overflow_and_trusted_exception() {
  using namespace sic::exact;
  Arithmetic arithmetic(3);
  constexpr sic::support::resources::Bytes kBase = 8 * sic::support::resources::kMib;
  sic::support::Reservations pool(256 * sic::support::resources::kMib);
  auto scratch = pool.try_acquire(kBase);
  Table table(arithmetic, 256, 1);
  unsigned prior_calls = 0;
  table.admit_workspace = [&](sic::support::resources::Bytes) {
    ++prior_calls;
    throw std::overflow_error("trusted observer overflow");
  };
  {
    sic::construct::resources::SeedKernelMemory kernels(table, *scratch, kBase, 2);
    bool refused = false;
    try {
      table.admit_workspace(UINT64_MAX);
    } catch (const sic::support::resources::AdmissionError& error) {
      refused = std::string(error.what()) == "owned prepared-seed kernel forecast unrepresentable";
    }
    SIC_CHECK(refused && prior_calls == 0 && scratch->bytes() == kBase);
    bool trusted = false;
    try {
      table.build_class(0);
    } catch (const std::overflow_error& error) {
      trusted = std::string(error.what()) == "trusted observer overflow";
    }
    SIC_CHECK(trusted && prior_calls == 1);  // do not convert the prior callback's exception
  }
}

void concurrent_refusal() {
  using namespace sic::exact;
  Arithmetic arithmetic(6);
  size_t cls = 0;
  while (cls < arithmetic.cls.size() && arithmetic.cls[cls].digit.size() < 2) ++cls;
  SIC_CHECK(cls < arithmetic.cls.size());
  if (cls == arithmetic.cls.size()) return;
  constexpr sic::support::resources::Bytes kBase = 16 * sic::support::resources::kMib,
                                           source_bytes = 64 * sic::support::resources::kMib;
  std::mutex state_mutex;
  std::map<std::thread::id, unsigned> calls;
  std::thread::id victim;
  std::atomic<bool> armed{false}, released{false}, peer_waiting{false}, refused{false};
  sic::support::Reservations pool(
      512 * sic::support::resources::kMib, [&](sic::support::resources::Bytes) {
        if (!armed) return true;
        std::lock_guard lock(state_mutex);
        return std::this_thread::get_id() != victim || calls[victim] < 3;
      });
  auto source = pool.try_acquire(source_bytes), scratch = pool.try_acquire(kBase);
  SIC_CHECK(source && scratch);
  phase_memory::PreparedSeed<Table> seed;
  auto table = std::make_unique<Table>(arithmetic, 512, 2);
  table->stream_states = false;
  table->node_slab = 1;
  seed.assign(std::move(table), std::move(source));
  std::barrier start(2);
  bool caught = false;
  {
    sic::construct::resources::SeedKernelMemory kernels(*seed, *scratch, kBase, 2);
    auto bound = seed->admit_workspace;
    seed->admit_workspace = [&](sic::support::resources::Bytes bytes) {
      const auto current = std::this_thread::get_id();
      unsigned count;
      bool chosen;
      {
        std::lock_guard lock(state_mutex);
        if (victim == std::thread::id()) victim = current;
        chosen = victim == current;
        count = ++calls[current];
      }
      if (count == 1) {
        if (!chosen) peer_waiting = true;
        start.arrive_and_wait();  // outside the binding and reservation mutexes
      }
      if (!chosen) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!released && std::chrono::steady_clock::now() < deadline)
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!released) throw std::runtime_error("concurrent refusal fixture timed out");
        throw sic::support::resources::AdmissionError("peer cancelled after owned refusal");
      }
      try {
        bound(bytes);
      } catch (const sic::support::resources::AdmissionError& error) {
        refused = count >= 3 && peer_waiting &&
                  std::string(error.what()) == "owned prepared-seed kernel growth refused";
        released = true;
        throw;
      }
    };
    armed = true;
    try {
      seed->build_class(cls);
    } catch (const sic::support::resources::AdmissionError&) {
      caught = true;
    }
  }  // Table's workers have joined and its original callback is restored
  SIC_CHECK(caught && refused && peer_waiting && calls.size() == 2);
  SIC_CHECK(calls[victim] == 3);
  SIC_CHECK(pool.used() == source_bytes + scratch->bytes());
  seed.reset();
  SIC_CHECK(pool.used() == scratch->bytes());
  scratch.reset();
  SIC_CHECK(pool.used() == 0);
}
}  // namespace

int main() {
  flint_set_num_threads(2);
  for (bool streamed : {false, true})
    for (unsigned panel : {1U, 3U}) refusal_cleanup(streamed, panel);
  for (bool streamed : {false, true}) {
    for (unsigned panel : {1U, 3U})
      for (bool prior : {false, true}) owned_refusal(streamed, panel, prior);
    for (long slab : {0L, 1L}) accepted(streamed, slab);
  }
  overflow_and_trusted_exception();
  concurrent_refusal();
  flint_cleanup_master();
  return sic_test::report();
}
