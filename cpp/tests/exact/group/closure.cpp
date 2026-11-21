// SPDX-License-Identifier: AGPL-3.0-or-later
#include <atomic>
#include <fstream>

#include "assertions.hpp"
#include "exact/group/group_checkpoint.hpp"
#include "exact/group/maximal_order_orbits.hpp"
#include "sic/support/kernel_settings.hpp"
#include "sic/support/stop.hpp"
#include "support/resources.hpp"
#include "temporary_directory.hpp"

namespace {
// A commuting binary action with wide BFS levels and many fixed base points.
// At d64 its ten generators force multiple 4 MiB tiles within a level.
void wide_generators(sic::exact::Group& group) {
  std::iota(group.orbit.begin(), group.orbit.end(), sic::support::Index{0});
  for (int bit = 0; bit < 10; ++bit) {
    sic::exact::GElem generator;
    generator.n = generator.chi = 1;
    generator.perm.resize(group.npts);
    for (long x = 0; x < group.npts; ++x)
      generator.perm[x] = sic::support::checked_index(x < 1024 ? x ^ (1 << bit) : x);
    group.gens.push_back(std::move(generator));
  }
}

void compare(const sic::exact::Group& a, const sic::exact::Group& b) {
  SIC_CHECK(a.N == b.N);
  SIC_CHECK(a.base == b.base);
  SIC_CHECK(a.invf == b.invf);
  SIC_CHECK(a.stride == b.stride);
  SIC_CHECK(a.nn == b.nn);
  SIC_CHECK(a.chi == b.chi);
  SIC_CHECK(a.img == b.img);
  SIC_CHECK(a.rep_elem == b.rep_elem);
  SIC_CHECK(a.rep_base == b.rep_base);
  for (size_t gi = 0; gi < a.gens.size(); ++gi)
    SIC_CHECK(a.element_of(a.gens[gi]) == b.element_of(b.gens[gi]));
}

#ifdef SIC_ENABLE_TEST_HOOKS
void compare(const sic::exact::GroupClosureTrace& a, const sic::exact::GroupClosureTrace& b) {
  SIC_CHECK(a.images == b.images);
  SIC_CHECK(a.norms == b.norms);
  SIC_CHECK(a.signs == b.signs);
  SIC_CHECK(a.exponents == b.exponents);
  SIC_CHECK(a.neighbours == b.neighbours);
  SIC_CHECK(a.relations == b.relations);
  SIC_CHECK(a.smith == b.smith);
  SIC_CHECK(a.transform == b.transform);
  SIC_CHECK(a.destination == b.destination);
}
#endif
}  // namespace

