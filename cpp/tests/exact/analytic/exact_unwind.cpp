// SPDX-License-Identifier: AGPL-3.0-or-later
#include <unistd.h>

#include <atomic>
#include <fstream>

#include "assertions.hpp"
#include "construct/stages/dimension_two.hpp"
#include "exact/analytic/table.hpp"
#include "exact/analytic/table/faddeev.hpp"
#include "exact/certification/reconstruction/ghost.hpp"
#include "temporary_directory.hpp"

namespace allocation_fault {
inline thread_local size_t fail_size = 0, skip = 0, recorded = 0;
inline thread_local bool record = false, fired = false;
}  // namespace allocation_fault
void* operator new(std::size_t bytes) {
  using namespace allocation_fault;
  if (record) recorded = bytes;
  if (fail_size && bytes == fail_size && skip-- == 0) {
    fail_size = 0;
    fired = true;
    throw std::bad_alloc();
  }
  if (void* memory = std::malloc(bytes ? bytes : 1)) return memory;
  throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void* memory) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
  try {
    return ::operator new(bytes);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void* operator new[](std::size_t bytes, const std::nothrow_t& tag) noexcept {
  return ::operator new(bytes, tag);
}
[[gnu::noinline]] void operator delete(void* memory, const std::nothrow_t&) noexcept {
  std::free(memory);
}
[[gnu::noinline]] void operator delete[](void* memory) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete[](void* memory, const std::nothrow_t&) noexcept {
  std::free(memory);
}

namespace {
template <class F>
bool throws(F&& fn, const char* message) {
  try {
    fn();
  } catch (const std::exception& error) {
    return std::string(error.what()).find(message) != std::string::npos;
  }
  return false;
}
void faddeev_unwind() {
  sic::support::flint::Arb tau, z;
  sic::support::flint::Acb out;
  arb_set_ui(tau, 2);
  arb_sqrt(tau, tau, 512);
  arb_set_ui(z, 100);
  SIC_CHECK(throws([&] { sic::exact::faddeev_strip(out, z, tau, 512); }, "outside the strip"));
  arb_set_si(tau, -1);
  arb_zero(z);
  SIC_CHECK(throws([&] { sic::exact::faddeev(out, z, tau, 512); }, "outside the strip"));
}
void ball_vector_assignment_unwind() {
  using namespace allocation_fault;
  sic::support::ComplexBallVector target(1), source(2);
  // Force native limb allocations too, so a prematurely cleared owner is visible
  // to ASan on destruction. Only the C++ vector allocation is fault-injected.
  sic::support::flint::Fmpz large;
  fmpz_one(large);
  fmpz_mul_2exp(large, large, 512);
  fmpz_add_ui(large, large, 1);
  acb_set_fmpz(target[0], large);
  SIC_CHECK(ARF_HAS_PTR(arb_midref(acb_realref(target[0]))));
  acb_set_fmpz(source[0], large);
  acb_add_ui(source[0], source[0], 1, 1024);
  acb_neg(source[1], source[0]);
  const sic::support::ComplexBallVector saved_target(target), saved_source(source);
  fail_size = source.size() * sizeof(acb_struct);
  skip = 0;
  fired = false;
  bool caught = false;
  try {
    target = source;
  } catch (const std::bad_alloc&) {
    caught = true;
  }
  fail_size = 0;
  SIC_CHECK(caught);
  SIC_CHECK(fired);
  SIC_CHECK(target.size() == saved_target.size());
  SIC_CHECK(acb_equal(target[0], saved_target[0]));
  SIC_CHECK(source.size() == saved_source.size());
  for (size_t i = 0; i < source.size(); ++i) SIC_CHECK(acb_equal(source[i], saved_source[i]));
  target = source;
  SIC_CHECK(target.size() == source.size());
  for (size_t i = 0; i < source.size(); ++i) SIC_CHECK(acb_equal(target[i], source[i]));
  target = target;
  SIC_CHECK(acb_equal(target[0], source[0]));
}
void table_unwind(const std::filesystem::path& marker) {
  sic::exact::Arithmetic ar(3);
  for (int workers : {1, 4}) {
    sic::support::ScopedStopFile stop(marker);
    sic::exact::Table table(ar, 256, workers);
    table.sd.resize(table.nstates());
    table.batch_state(0);
    table.release_states();
    // The final node admission leaves a stop file. The next parallel dispatch
    // throws after the phase scalars and slab arrays have acquired limb payloads.
    unsigned admissions = 0;
    table.admit_workspace = [&](auto) {
      ++admissions;
      std::ofstream(marker) << "stop\n";
    };
    SIC_CHECK(throws([&] { table.batch_state(0); }, "requested stop"));
    SIC_CHECK(admissions > 1);
    std::filesystem::remove(marker);
    table.release_states();
    table.admit_workspace = [](auto) {
      throw sic::support::resources::AdmissionError("unwind admission");
    };
    SIC_CHECK(throws([&] { table.batch_state(0); }, "unwind admission"));
  }
}
void ghost_unwind() {
  // A real certified table drives refinement through B0 before the observer
  // throws. All inverse, root-map, context and Newton locals must unwind.
  sic::exact::Arithmetic ar(3);
  sic::exact::Table table(ar, 128, 2);
  table.build_batched();
  table.release_states();
  for (const char* text : std::initializer_list<const char*>{nullptr, "", "garbage", "2junk",
                                                             "2147483648", "0", "1", "2"}) {
    sic::support::KernelSettings settings;
    if (text) settings.emplace("SIC_GHOST_DEBUG", text);
    sic::support::KernelSettingsScope scope(&settings);
    sic::exact::GhostRefiner refiner(8, 1, 1);
    sic::support::ComplexBallVector output;
    std::string why;
    FILE* capture = std::tmpfile();
    SIC_CHECK(capture != nullptr);
    std::fflush(stderr);
    const int saved_stderr = dup(STDERR_FILENO);
    SIC_CHECK(saved_stderr >= 0 && dup2(fileno(capture), STDERR_FILENO) >= 0);
    const bool refined = refiner.refine(table.val[0], 256, output.v, why);
    std::fflush(stderr);
    SIC_CHECK(dup2(saved_stderr, STDERR_FILENO) >= 0);
    close(saved_stderr);
    std::rewind(capture);
    std::string diagnostic;
    for (int ch; (ch = std::fgetc(capture)) != EOF;) diagnostic.push_back(static_cast<char>(ch));
    std::fclose(capture);
    SIC_CHECK(refined);
    SIC_CHECK((diagnostic.find("[ghost] times:") != std::string::npos) == (text != nullptr));
    SIC_CHECK((diagnostic.find("|X^2 - X|") != std::string::npos) ==
              (text && std::string(text) == "2"));
  }
  for (int workers : {1, 4}) {
    sic::exact::GhostRefiner refiner(8, 1, 1);
    refiner.kernel_threads = workers;
    bool visited = false;
    refiner.after_b0 = [&](const auto*, const auto&, auto, auto, auto) {
      visited = true;
      throw std::runtime_error("unwind after B0");
    };
    sic::support::ComplexBallVector output;
    std::string why;
    SIC_CHECK(throws([&] { refiner.refine(table.val[0], 256, output.v, why); }, "unwind after B0"));
    SIC_CHECK(visited);
  }
}
void ghost_allocation_unwind() {
  using namespace allocation_fault;
  // Discover the implementation's map node size without relying on its ABI.
  record = true;
  {
    std::map<long, acb_struct> probe;
    probe.emplace(0, acb_struct{});
  }
  record = false;
  const size_t map_node_size = recorded;
  SIC_CHECK(map_node_size > sizeof(acb_struct));
  sic::exact::Arithmetic ar(3);
  sic::exact::Table table(ar, 192, 1);
  table.build_batched();
  table.release_states();
  for (size_t preceding : {0u, 1u}) {
    sic::exact::GhostRefiner refiner(8, 1, 1);
    sic::support::ComplexBallVector output;
    std::string why;
    refiner.memory_observer = [&](const sic::exact::phase_memory::Event& event) {
      if (event.phase == sic::exact::phase_memory::Phase::roots &&
          std::strcmp(event.object, "phase") == 0) {
        fail_size = map_node_size;
        skip = preceding;
        fired = false;
      }
    };
    bool caught = false;
    try {
      refiner.refine(table.val[0], 384, output.v, why);
    } catch (const std::bad_alloc&) {
      caught = true;
    }
    fail_size = 0;
    SIC_CHECK(caught);
    SIC_CHECK(fired);
  }
  sic::exact::GhostRefiner warm(8, 1, 1);
  sic::support::ComplexBallVector output;
  std::string why;
  SIC_CHECK(warm.refine(table.val[0], 384, output.v, why));
  sic::exact::GhostRefiner refiner(8, 1, 1);
  refiner.S_hint = warm.S;
  refiner.hint_kind = warm.selection_kind;
  refiner.inverse_precision_hint = warm.inverse_precision_proposal;
  refiner.memory_observer = [&](const sic::exact::phase_memory::Event& event) {
    if (std::strcmp(event.object, "projection X") == 0) {
      fail_size = 14 * sizeof(acb_struct);
      skip = 0;
      fired = false;
    }
  };
  bool caught = false;
  try {
    refiner.refine(table.val[0], 384, output.v, why);
  } catch (const std::bad_alloc&) {
    caught = true;
  }
  fail_size = 0;
  SIC_CHECK(caught);
  SIC_CHECK(fired);
}
void workers_unwind() {
  std::atomic<unsigned> visits{0};
  SIC_CHECK(throws(
      [&] {
        sic::exact::parallel_for(16, 4, [&](long) {
          sic::support::flint::Arb x;
          arb_const_pi(x, 1024);
          ++visits;
          throw std::runtime_error("unwind worker");
        });
      },
      "unwind worker"));
  SIC_CHECK(visits > 0);
  sic::exact::RowWorkerTeam team(4);
  SIC_CHECK(throws(
      [&] {
        team.for_each(16, [](long) {
          sic::support::flint::Arb x;
          arb_const_pi(x, 1024);
          throw std::runtime_error("unwind row");
        });
      },
      "unwind row"));
  unsigned count = 0;
  team.for_each(1, [&](long) { ++count; });
  SIC_CHECK(count == 1);
}
void d2_unwind(const std::filesystem::path& root) {
  const auto radicals = root / "bad.sicrad";
  {
    std::ofstream f(radicals);
    f << "sic-exact-d2-radicals/1.0\nradicand 3\n"
         "alpha_squared 123456789012345678901234567890123456789/7 1/6\n"
         "beta_squared invalid 1/12\nend\n";
  }
  SIC_CHECK(throws(
      [&] { (void)sic::construct::dimension_two::make(root / "bad.sicx", radicals.string(), ""); },
      "bad rational in radical file"));
  SIC_CHECK(throws(
      [&] { (void)sic::construct::dimension_two::make(root / "missing" / "bad.sicx", "", ""); },
      "cannot write the balls file"));
}
}  // namespace

int main() {
  sic_test::TemporaryDirectory work("sic-exact-unwind");
  const auto root = work.path;
  {
    faddeev_unwind();
    ball_vector_assignment_unwind();
    table_unwind(root / "stop");
    ghost_unwind();
    ghost_allocation_unwind();
    workers_unwind();
    d2_unwind(root);
    sic::support::ScopedStopFile stop({});
    sic::support::stop_token.request_stop();
    SIC_CHECK(throws([] { sic::support::check_stop(); }, "requested stop"));
  }
  std::filesystem::remove_all(root);
  flint_cleanup_master();
  return sic_test::report();
}
