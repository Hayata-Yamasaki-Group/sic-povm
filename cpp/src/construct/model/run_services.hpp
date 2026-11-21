// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <functional>
#include <string>

#include "sic/support/stop.hpp"
namespace sic::construct {
struct RunServices {
  sic::support::StopToken* stop = nullptr;
  std::function<void(const std::string&)> progress;
};
}  // namespace sic::construct
