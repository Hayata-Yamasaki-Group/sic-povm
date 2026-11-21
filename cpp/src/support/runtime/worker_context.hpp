// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sic/support/admission.hpp"
#include "sic/support/kernel_settings.hpp"
#include "sic/support/stop.hpp"

namespace sic::support {
// Borrowed request state for a synchronous worker dispatch, including native pools.
struct WorkerContext {
  StopToken* stop = nullptr;
  const KernelSettings* settings = nullptr;
  ProgressSink* progress = nullptr;
  bool logging = false;
  static WorkerContext capture();
};
class WorkerContextScope {
 public:
  explicit WorkerContextScope(const WorkerContext& context);

 private:
  StopScope stop_;
  KernelSettingsScope settings_;
  ProgressScope progress_;
  admission::LogScope logging_;
};
}  // namespace sic::support
