// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::construct {
struct ProducerOptions;
struct Components;
struct RunServices;
int run(const ProducerOptions&, Components, const RunServices&);
}  // namespace sic::construct
