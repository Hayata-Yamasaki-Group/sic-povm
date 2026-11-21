// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/runtime/worker_context.hpp"

namespace sic::support {
WorkerContext WorkerContext::capture() {
  return {current_stop_token, current_kernel_settings, current_progress_sink, admission::logging};
}
WorkerContextScope::WorkerContextScope(const WorkerContext& context)
    : stop_(context.stop),
      settings_(context.settings),
      progress_(context.progress),
      logging_(context.logging) {}
}  // namespace sic::support
