#include <flint/acb_mat.h>

#include <array>
#include <barrier>
#include <filesystem>
#include <fstream>

#include "assertions.hpp"
#include "exact/runtime/parallel.hpp"
#include "support/resources.hpp"
#include "temporary_directory.hpp"

namespace {
template <class F>
bool throws(F&& operation) {
  try {
    operation();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

void require(bool condition) {
  if (!condition) throw std::runtime_error("class native share worker assertion");
}

struct Handles {
  thread_pool_handle* handles = nullptr;
  slong count = 0;
  explicit Handles(int threads) { count = flint_request_threads(&handles, threads); }
  ~Handles() { flint_give_back_threads(handles, count); }
};
}  // namespace

int main() {
  using namespace sic::exact;
  setenv("SIC_PARALLEL_GRAIN", "1", 1);
  flint_set_num_threads(8);  // Initialize the global pool once, before callbacks.
  const int initial_nested = nested_loop_budget;
  std::array<int, 2> leased{}, restored{};
  std::barrier rendezvous(2);
  parallel_for(2, 2, [&](long index) {
    require(flint_get_num_threads() == 1 && nested_loop_budget == 1);
    {
      ClassNativeShare budget(4, 8, 2);
      require(flint_get_num_threads() == 4 && nested_loop_budget == 4);
      {
        Handles native(4);
        leased[index] = native.count;
        rendezvous.arrive_and_wait();  // Both grants coexist in the one pool.
        require(native.count == 3);
      }
      acb_mat_t matrix, inverse;
      acb_mat_init(matrix, 64, 64);
      acb_mat_init(inverse, 64, 64);
      acb_mat_one(matrix);
      const int success = acb_mat_approx_inv(inverse, matrix, 128);
      acb_mat_clear(matrix);
      acb_mat_clear(inverse);
      require(success && flint_get_num_threads() == 4);
    }
    restored[index] = flint_get_num_threads();
    require(nested_loop_budget == 1);
  });
  SIC_CHECK(leased[0] == 3);
  SIC_CHECK(leased[1] == 3);
  SIC_CHECK(restored[0] == 1);
  SIC_CHECK(restored[1] == 1);
  SIC_CHECK(flint_get_num_threads() == 8);
  SIC_CHECK(nested_loop_budget == initial_nested);

  // A final singleton bypasses parallel_for suppression. It must still use
  // the same four-thread class share and restore the caller's eight threads.
  int singleton_threads = 0;
  parallel_for(1, 2, [&](long) {
    ClassNativeShare budget(4, 8, 2);
    singleton_threads = flint_get_num_threads();
  });
  SIC_CHECK(singleton_threads == 4);
  SIC_CHECK(flint_get_num_threads() == 8);
  SIC_CHECK(nested_loop_budget == initial_nested);
  for (int fault : {0, 1}) {
    SIC_CHECK(throws([&] {
      parallel_for(2, 2, [&](long) {
        try {
          ClassNativeShare budget(4, 8, 2);
          if (fault) throw sic::support::resources::AdmissionError("injected resource refusal");
          throw std::runtime_error("injected class exception");
        } catch (...) {
          require(flint_get_num_threads() == 1 && nested_loop_budget == 1);
          throw;
        }
      });
    }));
    SIC_CHECK(flint_get_num_threads() == 8);
    SIC_CHECK(nested_loop_budget == initial_nested);
  }
  sic_test::TemporaryDirectory scratch("sic-class-share-stop");
  const auto marker = scratch.path / "stop";
  sic::support::ScopedStopFile stop_scope(marker);
  SIC_CHECK(throws([&] {
    parallel_for(1, 2, [&](long) {
      ClassNativeShare budget(4, 8, 2);
      std::ofstream(marker) << "stop\n";
      parallel_for(1, 4, [](long) {});
    });
  }));
  std::filesystem::remove(marker);
  SIC_CHECK(flint_get_num_threads() == 8);
  SIC_CHECK(nested_loop_budget == initial_nested);
  for (const auto& limits : {std::array{0, 8, 2}, std::array{5, 8, 2}, std::array{4, 8, 0}}) {
    SIC_CHECK(throws([&] { ClassNativeShare invalid(limits[0], limits[1], limits[2]); }));
    SIC_CHECK(flint_get_num_threads() == 8);
    SIC_CHECK(nested_loop_budget == initial_nested);
  }
  puts(
      "Owned class shares: paired native leases3+3, final singleton4, TLS/nested restoration "
      "PASS.");
  flint_cleanup_master();
  return sic_test::report();
}
