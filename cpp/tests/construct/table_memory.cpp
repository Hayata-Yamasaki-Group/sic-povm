// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/runtime/resources/table_memory.hpp"

#include "assertions.hpp"
#include "exact/analytic/table.hpp"
#include "exact/certification/reconstruction/ghost.hpp"
#include "exact/group/group.hpp"
#include "exact/runtime/newton_class_memory.hpp"

namespace phase_memory = sic::exact::phase_memory;

namespace {
void retained_table_ownership() {
  using namespace sic::exact;
  Arithmetic arithmetic(3);
  constexpr sic::support::resources::Bytes kInitial = 64 * sic::support::resources::kMib,
                                           scratch_bytes = 16 * sic::support::resources::kMib;
  for (bool refuse : {false, true}) {
    sic::support::Reservations pool(refuse ? kInitial + scratch_bytes
                                           : 128 * sic::support::resources::kMib);
    phase_memory::PreparedSeed<Table> prepared;
    auto grant = pool.try_acquire(kInitial);
    auto scratch = pool.try_acquire(scratch_bytes);
    auto table = std::make_unique<Table>(arithmetic, 128, 4);
    table->val.resize(arithmetic.h());
    table->val[0].resize(64);
    for (auto& value : table->val[0]) acb_init(&value);
    arb_const_pi(acb_realref(&table->val[0][1]), 8192);
    acb_set_round(&table->val[0][1], &table->val[0][1], 512);
    table->gl_rule(8, 8192);  // retained rule data, independent of Table.prec
    prepared.assign(std::move(table), std::move(grant));
    const auto stored = sic::construct::resources::inspect_retained_table(*prepared);
    SIC_CHECK(stored.owned > kInitial && stored.finite_lagom && stored.source.finite_lagom);
    SIC_CHECK(stored.source.bits > 128 && stored.source.bits <= 512 &&
              stored.source.maximum_heap > 1000);
    std::cout << "retained Table: nominal=128 current=" << stored.source.bits
              << " maximum_heap=" << stored.source.maximum_heap << " owned=" << stored.owned
              << " growth_refused=" << refuse << '\n';
    SIC_CHECK(stored.owned >= kInitial + phase_memory::inspect_storage(prepared->val[0]).bytes());
    SIC_CHECK(prepared.try_grow(stored.owned) != refuse);
    if (refuse) {
      prepared.reset();  // rejected Table dies before its build scratch
      SIC_CHECK(pool.used() == scratch_bytes);
    } else {
      SIC_CHECK(prepared.granted() == stored.owned);
      SIC_CHECK(pool.used() == stored.owned + scratch_bytes);
    }
    scratch.reset();
    prepared.reset();
    SIC_CHECK(pool.used() == 0);
  }
}
}  // namespace
int main() {
  retained_table_ownership();
  flint_cleanup_master();
  return sic_test::report();
}
