// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/acb.h>
#include <flint/flint.h>

#include <functional>
#include <mutex>
#include <string>

#include "sic/support/kernel_settings.hpp"
#include "sic/support/stop.hpp"
namespace sic::construct {
// FLINT's configured worker count is process-wide. Native construction runs
// are serialized in this process, restore the previous configuration, and never
// alter the environment. Other clients of this native setting must share this lease.
class NativeRuntimeLease {
 public:
  NativeRuntimeLease();
  ~NativeRuntimeLease();

 private:
  static std::mutex mutex_;
  std::unique_lock<std::mutex> lock_;
  int previous_;
};
}  // namespace sic::construct
