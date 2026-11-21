// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstring>

#include "assertions.hpp"
#include "construct/model/memory_plan.hpp"
#include "construct/runtime/resources/construction_model.hpp"
#include "construct/runtime/resources/direct_rows.hpp"
#if defined(__linux__)
#include <sys/mman.h>
#endif

int main() {
  using namespace sic::construct;
  using namespace sic::support::resources;
  using sic::support::resources::add;
  using sic::support::resources::Bytes;
  using sic::support::resources::mul;
  using R = Retention;
  using C = MemoryControl;
  // A prior cache grant is not a fresh admission: mandatory workspace can fit
  // after a limit change while refill/replacement of the window cannot. Test
  // both a pressure-cleared window and one with a charged, small resident row.
  for (bool populated : {false, true}) {
    Budget row_budget;
    const Bytes workspace = 4 * kMib, window = 64 * kMib;
    auto rows = std::make_unique<sic::exact::DirectEtaRows>(window);
    if (populated) {
      auto batch = rows->prepare("root", 8, 128, {{1, 0, 1, 8}});
      *batch.slots[0] = sic::support::ComplexBallVector(8);
      rows->settle();
      SIC_CHECK(rows->size() == 1);
    }
    row_budget.initial_limit = add(inspect().rss, workspace + window / 2);
    row_budget.require_additional("test mandatory direct workspace fits", workspace);
    sic::construct::resources::admit_direct_rows(rows, row_budget, workspace);
    SIC_CHECK(!rows);
  }
  SIC_CHECK(MemoryPlanner::choose(C::automatic, {100, true, true, 20}, 100, 10) == R::keep);
  SIC_CHECK(MemoryPlanner::choose(C::automatic, {100, true, true, 20}, 99, 10) == R::spill);
  SIC_CHECK(MemoryPlanner::choose(C::automatic, {100, true, true, 1}, 99, 10) == R::recompute);
  SIC_CHECK(MemoryPlanner::choose(C::spill, {100, true, true, 1}, 200, 10) == R::spill);
  SIC_CHECK(MemoryPlanner::choose(C::spill, {100, false, true, 1}, 200, 10) == R::recompute);
  SIC_CHECK(MemoryPlanner::choose(C::keep_all, {100, true, true, 1}, 0, 10) == R::keep);
  bool refused = false;
  try {
    (void)MemoryPlanner::choose(C::automatic, {100, false, false}, 99, 10);
  } catch (const AdmissionError&) {
    refused = true;
  }
  SIC_CHECK(refused);
  SIC_CHECK(MemoryPlanner::recompute_retry_seed(true, true, 2958, 144, 512));
  SIC_CHECK(!MemoryPlanner::recompute_retry_seed(false, true, 2958, 144, 512));
  SIC_CHECK(!MemoryPlanner::recompute_retry_seed(true, false, 2958, 144, 512));
  SIC_CHECK(!MemoryPlanner::recompute_retry_seed(true, true, 512, 144, 512));
  SIC_CHECK(!MemoryPlanner::recompute_retry_seed(true, true, 2958, 2958, 512));
  SIC_CHECK(!MemoryPlanner::recompute_retry_seed(true, true, 2958, 144, 2958));
  const auto psi = MemoryPlanner::parse_io_pressure(
      "some avg10=0.00 avg60=0.00 avg300=0.00 total=1000\n"
      "full avg10=0.00 avg60=0.00 avg300=0.00 total=500\n",
      "fixture");
  SIC_CHECK(psi && psi->some_us == 1000 && psi->full_us == 500);
  SIC_CHECK(!MemoryPlanner::parse_io_pressure("some total=1000\n", "fixture"));
  double io_cost = 500.0 * kMib;
  bool false_pressure = false;
  // Quiet alignment diagnostics preserve the rate threshold for healthy disk traffic.
  for (int i = 0; i < 100; ++i)
    false_pressure |= MemoryPlanner::io_feedback(psi, psi, 2, 99, io_cost).pressure;
  SIC_CHECK(!false_pressure && io_cost == 500.0 * kMib);
  const auto healthy30 = MemoryPlanner::io_feedback(psi, psi, 1, 30 * kMib, io_cost);
  const auto healthy40 = MemoryPlanner::io_feedback(psi, psi, 1, 40 * kMib, io_cost);
  SIC_CHECK(healthy30.known && !healthy30.pressure);
  SIC_CHECK(healthy40.known && !healthy40.pressure);
  auto blocked = *psi;
  blocked.some_us += 200000;
  blocked.full_us += 100000;
  const auto stalled = MemoryPlanner::io_feedback(psi, blocked, 1, 0, io_cost);
  SIC_CHECK(stalled.known && stalled.pressure);  // A blocked device may complete no bytes.
  blocked = *psi;
  blocked.some_us += 550000;
  blocked.full_us += 50000;
  SIC_CHECK(MemoryPlanner::io_feedback(psi, blocked, 1, 0, io_cost).pressure);
  blocked.full_us = psi->full_us;
  SIC_CHECK(!MemoryPlanner::io_feedback(psi, blocked, 1, 0, io_cost).pressure);
  SIC_CHECK(!MemoryPlanner::io_feedback({}, blocked, 1, 0, io_cost).known);
  SIC_CHECK(!MemoryPlanner::io_feedback(psi, {}, 1, 0, io_cost).known);
  blocked.scope = "different-cgroup";
  SIC_CHECK(!MemoryPlanner::io_feedback(psi, blocked, 1, 0, io_cost).known);
  blocked = *psi;
  --blocked.full_us;
  SIC_CHECK(!MemoryPlanner::io_feedback(psi, blocked, 1, 0, io_cost).known);
  SIC_CHECK(!MemoryPlanner::io_feedback(psi, psi, .5, kMib, io_cost).known);
  Budget io_budget;
  io_budget.initial_limit = add(mul(inspect().rss, 2), 256 * kMib);
  MemoryPlanner io_planner(io_budget, C::automatic);
  bool io_evicted = false;
  const int io_workers = io_planner.apply_runtime(
      "I/O stall recovery test", 8, kMib, [&] { io_evicted = true; }, stalled, 0);
  SIC_CHECK(!io_evicted);  // Eviction under I/O-only pressure would cause unnecessary rereads.
  SIC_CHECK(io_workers >= 1 && io_workers <= 4);
  const std::vector<std::filesystem::path> pressure_paths{"/fixture/leaf", "/fixture/system"};
  const std::string valid_pressure = "some total=1000\nfull total=500\n";
  const auto fallback_pressure = MemoryPlanner::load_io_pressure(
      pressure_paths, [&](const auto& path) -> std::optional<std::string> {
        return path == pressure_paths[0] ? "some total=bad\nfull total=500\n" : valid_pressure;
      });
  SIC_CHECK(fallback_pressure && fallback_pressure->scope == pressure_paths[1].string());
  const auto missing_leaf = MemoryPlanner::load_io_pressure(
      pressure_paths, [&](const auto& path) -> std::optional<std::string> {
        return path == pressure_paths[0] ? std::optional<std::string>{} : valid_pressure;
      });
  SIC_CHECK(missing_leaf && missing_leaf->scope == pressure_paths[1].string());
  int pressure_reads = 0;
  const auto leaf_pressure = MemoryPlanner::load_io_pressure(
      pressure_paths, [&](const auto&) -> std::optional<std::string> {
        ++pressure_reads;
        return valid_pressure;
      });
  SIC_CHECK(leaf_pressure && leaf_pressure->scope == pressure_paths[0].string() &&
            pressure_reads == 1);
  SIC_CHECK(!MemoryPlanner::load_io_pressure(
      pressure_paths, [](const auto&) -> std::optional<std::string> { return {}; }));
  using IoWindow = MemoryPlanner::IoWindow;
  const auto at = [](double seconds) {
    return IoWindow::Clock::time_point{} + std::chrono::duration_cast<IoWindow::Clock::duration>(
                                               std::chrono::duration<double>(seconds));
  };
  IoWindow window;
  window.reset(at(0), 0, psi);
  SIC_CHECK(!window.observe(at(.5), 15 * kMib, psi, io_cost).known);
  SIC_CHECK(window.observe(at(1), 30 * kMib, psi, io_cost).known);
  // A missing/scope/reset transition inside a short window must discard the old baseline.
  window.reset(at(0), 0, psi);
  SIC_CHECK(!window.observe(at(.25), 0, {}, io_cost).known);
  SIC_CHECK(!window.observe(at(.5), 0, psi, io_cost).known);
  SIC_CHECK(!window.observe(at(1.1), 0, psi, io_cost).known);
  SIC_CHECK(window.observe(at(1.5), 0, psi, io_cost).known);
  auto other_scope = *psi;
  other_scope.scope = "other";
  window.reset(at(0), 0, psi);
  window.observe(at(.25), 0, other_scope, io_cost);
  window.observe(at(.5), 0, psi, io_cost);
  SIC_CHECK(!window.observe(at(1.1), 0, psi, io_cost).known);
  auto rising = *psi;
  rising.some_us += 200;
  rising.full_us += 100;
  auto rolled = rising;
  --rolled.full_us;  // Still above the baseline, but below the last observed counter.
  window.reset(at(0), 0, psi);
  window.observe(at(.25), 0, rising, io_cost);
  SIC_CHECK(!window.observe(at(.5), 0, rolled, io_cost).known);
  SIC_CHECK(!window.observe(at(1.1), 0, rising, io_cost).known);
  SIC_CHECK(window.observe(at(1.5), 0, rising, io_cost).known);
  window.reset(at(0), 0, psi);
  window.observe(at(.25), 2 * kMib, psi, io_cost);
  window.observe(at(.5), kMib, psi, io_cost);  // A byte-counter rollback also resets cost sampling.
  SIC_CHECK(!window.observe(at(1.1), 2 * kMib, psi, io_cost).known);
  window.reset(at(0), 0, psi);
  window.observe(at(.5), 0, psi, io_cost);
  window.observe(at(.25), 0, psi, io_cost);  // Clock rollback invalidates the prior window.
  SIC_CHECK(!window.observe(at(1.1), 0, psi, io_cost).known);
  sic::construct::resources::Model model(128, 5, 1, 1, 8);
  model.step_degrees = {5, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
  model.coset_counts = {1};
  for (auto p : model.step_degrees) model.coset_counts.push_back(mul(model.coset_counts.back(), p));
  SIC_CHECK(model.coset_counts.back() == model.degree);
  model.smith_rank = 4;
  model.kernel_workers = 2;
  model.symbolic_top = true;
  const auto resident = model.final_buffers(1000, 512);
  model.phased_final = true;
  const auto bounded = model.final_buffers(1000, 512);
  SIC_CHECK(bounded < resident);
  SIC_CHECK(model.final_buffers(1100, 512) >= bounded);
  SIC_CHECK(model.final_buffers(1000, 1024) >= bounded);
  const auto whole = model.final_buffers(1000, 512);
  model.phased_representative_rows = model.degree / 8;
  SIC_CHECK(model.final_buffers(1000, 512) <= whole);
  const auto two_workers = model.native_workspace(1000, 512);
  model.kernel_workers = 4;
  SIC_CHECK(model.native_workspace(1000, 512) > two_workers);
  // Current RSS is small, but future phase scratch requires fewer workers.
  sic::construct::resources::Model future_model = model;
  auto future = [&](int workers) {
    future_model.kernel_workers = workers;
    return future_model.final_buffers(1000, 512);
  };
  Budget future_budget;
  future_budget.initial_limit = future(4) + (future(8) - future(4)) / 2;
  MemoryPlanner future_planner(future_budget, C::automatic);
  SIC_CHECK(inspect().rss < future_budget.initial_limit);
  const int phase_workers = future_planner.phase_workers(8, future);
  SIC_CHECK(phase_workers == 4);
  SIC_CHECK(future(phase_workers) <= future_budget.limit());
  // An inherited RSS floor cannot consume the allowance for not-yet-owned scratch.
  MemoryPlanner::PhaseReservation inherited{200 * kMib, 64 * kMib, 80 * kMib, 256 * kMib, 0};
  SIC_CHECK(inherited.future() == 136 * kMib);
  SIC_CHECK(inherited.unknown() == 192 * kMib);
  SIC_CHECK(inherited.total() ==
            392 * kMib);  // max(RSS, structural) would underreserve by 136 kMib.
  Budget inherited_budget;
  inherited_budget.initial_limit = 384 * kMib;
  MemoryPlanner inherited_planner(inherited_budget, C::automatic);
  refused = false;
  try {
    inherited_planner.reserve(inherited.total());
  } catch (const AdmissionError&) {
    refused = true;
  }
  SIC_CHECK(refused);
  const auto with_workers = [&](int workers) {
    auto phase = inherited;
    phase.structural = add(phase.present, mul(workers, 64 * kMib));
    return phase.total();
  };
  SIC_CHECK(inherited_planner.phase_workers(4, with_workers) == 2);
  Budget anchored_budget;
  anchored_budget.initial_limit = 512 * kMib;
  MemoryPlanner anchored(anchored_budget, C::automatic);
  anchored.reserve(inherited.total());
  SIC_CHECK(anchored.decide_reserved("future item one", {100 * kMib, true, false}) == R::keep);
  SIC_CHECK(anchored.reserved() == 492 * kMib);
  SIC_CHECK(anchored.decide_reserved("future item two", {100 * kMib, true, false}) == R::spill);
  // A new boundary includes existing commitments once, and active-phase decisions keep its anchor.
  anchored.reserve(400 * kMib);
  SIC_CHECK(anchored.reserved() == 400 * kMib);
  SIC_CHECK(anchored.decide_reserved("future item three", {100 * kMib, true, false}) == R::keep);
  SIC_CHECK(anchored.reserved() == 500 * kMib);
  anchored.reset();
  refused = false;
  try {
    (void)anchored.reserved();
  } catch (const std::logic_error&) {
    refused = true;
  }
  SIC_CHECK(refused);
  inherited.present = inherited.structural + 1;
  refused = false;
  try {
    (void)inherited.total();
  } catch (const std::logic_error&) {
    refused = true;
  }
  SIC_CHECK(refused);
  model.coset_counts.back()++;
  refused = false;
  try {
    (void)model.final_buffers(1000, 512);
  } catch (const std::runtime_error&) {
    refused = true;
  }
  SIC_CHECK(refused);
#if defined(__linux__)
  // A live optional cache exceeds the cap; worker admission succeeds only after eviction.
  Budget pressure_budget;
  pressure_budget.initial_limit = add(inspect().rss, 32 * kMib);
  const auto mapping_bytes = 64 * kMib;
  void* mapping =
      mmap(nullptr, mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  SIC_CHECK(mapping != MAP_FAILED);
  if (mapping != MAP_FAILED) {
    std::memset(mapping, 1, mapping_bytes);
    MemoryPlanner pressure_planner(pressure_budget, C::automatic);
    bool evicted = false;
    const int workers = pressure_planner.adjust("pressure recovery test", 8, 8 * kMib, [&] {
      SIC_CHECK(munmap(mapping, mapping_bytes) == 0);
      evicted = true;
    });
    SIC_CHECK(evicted);
    SIC_CHECK(workers >= 1 && workers < 8);
  }
#endif
  return sic_test::report();
}
