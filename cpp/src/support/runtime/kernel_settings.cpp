// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/kernel_settings.hpp"

namespace sic::support {
const char* kernel_setting(const char* name) {
  if (!current_kernel_settings) return std::getenv(name);
  const auto found = current_kernel_settings->find(name);
  return found == current_kernel_settings->end() ? nullptr : found->second.c_str();
}
}  // namespace sic::support

namespace sic::support {
int progressf(const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  if (!current_progress_sink || !current_progress_sink->emit) {
    const int result = std::vfprintf(stdout, format, arguments);
    va_end(arguments);
    return result;
  }
  va_list copy;
  va_copy(copy, arguments);
  const int size = std::vsnprintf(nullptr, 0, format, copy);
  va_end(copy);
  if (size < 0) {
    va_end(arguments);
    throw std::runtime_error("progress formatting failed");
  }
  std::vector<char> text(static_cast<size_t>(size) + 1);
  std::vsnprintf(text.data(), text.size(), format, arguments);
  va_end(arguments);
  std::lock_guard lock(current_progress_sink->mutex);
  current_progress_sink->emit(std::string(text.data(), size));
  return size;
}
}  // namespace sic::support

namespace sic::support {
thread_local const KernelSettings* current_kernel_settings = nullptr;
}

namespace sic::support {
thread_local ProgressSink* current_progress_sink = nullptr;
}

namespace sic::support {
KernelSettingsScope::KernelSettingsScope(const KernelSettings* settings)
    : previous_(current_kernel_settings) {
  current_kernel_settings = settings;
}
KernelSettingsScope::~KernelSettingsScope() { current_kernel_settings = previous_; }
ProgressScope::ProgressScope(ProgressSink* sink) : previous_(current_progress_sink) {
  current_progress_sink = sink;
}
ProgressScope::~ProgressScope() { current_progress_sink = previous_; }
}  // namespace sic::support
