// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "construct/runtime/resources/table_memory.hpp"
namespace sic::construct {
struct NewtonCachePlan {
  size_t reused = 0, physical = 0;
  std::unique_ptr<sic::exact::Table> table;
};
struct NewtonResidentPlan {
  sic::support::resources::Bytes resident = 0, future_controls = 0;
  resources::RetainedTable table;
};
}  // namespace sic::construct
