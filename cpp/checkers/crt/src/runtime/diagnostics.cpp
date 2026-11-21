// SPDX-License-Identifier: AGPL-3.0-or-later
#include "diagnostics.hpp"

namespace sic_check::crt::sicx {

[[noreturn]] void resource(const string& m) { throw Refuse("REFUSED: SICX input: " + m); }

[[noreturn]] void reject(const string& m) { throw Reject("SICX input: " + m); }

double now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double rss_gb() {
  struct rusage u;
  getrusage(RUSAGE_SELF, &u);
  return u.ru_maxrss / 1048576.0;
}

[[noreturn]] void refuse(const string& why) { throw std::runtime_error("REFUSED: " + why); }

void say(const char* f, ...) {
  va_list ap;
  va_start(ap, f);
  std::vprintf(f, ap);
  va_end(ap);
  std::fflush(stdout);
}

std::atomic<Uint64> g_reach[64];
}  // namespace sic_check::crt::sicx
