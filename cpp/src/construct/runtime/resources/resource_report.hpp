// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "construct/runtime/resources/construction_model.hpp"
namespace sic::construct::resources {
void report(const Model& model, const Budget& budget, sic::support::resources::BitCount precision,
            int workers, const char* output);
}
