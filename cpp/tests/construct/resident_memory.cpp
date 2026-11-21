// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/runtime/resources/resident_memory.hpp"

#include "assertions.hpp"
#include "support/resources.hpp"

namespace phase_memory = sic::exact::phase_memory;

using namespace sic::exact;

namespace {
void retained_capacities() {
  Arithmetic arithmetic(3);
  Group group(arithmetic);
  group.generate_standard();
  group.close(
      sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")));
  const auto original = sic::construct::resources::inspect_group(group);
  group.gens[0].perm.reserve(group.gens[0].perm.capacity() + 8192);
  const auto enlarged = sic::construct::resources::inspect_group(group);
  SIC_CHECK(enlarged.payload >= original.payload + 8192 * sizeof(int));
  group.gens[0].perm.clear();
  SIC_CHECK(sic::construct::resources::inspect_group(group).bytes() == enlarged.bytes());

  auto layout = std::make_shared<OrbitTableLayout>(arithmetic.d, arithmetic.h(), group.orbit);
  const auto one = sic::construct::resources::inspect_layouts(group, {layout.get()});
  SIC_CHECK(sic::construct::resources::inspect_layouts(group, {layout.get(), layout.get(), nullptr})
                .bytes() == one.bytes());
  auto second = std::make_shared<OrbitTableLayout>(arithmetic.d, arithmetic.h(), group.orbit);
  SIC_CHECK(
      sic::construct::resources::inspect_layouts(group, {layout.get(), second.get()}).bytes() ==
      2 * one.bytes());
  layout->conjugate_index[0].reserve(4096);
  SIC_CHECK(sic::construct::resources::inspect_layouts(group, {layout.get()}).payload >=
            one.payload + 4096 * sizeof(int));
  SIC_CHECK(sic::construct::resources::inspect_group(group).bytes() == enlarged.bytes());
  auto other_canonical = group.orbit;
  OrbitTableLayout borrowed(arithmetic.d, arithmetic.h(), other_canonical);
  bool rejected = false;
  try {
    (void)sic::construct::resources::inspect_layouts(group, {&borrowed});
  } catch (const phase_memory::StorageContractError&) {
    rejected = true;
  }
  SIC_CHECK(rejected);

  const auto before = sic::construct::resources::inspect_arithmetic(arithmetic);
  fmpz_t large;
  fmpz_init(large);
  fmpz_one(large);
  fmpz_mul_2exp(large, large, 32768);
  fmpq_set_fmpz(arithmetic.cls[0].tau[0].a.v, large);
  SIC_CHECK(sic::construct::resources::inspect_arithmetic(arithmetic).payload >
            before.payload + 4096);
  fmpz_clear(large);
}

void retained_balls_and_retry_owners() {
  struct TrivialOwner {
    long* pointer;
  };
  static_assert(std::is_trivially_copyable_v<TrivialOwner>);
  static_assert(!sic::construct::resources::ResidentCensus::kPlainLabel<TrivialOwner>);
  static_assert(!sic::construct::resources::ResidentCensus::kPlainLabel<acb_struct>);
  Arithmetic arithmetic(3);
  Table table(arithmetic, 128, 1);
  table.val.resize(arithmetic.h());
  table.val[0].resize(64);
  for (auto& value : table.val[0]) acb_init(&value);
  table.val[0].reserve(128);
  arb_const_pi(acb_realref(&table.val[0][1]), 16384);
  acb_set_round(&table.val[0][1], &table.val[0][1], 512);
  table.gl_rule(8, 4096);
  const auto stored = sic::construct::resources::inspect_retained_table(table);
  const auto values = phase_memory::inspect_storage(table.val[0]);
  SIC_CHECK(stored.finite_lagom && stored.source.finite_lagom);
  SIC_CHECK(stored.source.bits <= 512 && stored.source.maximum_heap > 2000);
  SIC_CHECK(stored.owned >=
            64 * sic::support::resources::kMib + values.bytes() + 64 * table.val[0].capacity());
  phase_memory::CopySource separate;
  separate.observe(table.zero_value[0]);
  for (const auto& value : table.val[0]) separate.observe(&value);
  SIC_CHECK(stored.source.bits == separate.bits);
  SIC_CHECK(stored.source.maximum_heap == separate.maximum_heap);
  sic::construct::resources::ResidentCensus raw_balls;
  raw_balls.dynamic(table.val);
  SIC_CHECK(raw_balls.storage.payload >= values.bytes());
  acb_indeterminate(&table.val[0][3]);
  SIC_CHECK(!sic::construct::resources::inspect_retained_table(table).finite_lagom);

  sic::support::ComplexBallVector path(4);
  arb_const_pi(acb_realref(path[0]), 8192);
  std::vector<std::vector<long>> representatives(3);
  representatives[1].reserve(8192);
  std::vector<Leaf> leaves(4);
  fmpz_one(leaves[0].x);
  fmpz_mul_2exp(leaves[0].x, leaves[0].x, 8192);
  sic::construct::resources::ResidentCensus caller;
  caller.dynamic(representatives);
  caller.dynamic(path);
  caller.dynamic(leaves);
  SIC_CHECK(caller.storage.payload >= 8192 * sizeof(long) + 2 * 1024);
  const auto payload = caller.storage.payload;
  representatives[1].clear();
  sic::construct::resources::ResidentCensus again;
  again.dynamic(representatives);
  again.dynamic(path);
  again.dynamic(leaves);
  SIC_CHECK(again.storage.payload == payload);

  LeafSpool::Ticket ticket;
  ticket.index = std::make_shared<std::vector<off_t>>();
  ticket.index->reserve(4096);
  sic::construct::resources::ResidentCensus shared;
  shared.dynamic(ticket);
  const auto first = shared.storage.bytes();
  shared.dynamic(ticket);
  SIC_CHECK(shared.storage.bytes() == first);
  auto parts = std::make_shared<std::vector<LeafSpool::Ticket>>(2, ticket);
  LeafSpool::Ticket tree;
  tree.parts = parts;
  shared.dynamic(tree);
  SIC_CHECK(shared.storage.payload >= 4096 * sizeof(off_t) + 2 * sizeof(ticket));
  SIC_CHECK(shared.storage.bytes() < first + 4096 * sizeof(off_t));
}

void floors_and_future_controls() {
  constexpr sic::support::resources::Bytes kFixed = 1000, caller = 2000, floor = 3000;
  auto plan = sic::construct::resources::resident_plan(100, kFixed, caller, floor, 400, 500);
  SIC_CHECK(plan.inspected == kFixed + floor && plan.total() == 4500);
  SIC_CHECK(
      sic::construct::resources::resident_plan(9000, kFixed, caller, floor, 400, 500).total() ==
      9500);
  SIC_CHECK(
      sic::construct::resources::resident_plan(100, kFixed, caller, floor, 12000, 500).total() ==
      12500);
  SIC_CHECK(sic::construct::resources::resident_plan(100, kFixed, 5000, floor, 400, 500).total() ==
            6500);
  const auto controls = sic::construct::resources::newton_future_controls(2048, 128, 1000);
  SIC_CHECK(controls > 128 * 4 * sic::support::resources::kMib);
  SIC_CHECK(sic::construct::resources::newton_future_controls(2048, 128, 2000) > controls);
  SIC_CHECK(sic::construct::resources::newton_future_controls(4096, 128, 1000) > controls);
  sic::support::resources::Budget budget;
  budget.initial_limit = 2048;
  SIC_CHECK(budget.workers(2, 0, 512) == 2);
  bool refused = false;
  try {
    (void)budget.workers(
        2, sic::construct::resources::resident_plan(0, 0, 0, 0, 0, controls).total(), 1024);
  } catch (const sic::support::resources::AdmissionError&) {
    refused = true;
  }
  SIC_CHECK(refused);
  std::string oversized_reason(sic::construct::resources::kNewtonClassControlLimit, 'x');
  refused = false;
  try {
    sic::construct::resources::require_class_controls({}, oversized_reason, {});
  } catch (const sic::support::resources::AdmissionError&) {
    refused = true;
  }
  SIC_CHECK(refused);
  bool overflow = false;
  try {
    (void)sic::construct::resources::resident_plan(0, UINT64_MAX, 1, 0, 0, 0);
  } catch (const std::overflow_error&) {
    overflow = true;
  }
  SIC_CHECK(overflow);
}
}  // namespace

int main() {
  retained_capacities();
  retained_balls_and_retry_owners();
  floors_and_future_controls();
  flint_cleanup();
  return sic_test::report();
}
