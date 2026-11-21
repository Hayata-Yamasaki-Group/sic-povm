// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <optional>
namespace sic::construct {
int default_producer_workers(unsigned exponent);
int newton_class_workers(std::optional<int> class_threads, int threads, int remaining);
}  // namespace sic::construct