int main() {
  sic_test::TemporaryDirectory scratch("sic-group-closure");
  const auto directory = scratch.path;
  // Exercise actual multi-tile levels below d128, including ordinary public
  // outputs and progress in builds without snapshots.
  {
    sic::exact::Arithmetic ar(6);
    sic::exact::Group serial(ar), parallel(ar);
    wide_generators(serial);
    wide_generators(parallel);
    sic::support::KernelSettings settings{{"SIC_THREADS", "1"}};
    sic::support::KernelSettingsScope scope(&settings);
    std::vector<size_t> expected_progress, actual_progress;
#ifdef SIC_ENABLE_TEST_HOOKS
    sic::exact::GroupClosureTrace serial_trace;
    serial.closure_trace = &serial_trace;
#endif
    serial.close(
        sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")),
        1024, {}, [&](size_t q) { expected_progress.push_back(q); });
    settings["SIC_THREADS"] = "8";
#ifdef SIC_ENABLE_TEST_HOOKS
    sic::exact::GroupClosureTrace trace;
    parallel.closure_trace = &trace;
#endif
    parallel.close(
        sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")),
        1024, {}, [&](size_t q) { actual_progress.push_back(q); });
    compare(serial, parallel);
    SIC_CHECK(expected_progress == actual_progress);
#ifdef SIC_ENABLE_TEST_HOOKS
    compare(serial_trace, trace);
    if (sic::support::resources::inspect().cpus > 1) SIC_CHECK(trace.max_level_tiles > 1);
    // Cancellation is requested immediately before a non-owner stride check
    // inside expansion. That exact check must throw, never return past polling.
    sic::exact::Group cancelled_group(ar);
    wide_generators(cancelled_group);
    sic::support::StopToken token(directory / "worker.stop");
    sic::support::StopScope stop_scope(&token);
    sic::exact::GroupClosureTrace stopped;
    std::atomic<bool> requested{false}, returned_after_stop{false};
    stopped.worker_stop_check = [&](size_t q, int worker, bool after) {
      thread_local bool requested_here = false;
      if (q && worker > 0) {
        if (after) {
          if (requested_here) returned_after_stop = true;
        } else {
          requested_here = !requested.exchange(true);
          if (requested_here) std::ofstream(directory / "worker.stop") << "stop\n";
        }
      }
    };
    cancelled_group.closure_trace = &stopped;
    bool cancelled = false;
    try {
      cancelled_group.close(
          sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")),
          1024);
    } catch (const sic::Error& error) {
      cancelled = error.kind() == sic::ErrorKind::cancelled;
    }
    if (sic::support::resources::inspect().cpus > 1) {
      SIC_CHECK(requested && cancelled && !returned_after_stop);
      SIC_CHECK(cancelled_group.N == 0);
    }
#endif
  }
  for (int s = 2; s <= 7; ++s) {
    for (bool maximal : {false, true}) {
      sic::exact::Arithmetic ar(s);
      sic::exact::reduced_order::CycleUnit unit;
      if (maximal) unit = sic::exact::reduced_order::configure_arith(ar);
      const long cap = ar.h() * ar.d * ar.d / (2 * std::max(1, unit.k));
      sic::support::KernelSettings settings{{"SIC_THREADS", "1"}};
      sic::support::KernelSettingsScope scope(&settings);
      sic::exact::Group serial(ar);
      if (unit.k > 1) sic::exact::reduced_order::configure_group_orbits(serial, unit);
      serial.generate_standard();
#ifdef SIC_ENABLE_TEST_HOOKS
      sic::exact::GroupClosureTrace expected;
      serial.closure_trace = &expected;
#endif
      std::vector<size_t> serial_progress;
      serial.close(
          sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")),
          cap, {}, [&](size_t q) { serial_progress.push_back(q); });
      const auto reference = directory / "serial.bin";
      sic::exact::GroupCheckpoint::save(serial, reference);
      const auto digest = sic::exact::checkpoint_digest(reference);
      for (int workers : {1, 2, 3, 8, 16}) {
        settings["SIC_THREADS"] = std::to_string(workers);
        sic::exact::Group parallel(ar);
        if (unit.k > 1) sic::exact::reduced_order::configure_group_orbits(parallel, unit);
        parallel.generate_standard();
#ifdef SIC_ENABLE_TEST_HOOKS
        sic::exact::GroupClosureTrace actual;
        parallel.closure_trace = &actual;
#endif
        std::vector<size_t> progress;
        parallel.close(
            sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")),
            cap, {}, [&](size_t q) { progress.push_back(q); });
        SIC_CHECK(progress == serial_progress);
        compare(serial, parallel);
#ifdef SIC_ENABLE_TEST_HOOKS
        compare(expected, actual);
#endif
        const auto checkpoint = directory / "parallel.bin";
        sic::exact::GroupCheckpoint::save(parallel, checkpoint);
        SIC_CHECK(sic::exact::checkpoint_digest(checkpoint) == digest);
        std::filesystem::remove(checkpoint);
        std::filesystem::remove(checkpoint.string() + ".sha256");
      }
      std::filesystem::remove(reference);
      std::filesystem::remove(reference.string() + ".sha256");
    }
  }
  // Request cancellation only after some rows have actually been discovered.
  // The callback runs on the owner; the next tile/merge observes the sticky token.
  for (int workers : {1, 3, 16}) {
    sic::exact::Arithmetic ar(6);
    sic::exact::Group group(ar);
    group.generate_standard();
    sic::support::StopToken token;
    sic::support::StopScope stop_scope(&token);
    sic::support::KernelSettings settings{{"SIC_THREADS", std::to_string(workers)}};
    sic::support::KernelSettingsScope scope(&settings);
    bool cancelled = false, started = false;
    try {
      group.close(
          sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")),
          ar.h() * ar.d * ar.d / 2, {}, [&](size_t q) {
            if (q) {
              started = true;
              token.request_stop();
            }
          });
    } catch (const sic::Error& error) {
      cancelled = error.kind() == sic::ErrorKind::cancelled;
    }
    SIC_CHECK(started && cancelled);
    SIC_CHECK(group.N == 0);
    // A failed closure is never a checkpointable completed group.
    bool refused = false;
    try {
      sic::exact::GroupCheckpoint::save(group, directory / "cancelled.bin");
    } catch (const std::logic_error&) {
      refused = true;
    }
    SIC_CHECK(refused);
  }
  // Admission fails before allocating the candidate tile or creating workers.
  {
    sic::exact::Arithmetic ar(5);
    sic::exact::Group group(ar);
    group.generate_standard();
    sic::support::KernelSettings settings{{"SIC_THREADS", "3"}, {"SIC_MEMORY_MB", "1"}};
    sic::support::KernelSettingsScope scope(&settings);
    bool refused = false;
    try {
      group.close(
          sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")),
          ar.h() * ar.d * ar.d / 2);
    } catch (const sic::support::resources::AdmissionError&) {
      refused = true;
    }
    if (sic::support::resources::inspect().cpus > 1) {
      SIC_CHECK(refused);
      SIC_CHECK(group.N == 0);
    } else {
      SIC_CHECK(!refused);
      SIC_CHECK(group.N == ar.h() * ar.d * ar.d / 2);
    }
  }
  return sic_test::report();
}
