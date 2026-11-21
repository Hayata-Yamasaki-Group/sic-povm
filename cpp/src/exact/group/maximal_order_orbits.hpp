// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "exact/arithmetic/maximal_order.hpp"
#include "exact/group/group.hpp"
namespace sic::exact::reduced_order {
long configure_group_orbits(Group& G, const CycleUnit& unit);
}
