// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/admission.hpp"

namespace sic::support::admission {
Bytes add(Bytes a, Bytes b) {
  if (b > std::numeric_limits<Bytes>::max() - a)
    throw std::overflow_error("resource byte-count addition overflow");
  return a + b;
}
}  // namespace sic::support::admission

namespace sic::support::admission {
Bytes mul(Bytes a, Bytes b) {
  if (a && b > std::numeric_limits<Bytes>::max() / a)
    throw std::overflow_error("resource byte-count multiplication overflow");
  return a * b;
}
}  // namespace sic::support::admission

namespace sic::support::admission::detail {
[[gnu::cold, gnu::noinline]] void write_record(const char* resource, const char* stage,
                                               const char* decision, Bytes used, Bytes requested,
                                               Bytes limit) {
  // One stdio call keeps concurrent records intact. Stages are internal names.
  std::fprintf(stderr,
               "ADMISSION resource=%s stage=\"%s\" decision=%s used=%llu request=%llu limit=%llu\n",
               resource, stage, decision, static_cast<unsigned long long>(used),
               static_cast<unsigned long long>(requested), static_cast<unsigned long long>(limit));
}
}  // namespace sic::support::admission::detail

namespace sic::support::admission {
thread_local bool logging = false;
}

namespace sic::support::admission {
void record(const char* resource, const char* stage, const char* decision, Bytes used,
            Bytes requested, Bytes limit) {
  if (logging) [[unlikely]]
    detail::write_record(resource, stage, decision, used, requested, limit);
}
}  // namespace sic::support::admission

namespace sic::support::admission {
LogScope::LogScope(bool enabled) : previous_(logging) { logging = enabled; }
LogScope::~LogScope() { logging = previous_; }
}  // namespace sic::support::admission
