// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
namespace sic::support {
using KernelSettings = std::map<std::string, std::string, std::less<>>;
extern thread_local const KernelSettings* current_kernel_settings;
const char* kernel_setting(const char* name);
class KernelSettingsScope {
 public:
  explicit KernelSettingsScope(const KernelSettings* settings);
  ~KernelSettingsScope();
  KernelSettingsScope(const KernelSettingsScope&) = delete;

 private:
  const KernelSettings* previous_;
};
struct ProgressSink {
  std::function<void(const std::string&)> emit;
  mutable std::mutex mutex;
};
extern thread_local ProgressSink* current_progress_sink;
class ProgressScope {
 public:
  explicit ProgressScope(ProgressSink* sink);
  ~ProgressScope();

 private:
  ProgressSink* previous_;
};
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
int
progressf(const char* format, ...);
}  // namespace sic::support
