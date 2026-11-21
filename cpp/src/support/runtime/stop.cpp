// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/stop.hpp"

namespace sic::support {
void StopToken::check() const try {
  if (source_.stop_requested() || (!file_.empty() && std::filesystem::exists(file_)))
    throw sic::Error(ErrorKind::cancelled, "requested stop: stop token or stop file");
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(ErrorKind::io, error.what());
}
}  // namespace sic::support

namespace sic::support {
void check_stop() { (current_stop_token ? *current_stop_token : stop_token).check(); }
}  // namespace sic::support

namespace sic::support {
ScopedStopFile::ScopedStopFile(std::filesystem::path file) : previous_(std::move(file)) {
  std::swap(stop_token, previous_);
}
}  // namespace sic::support

namespace sic::support {
ScopedStopFile::~ScopedStopFile() { std::swap(stop_token, previous_); }
}  // namespace sic::support

namespace sic::support {
StopToken stop_token;
}

namespace sic::support {
thread_local StopToken* current_stop_token = nullptr;
}

namespace sic::support {
StopToken::StopToken(std::filesystem::path file) : file_(std::move(file)) {}
}  // namespace sic::support

namespace sic::support {
StopScope::StopScope(StopToken* token) : previous_(current_stop_token) {
  current_stop_token = token;
}
StopScope::~StopScope() { current_stop_token = previous_; }
}  // namespace sic::support
