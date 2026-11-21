// SPDX-License-Identifier: AGPL-3.0-or-later
// representative-fast-v3: freed Arb limbs of a representative band must not
// stay counted as live RSS by the next band's admission.
//
// The d=2048 count208 run filled a band of 2^26 Arb
// balls (~2.2 KB of limbs each) from 128 worker threads, freed them leaf by
// leaf during recognition, and was then refused before band 1 because VmRSS
// had not fallen. This test reproduces the pattern at a moderate size: worker
// threads fill a vector of high-precision balls while keeping small long-lived
// allocations interleaved in their arenas; the main thread then clears every
// ball. It checks that malloc_trim(0) (sic::support::resources::release_free_heap) returns
// the freed pages, and that Budget::require_additional trims and re-measures
// before it refuses, while still refusing a forecast above the cap.
#include <flint/arb.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "support/resources.hpp"

namespace {

namespace resources = sic::support::resources;
using sic::support::resources::Bytes;

Bytes rss() { return sic::support::resources::inspect().rss; }

int failures = 0;
void check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) ++failures;
}

// Fill `count` balls at `bits` precision from `threads` workers. Each worker
// also keeps one small allocation per `stride` balls alive in its own arena.
void fill(arb_ptr values, long count, slong bits, int threads,
          std::vector<std::vector<void*>>& keep) {
  std::vector<std::thread> pool;
  keep.assign(threads, {});
  for (int w = 0; w < threads; ++w)
    pool.emplace_back([&, w] {
      const long begin = count * w / threads, end = count * (w + 1) / threads;
      for (long i = begin; i < end; ++i) {
        arb_set_si(values + i, 2 + i % 1009);
        arb_sqrt(values + i, values + i, bits);
        if ((i - begin) % 64 == 63) keep[w].push_back(std::malloc(48));
      }
      flint_cleanup();
    });
  for (auto& thread : pool) thread.join();
}

// Clear balls in label order from the main thread, as recognition does.
void clear_in_order(arb_ptr values, long count) {
  for (long i = 0; i < count; ++i) {
    arb_clear(values + i);
    arb_init(values + i);
  }
}

}  // namespace

int main() {
  const long count = 1L << 19;  // 524,288 balls
  const slong bits = 17300;     // the d=2048 working precision class
  const int threads = 8;
  const Bytes limbs =
      sic::support::resources::mul(count, sic::support::resources::real_bytes(bits));

  const Bytes base = rss();
  std::vector<std::vector<void*>> keep;
  arb_ptr values = _arb_vec_init(count);
  fill(values, count, bits, threads, keep);
  const Bytes full = rss();
  clear_in_order(values, count);
  _arb_vec_clear(values, count);
  const Bytes freed = rss();
  sic::support::resources::release_free_heap();
  const Bytes trimmed = rss();
  std::printf(
      "band heap: forecast_limbs=%llu base=%llu full=%llu freed_untrimmed=%llu trimmed=%llu\n",
      (unsigned long long)limbs, (unsigned long long)base, (unsigned long long)full,
      (unsigned long long)freed, (unsigned long long)trimmed);
  check(full > base + limbs / 2, "band allocation is resident");
  check(freed > base + limbs / 2,
        "freed band limbs stay resident without a trim (the count208 failure mode)");
  check(trimmed < base + limbs / 8, "malloc_trim returns at least 7/8 of the band");

  // Budget: same pattern, then admit an extra band-sized forecast under a cap
  // that only fits once the freed pages are returned.
  values = _arb_vec_init(count);
  for (auto& arena : keep)
    for (void* p : arena) std::free(p);
  fill(values, count, bits, threads, keep);
  clear_in_order(values, count);
  _arb_vec_clear(values, count);
  const Bytes before = rss();
  const Bytes cap = trimmed + limbs + limbs / 2;
  setenv("SIC_MEMORY_MB", std::to_string(cap / sic::support::resources::kMib).c_str(), 1);
  const sic::support::resources::Budget budget;
  check(before + limbs > (cap / sic::support::resources::kMib) * sic::support::resources::kMib,
        "untrimmed RSS plus the next band exceeds the cap");
  bool admitted = true;
  try {
    budget.require_additional("test next band", limbs);
  } catch (const sic::support::resources::AdmissionError&) {
    admitted = false;
  }
  check(admitted, "require_additional trims, re-measures and admits the next band");
  check(rss() < before - limbs / 2, "the admission trim lowered RSS");
  bool refused = false;
  try {
    budget.require_additional("test over-cap band", sic::support::resources::mul(4, limbs));
  } catch (const sic::support::resources::AdmissionError& error) {
    refused =
        std::string(error.what()).find("resource admission rejected before test over-cap band") ==
        0;
  }
  check(refused, "a forecast above the cap is still refused after the trim");
  bool refused_require = false;
  try {
    budget.require("test require", sic::support::resources::mul(4, limbs));
  } catch (const sic::support::resources::AdmissionError&) {
    refused_require = true;
  }
  check(refused_require, "require() still refuses above the cap");

  for (auto& arena : keep)
    for (void* p : arena) std::free(p);
  flint_cleanup_master();
  if (failures) std::printf("%d failure(s)\n", failures);
  return failures ? 1 : 0;
}
