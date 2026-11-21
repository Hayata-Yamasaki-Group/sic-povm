#include "exact/runtime/memory/phase_accounting.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <thread>

#include "assertions.hpp"
#include "exact/runtime/memory/ball_storage.hpp"
#include "exact/runtime/memory/product_tiles.hpp"
#include "support/runtime/reservations.hpp"

namespace {
using sic::support::Reservations;
using sic::support::resources::Bytes;
template <class F>
bool throws(F&& operation) {
  try {
    operation();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

void ownership() {
  SIC_CHECK(throws([] { sic::support::Reservations invalid(0); }));
  sic::support::Reservations pool(100);
  SIC_CHECK(throws([&] { pool.try_acquire(0); }));
  SIC_CHECK(!pool.try_acquire(101));
  auto first = pool.try_acquire(40);
  auto second = pool.try_acquire(60);
  SIC_CHECK(first && second && pool.used() == 100);
  SIC_CHECK(!first->try_resize(41));
  SIC_CHECK(first->bytes() == 40 && pool.used() == 100);
  SIC_CHECK(second->try_resize(20));
  SIC_CHECK(first->try_resize(80));
  SIC_CHECK(pool.used() == 100);
  SIC_CHECK(!pool.try_acquire(UINT64_MAX));
  first.reset();
  SIC_CHECK(pool.used() == 20);
  auto third = pool.try_acquire(70);
  *second = std::move(*third);  // Releases old ownership before taking new.
  SIC_CHECK(second->bytes() == 70 && third->bytes() == 0 && pool.used() == 70);
  SIC_CHECK(throws([&] { third->try_resize(1); }));
  *second = std::move(*second);
  SIC_CHECK(second->bytes() == 70 && pool.used() == 70);
  SIC_CHECK(second->try_resize(0));
  SIC_CHECK(pool.used() == 0);
  SIC_CHECK(second->try_resize(100));
  second.reset();
  third.reset();
  SIC_CHECK(pool.used() == 0);

  // Lease state survives the scheduler facade. No dangling pool reference.
  std::optional<sic::support::Reservations::Lease> survivor;
  {
    sic::support::Reservations local(8);
    survivor = local.try_acquire(4);
  }
  SIC_CHECK(survivor->try_resize(8));
  SIC_CHECK(!survivor->try_resize(9));
  survivor.reset();

  sic::support::Reservations other(100);
  auto a = pool.try_acquire(30);
  auto b = other.try_acquire(70);
  *a = std::move(*b);
  SIC_CHECK(pool.used() == 0 && other.used() == 70);
  SIC_CHECK(a->bytes() == 70 && b->bytes() == 0);
  a.reset();
  b.reset();
  SIC_CHECK(other.used() == 0);

  auto owned_context = std::make_shared<Bytes>(6);
  {
    sic::support::Reservations local(
        8, [context = owned_context](Bytes total) { return total <= *context; });
    survivor = local.try_acquire(4);
  }
  owned_context.reset();
  SIC_CHECK(survivor->try_resize(6));
  SIC_CHECK(!survivor->try_resize(7));
  survivor.reset();

  // Addition never wraps, even at the largest representable capacity.
  sic::support::Reservations large(UINT64_MAX);
  auto almost_all = large.try_acquire(UINT64_MAX - 1);
  SIC_CHECK(!large.try_acquire(2));
  auto last = large.try_acquire(1);
  SIC_CHECK(last && large.used() == UINT64_MAX);
  SIC_CHECK(!last->try_resize(2));
}

void fresh_admission() {
  std::vector<Bytes> observations;
  Bytes fresh_limit = 80;
  bool inject_failure = false;
  sic::support::Reservations pool(100, [&](Bytes aggregate) {
    observations.push_back(aggregate);
    if (inject_failure) throw sic::support::resources::AdmissionError("injected fresh refusal");
    return aggregate <= fresh_limit;
  });
  auto first = pool.try_acquire(40);
  SIC_CHECK(!pool.try_acquire(50));
  SIC_CHECK(pool.used() == 40);
  auto second = pool.try_acquire(40);
  SIC_CHECK(second && pool.used() == 80);
  SIC_CHECK(!first->try_resize(41));
  SIC_CHECK(first->bytes() == 40 && pool.used() == 80);
  SIC_CHECK(observations == std::vector<Bytes>({40, 90, 80, 81}));
  inject_failure = true;
  SIC_CHECK(throws([&] { first->try_resize(50); }));
  SIC_CHECK(first->bytes() == 40 && pool.used() == 80);
  SIC_CHECK(throws([&] { pool.try_acquire(1); }));
  SIC_CHECK(pool.used() == 80);
  // Fresh pressure must never prevent releasing existing obligations.
  fresh_limit = 0;
  SIC_CHECK(first->try_resize(20));
  second.reset();
  SIC_CHECK(pool.used() == 20);
  first.reset();
  SIC_CHECK(pool.used() == 0);
}

void simultaneous_grants() {
  // Both requests occur before either successful owner can release. Exactly
  // one request fits, even though both would fit an independent RSS snapshot.
  for (int iteration = 0; iteration < 100; ++iteration) {
    sic::support::Reservations pool(10);
    std::barrier start(2), acquired(2);
    std::array<bool, 2> granted{};
    std::array<std::thread, 2> workers;
    for (size_t id = 0; id < workers.size(); ++id) {
      workers[id] = std::thread([&, id] {
        start.arrive_and_wait();
        auto lease = pool.try_acquire(6);
        granted[id] = lease.has_value();
        acquired.arrive_and_wait();
      });
    }
    for (auto& worker : workers) worker.join();
    SIC_CHECK(granted[0] != granted[1]);
    SIC_CHECK(pool.used() == 0);
  }
  // More owners repeatedly grow/shrink; callbacks see serialized aggregate
  // obligations, rather than just the request of the current class.
  std::atomic<bool> invalid{false};
  Bytes largest = 0;
  sic::support::Reservations pool(1024, [&](Bytes aggregate) {
    if (aggregate > 1024) invalid = true;
    largest = std::max(largest, aggregate);  // protected by pool's grant mutex
    return true;
  });
  std::barrier start(16);
  std::vector<std::thread> workers;
  for (int id = 0; id < 16; ++id)
    workers.emplace_back([&, id] {
      start.arrive_and_wait();
      for (int repeat = 0; repeat < 200; ++repeat) {
        auto lease = pool.try_acquire(32);
        if (!lease) continue;
        const Bytes before = lease->bytes();
        if (!lease->try_resize(96 + id) && lease->bytes() != before) invalid = true;
        if (!lease->try_resize(16) || lease->bytes() != 16) invalid = true;
      }
    });
  for (auto& worker : workers) worker.join();
  SIC_CHECK(!invalid);
  SIC_CHECK(largest <= 1024);
  SIC_CHECK(pool.used() == 0);
}

void teardown_and_retry() {
  // Synthetic lifetime trace, not a production Newton allocation test.
  // A refusal unwinds scratch before releasing the lease, preserves the
  // certified input, and never commits an incomplete result.
  constexpr std::array<Bytes, 8> kPhases{28, 35, 40, 55, 32, 63, 67, 42};
  const std::vector<unsigned char> certified_seed(8, 0x5a);
  for (size_t fault = 0; fault < kPhases.size(); ++fault) {
    size_t phase = 0;
    bool refuse = true;
    bool published = false;
    sic::support::Reservations pool(100, [&](Bytes aggregate) {
      return aggregate == certified_seed.size() || !refuse || phase != fault;
    });
    auto run = [&] {
      auto lease = pool.try_acquire(8);
      if (!lease) return false;
      // Scratch is declared AFTER its owner, so unwinding frees it first.
      auto seed = certified_seed;
      std::optional<sic::support::Reservations::Lease> phase_lease;
      std::vector<unsigned char> scratch;
      for (phase = 0; phase < kPhases.size(); ++phase) {
        const Bytes total = kPhases[phase];
        std::vector<unsigned char>().swap(scratch);
        phase_lease.reset();
        phase_lease = pool.try_acquire(total - seed.size());
        if (!phase_lease) return false;
        scratch.assign(total - seed.size(), 0x11);
        if (seed != certified_seed) throw std::logic_error("mutated certified input");
      }
      published = true;
      return true;
    };
    SIC_CHECK(!run());
    SIC_CHECK(!published);
    SIC_CHECK(pool.used() == 0);
    refuse = false;
    phase = 0;
    SIC_CHECK(run());
    SIC_CHECK(published);
    SIC_CHECK(pool.used() == 0);
  }
  sic::support::Reservations pool(100);
  SIC_CHECK(throws([&] {
    auto lease = pool.try_acquire(90);
    std::vector<unsigned char> scratch(90);
    throw std::runtime_error("injected phase worker exception");
  }));
  SIC_CHECK(pool.used() == 0);
  auto singleton = pool.try_acquire(100);
  SIC_CHECK(singleton && singleton->bytes() == 100);
}

void retained_ball_storage() {
  using sic::exact::phase_memory::inspect_storage;
  std::vector<acb_struct> values(3);
  values.reserve(8);
  for (auto& value : values) acb_init(&value);
  struct Clear {
    std::vector<acb_struct>& values;
    ~Clear() {
      for (auto& value : values) acb_clear(&value);
    }
  } clear{values};
  const auto empty = inspect_storage(values);
  SIC_CHECK(empty.values == 3 && empty.header_bytes >= 8 * sizeof(acb_struct));
  SIC_CHECK(empty.heap_bytes == 0 && empty.finite_lagom);
  for (auto& value : values) {
    arb_const_pi(acb_realref(&value), 8192);
    arb_set(acb_imagref(&value), acb_realref(&value));
  }
  const auto wide = inspect_storage(values);
  SIC_CHECK(wide.largest_mantissa > 8000 && wide.heap_bytes > 6000);
  for (auto& value : values) acb_set_round(&value, &value, 512);
  const auto rounded = inspect_storage(values);
  SIC_CHECK(rounded.largest_mantissa <= 512);
  SIC_CHECK(rounded.heap_bytes >= wide.heap_bytes);
  SIC_CHECK(rounded.bytes() > values.capacity() * sic::support::resources::complex_bytes(512));
  SIC_CHECK(rounded.finite_lagom);

  // A finite value with a one-bit mantissa can still allocate its exponent.
  for (auto& value : values) acb_zero(&value);
  fmpz_t exponent;
  fmpz_init(exponent);
  fmpz_one(exponent);
  fmpz_mul_2exp(exponent, exponent, 128);
  acb_one(&values[0]);
  arb_mul_2exp_fmpz(acb_realref(&values[0]), acb_realref(&values[0]), exponent);
  const auto huge_exponent = inspect_storage(values);
  SIC_CHECK(acb_is_finite(&values[0]));
  SIC_CHECK(huge_exponent.largest_mantissa == 1);
  SIC_CHECK(!huge_exponent.finite_lagom && huge_exponent.heap_bytes > 0);
  acb_one(&values[0]);
  mag_set_ui(arb_radref(acb_realref(&values[0])), 1);
  mag_mul_2exp_fmpz(arb_radref(acb_realref(&values[0])), arb_radref(acb_realref(&values[0])),
                    exponent);
  const auto huge_radius = inspect_storage(values);
  SIC_CHECK(acb_is_finite(&values[0]));
  SIC_CHECK(huge_radius.largest_mantissa == 1);
  SIC_CHECK(!huge_radius.finite_lagom && huge_radius.heap_bytes > 0);
  fmpz_clear(exponent);
  acb_indeterminate(&values[0]);
  SIC_CHECK(!inspect_storage(values).finite_lagom);
}

void phase_forecasts() {
  namespace phase_memory = sic::exact::phase_memory;

  phase_memory::Ceilings c;
  c.dimension = 64;
  c.source_bits = c.selection_bits = 256;
  c.inverse_bits = c.stored_matrix_bits = 512;
  c.high_bits = 4096;
  c.candidate_rows = 8 * (2 * c.dimension - 2);
  c.workers = 4;
  c.modular = c.column_inverse = c.threaded_product = true;
  c.refined_correction = c.parallel_reconstruction = true;
  const auto first = phase_memory::forecast(c);
  const sic::support::resources::Count D = c.dimension * c.dimension, N = 2 * c.dimension - 2;
  SIC_CHECK(first.empty_matrix >= N * N * sizeof(acb_struct));
  SIC_CHECK(first.reshape == (N * N + 3 * N) * sizeof(double));
  SIC_CHECK(first.at(phase_memory::Phase::projection) > 4 * first.source_grid);
  SIC_CHECK(first.at(phase_memory::Phase::inverse) >= 8 * first.matrix + 2 * first.source_grid);
  SIC_CHECK(first.at(phase_memory::Phase::certification) >=
            phase_memory::kCertificationMatrixOwners * first.matrix + first.reshape +
                first.source_grid);
  SIC_CHECK(phase_memory::kCertificationMatrixOwners == 4);
  // The product term changed; the independent inverse/native envelope did not.
  const auto product = sic::exact::product_tiles::scratch(
      N, c.workers, sic::support::resources::newton_matrix_bits(c.stored_matrix_bits));
  SIC_CHECK(first.at(phase_memory::Phase::certification) >=
            first.small_objects + first.reshape + first.source_grid + 4 * first.matrix + product);
  SIC_CHECK(first.at(phase_memory::Phase::high_newton) >
            first.source_grid + 2 * first.matrix +
                2 * D * sic::support::resources::complex_bytes(c.high_bits));
  for (Bytes phase : first.totals) SIC_CHECK(first.peak() >= phase);

  // Previous output coexists with high Newton and certification. It is gone
  // before reconstruction, and was released on entry before source setup.
  c.previous_output_bits = 3072;
  const auto retry = phase_memory::forecast(c);
  SIC_CHECK(retry.at(phase_memory::Phase::high_newton) -
                first.at(phase_memory::Phase::high_newton) ==
            retry.previous_output);
  SIC_CHECK(retry.at(phase_memory::Phase::certification) -
                first.at(phase_memory::Phase::certification) ==
            retry.previous_output);
  for (auto phase :
       {phase_memory::Phase::roots, phase_memory::Phase::projection, phase_memory::Phase::selection,
        phase_memory::Phase::inverse, phase_memory::Phase::reconstruction})
    SIC_CHECK(retry.at(phase) == first.at(phase));

  // Equal arithmetic ceilings do not erase a larger actual retained buffer.
  c.previous_output_storage_bytes = 2 * retry.previous_output;
  const auto retained = phase_memory::forecast(c);
  SIC_CHECK(retained.previous_output > retry.previous_output);
  SIC_CHECK(retained.at(phase_memory::Phase::high_newton) >
            retry.at(phase_memory::Phase::high_newton));
  c.source_storage_bytes = 3 * first.source_grid;
  const auto wider_source = phase_memory::forecast(c);
  SIC_CHECK(wider_source.at(phase_memory::Phase::projection) -
                retained.at(phase_memory::Phase::projection) ==
            4 * (wider_source.source_grid - retained.source_grid));

  c.previous_output_storage_bytes = c.previous_output_bits = c.source_storage_bytes = 0;
  c.refined_correction = false;
  const auto unrefined = phase_memory::forecast(c);
  SIC_CHECK(unrefined.at(phase_memory::Phase::high_newton) <=
            first.at(phase_memory::Phase::high_newton));
  // At sufficiently high precision the two high caches dominate low-q
  // correction work. The forecast is a maximum, not a sum of both phases.
  auto dominating = c;
  dominating.high_bits = 262144;
  const auto without_correction = phase_memory::forecast(dominating);
  dominating.refined_correction = true;
  SIC_CHECK(phase_memory::forecast(dominating).at(phase_memory::Phase::high_newton) ==
            without_correction.at(phase_memory::Phase::high_newton));
  c.parallel_reconstruction = false;
  const auto serial_output = phase_memory::forecast(c);
  SIC_CHECK(serial_output.at(phase_memory::Phase::reconstruction) <
            unrefined.at(phase_memory::Phase::reconstruction));
  c.stored_matrix_bits = 1024;
  const auto old_m0 = phase_memory::forecast(c);
  SIC_CHECK(old_m0.matrix > first.matrix);
  SIC_CHECK(old_m0.at(phase_memory::Phase::certification) >
            serial_output.at(phase_memory::Phase::certification));
  c.candidate_rows = D - 1;
  const auto reference_selection = phase_memory::forecast(c);
  SIC_CHECK(reference_selection.at(phase_memory::Phase::selection) >
            old_m0.at(phase_memory::Phase::selection));
  c.modular = false;
  SIC_CHECK(throws([&] { phase_memory::forecast(c); }));
  c.native_scalar_bytes = sizeof(long double);
  c.native_complex_bytes = 2 * sizeof(long double);
  SIC_CHECK(phase_memory::forecast(c).at(phase_memory::Phase::selection) > 0);
  c.dimension = 63;
  SIC_CHECK(throws([&] { phase_memory::forecast(c); }));
  c.dimension = 64;
  c.workers = 0;
  SIC_CHECK(throws([&] { phase_memory::forecast(c); }));
  c.workers = 4;
  c.source_bits = UINT64_MAX;
  SIC_CHECK(throws([&] { phase_memory::forecast(c); }));
  c.source_bits = c.selection_bits = UINT64_MAX;
  SIC_CHECK(throws([&] { phase_memory::forecast(c); }));
}
}  // namespace

int main() {
  ownership();
  fresh_admission();
  simultaneous_grants();
  teardown_and_retry();
  retained_ball_storage();
  phase_forecasts();
  flint_cleanup_master();
  return sic_test::report();
}
