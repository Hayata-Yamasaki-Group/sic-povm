// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/runtime/runtime.hpp"

namespace sic::construct {
std::mutex NativeRuntimeLease::mutex_;
NativeRuntimeLease::NativeRuntimeLease() : lock_(mutex_), previous_(flint_get_num_threads()) {}
NativeRuntimeLease::~NativeRuntimeLease() { flint_set_num_threads(previous_); }
}  // namespace sic::construct
