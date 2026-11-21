// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace sic::support::admission {
using Bytes = std::uint64_t;
Bytes add(Bytes a, Bytes b);
Bytes mul(Bytes a, Bytes b);
inline bool fits(Bytes used, Bytes bytes, Bytes limit) {
  return used <= limit && bytes <= limit - used;
}
// Set before launching workers; restore only after they have joined. Disabled
// by default so diagnostic I/O cannot serialize normal admission hot paths.
extern thread_local bool logging;
class LogScope {
 public:
  explicit LogScope(bool enabled);
  ~LogScope();
  LogScope(const LogScope&) = delete;
  LogScope& operator=(const LogScope&) = delete;

 private:
  bool previous_;
};
namespace detail {
// Keep diagnostic formatting out of the admission fast path, including the
// compiler's inlining budget. Normal decoder tasks must only test the flag.
[[gnu::cold, gnu::noinline]] void write_record(const char* resource, const char* stage,
                                               const char* decision, Bytes used, Bytes requested,
                                               Bytes limit);
}  // namespace detail
void record(const char* resource, const char* stage, const char* decision, Bytes used,
            Bytes requested, Bytes limit);
}  // namespace sic::support::admission
