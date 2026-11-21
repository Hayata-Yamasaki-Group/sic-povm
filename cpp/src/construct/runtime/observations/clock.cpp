// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/runtime/observations/clock.hpp"

#include <chrono>

#include "construct/model/session_state.hpp"
namespace sic::construct {
double secs() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}
}  // namespace sic::construct

namespace sic::construct {
double Construction::stamp() { return secs() - T0; }
}  // namespace sic::construct
