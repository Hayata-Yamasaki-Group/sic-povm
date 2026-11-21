// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <filesystem>
#include <stdexcept>
#include <stop_token>
#include <utility>

#include "sic/error.hpp"

namespace sic::support {
// Configure before launching workers. All exact kernels share this source;
// programmatic cancellation is sticky, while a stop file is polled at the
// existing kernel boundaries and remains removable by the caller.
class StopToken {
 public:
  explicit StopToken(std::filesystem::path file = {});
  void request_stop() { source_.request_stop(); }
  void check() const;

 private:
  std::filesystem::path file_;
  std::stop_source source_;
};
extern StopToken stop_token;
extern thread_local StopToken* current_stop_token;
class StopScope {
 public:
  explicit StopScope(StopToken* token);
  ~StopScope();
  StopScope(const StopScope&) = delete;
  StopScope& operator=(const StopScope&) = delete;

 private:
  StopToken* previous_;
};
void check_stop();
// Scope changes are owner-thread operations, outside any running worker job.
class ScopedStopFile {
 public:
  explicit ScopedStopFile(std::filesystem::path file);
  ~ScopedStopFile();
  ScopedStopFile(const ScopedStopFile&) = delete;
  ScopedStopFile& operator=(const ScopedStopFile&) = delete;

 private:
  StopToken previous_;
};
}  // namespace sic::support
