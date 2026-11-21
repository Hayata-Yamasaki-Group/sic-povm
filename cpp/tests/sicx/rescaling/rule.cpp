// SPDX-License-Identifier: AGPL-3.0-or-later
#include <iostream>
#include <stdexcept>

#include "sic/sicx/io.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/codec/rescaling_rule.hpp"
#include "sicx/container/rescaling_input.hpp"
#include "support/admitted_worker_team.hpp"

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void compare(const sic::sicx::RescalingCollector& input, std::span<const uint64_t> ceilings = {}) {
#ifdef SIC_ENABLE_TEST_HOOKS
  uint64_t largest = 0;
  input.valuations([&](int64_t, uint64_t, std::span<const uint32_t> values) {
    largest = std::max<uint64_t>(largest, values.size());
  });
#endif
  auto serial = input;
  const auto expected = serial.compute(1, ceilings);
  for (const unsigned threads : {1u, 2u, 12u}) {
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
      auto copy = input;
#ifdef SIC_ENABLE_TEST_HOOKS
      const auto capacity = sic::support::local_worker_team().size();
      sic::sicx::rescaling_thread_starts = 0;
#endif
      const auto actual = copy.compute(threads, ceilings);
      require(actual.shifts == expected.shifts && actual.gain == expected.gain,
              "worker count changed rescaling output");
      require(sic::support::dump_json(sic::sicx::rescaling_json(actual)) ==
                  sic::support::dump_json(sic::sicx::rescaling_json(expected)),
              "worker count changed rescaling provenance");
      unsigned visits = 0;
      copy.valuations([&](int64_t, uint64_t, std::span<const uint32_t>) { ++visits; });
      require(visits == 0, "compute did not consume valuation tables");
#ifdef SIC_ENABLE_TEST_HOOKS
      const auto workers = std::min<uint64_t>(
          threads, std::max<uint64_t>(1, largest / sic::sicx::rescaling_parallel_grain));
      require(sic::sicx::rescaling_thread_starts == (workers > capacity ? workers - capacity : 0),
              "thread starts do not respect the per-worker grain");
#endif
    }
  }
}

void fixture(const char* path) {
  const sic::sicx::Reader reader(path);
  const auto& meta = reader.meta();
  sic::sicx::RescalingCollector collector(meta);
  for (size_t i = 0; i < meta.elements.size(); ++i) {
    // Restore nonzero shifts: finalized fixtures alone exercise identity only.
    collector.add(sic::sicx::element_id_of(meta, meta.elements[i]),
                  sic::sicx::unrescaled_source(reader, i));
  }
  compare(collector);
  auto copy = collector;
  require(copy.compute(1).shifts == meta.rescaling_shifts, "fixture rescaling changed");
  const std::vector<uint64_t> ceilings(meta.degrees.size(), 1);
  compare(collector, ceilings);
#ifdef SIC_ENABLE_TEST_HOOKS
  // Exercise actual d16-d64 tables through the crew as well as the default
  // grain, even when all their tables fit on the calling thread.
  sic::sicx::rescaling_parallel_grain = 1;
  compare(collector);
  compare(collector, ceilings);
  sic::sicx::rescaling_parallel_grain = sic::sicx::kRescalingParallelGrain;
#endif
}

void boundary(uint64_t size) {
  // A synthetic single-level tower allows exact sizes adjacent to the grain.
  // Positive slack exercises both scan and update for several tables.
  sic::sicx::Meta meta;
  meta.degrees = {static_cast<unsigned>(size), 2};
  meta.width = {1, size, 2 * size};
  sic::sicx::RescalingCollector collector(meta);
  sic::sicx::FmpzPair pair;
  fmpz_one(pair.x);
  fmpz_mul_2exp(pair.x, pair.x, 4 * size);
  auto leaves = [&](const sic::sicx::LeafVisitor& visit) {
    visit(0, pair.x, pair.y);
    visit(size - 1, pair.x, pair.y);
  };
  collector.add({"wsq", 1}, leaves);
  collector.add({"actodd", 1, -1, 0}, leaves);
  collector.add({"fcoef", 1, -1, 1}, leaves);
  compare(collector);
  const std::vector<uint64_t> ceilings{1, 2};
  compare(collector, ceilings);
}

void growth() {
  // The smaller table dispatches before the larger one. Later scans/updates
  // reuse the grown crew with fewer active workers, including a serial table.
  const auto size = 2 * sic::sicx::kRescalingParallelGrain;
  sic::sicx::Meta meta;
  meta.degrees = {static_cast<unsigned>(size), 2, 2};
  meta.width = {1, size, 2 * size, 4 * size};
  sic::sicx::RescalingCollector collector(meta);
  sic::sicx::FmpzPair pair;
  fmpz_set_ui(pair.x, 1u << 16);
  auto leaves = [&](const sic::sicx::LeafVisitor& visit) { visit(0, pair.x, pair.y); };
  collector.add({"wsq", 0}, leaves);
  collector.add({"wsq", 1}, leaves);
  collector.add({"wsq", 2}, leaves);
  compare(collector);
  const std::vector<uint64_t> ceilings{1, 2, 3};
  compare(collector, ceilings);
}
}  // namespace

int main(int argc, char** argv) {
  require(argc == 4, "expected d16, d32 and d64 fixture paths");
  flint_set_num_threads(1);
  for (int i = 1; i < argc; ++i) fixture(argv[i]);
  for (const uint64_t workers : {1, 2, 4, 12})
    for (const int delta : {-1, 0, 1})
      boundary(workers * sic::sicx::kRescalingParallelGrain + delta);
  growth();
  std::cout << "rescaling worker identity, provenance, grain and bounded starts: PASS\n";
}
