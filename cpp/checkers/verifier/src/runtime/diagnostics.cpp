// SPDX-License-Identifier: AGPL-3.0-or-later
#include "diagnostics.hpp"

namespace sic_check::verifier {
[[noreturn]] void reject(const string& m) { throw Reject(m); }

void say(const char* f, ...) {
  va_list ap;
  va_start(ap, f);
  std::vprintf(f, ap);
  va_end(ap);
  std::fflush(stdout);
}
}  // namespace sic_check::verifier
