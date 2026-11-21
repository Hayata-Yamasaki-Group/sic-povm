// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/resources/checked_bytes.hpp"

#include <flint/acb.h>
#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "sic/error.hpp"
#include "sic/support/admission.hpp"
#include "sic/support/kernel_settings.hpp"
#include "sic/support/parse_number.hpp"
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace sic::support::resources {
AdmissionError::AdmissionError(const std::string& message)
    : sic::Error(sic::ErrorKind::resource, message) {}

std::uint64_t integer(std::string_view text, sic::ErrorKind kind) {
  try {
    return sic::support::parse_number<std::uint64_t>(text, "resource value", kind);
  } catch (const sic::Error&) {
    throw sic::Error(kind, "invalid unsigned resource value: " + std::string(text));
  }
}

Bytes megabytes(const char* text) {
  char* end = nullptr;
  const double value = std::strtod(text, &end);
  const long double bytes = static_cast<long double>(value) * kMib;
  if (end == text || *end || !std::isfinite(value) || value <= 0 || bytes < 1 ||
      bytes >= static_cast<long double>(std::numeric_limits<Bytes>::max()))
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "SIC_MEMORY_MB must be positive, finite and representable");
  return static_cast<Bytes>(bytes);
}

int thread_count(const char* text, const char* name) {
  const Count value = integer(text, sic::ErrorKind::invalid_argument);
  if (!value || value > static_cast<Count>(std::numeric_limits<int>::max()))
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     std::string(name) + " must be a positive int");
  return static_cast<int>(value);
}
}  // namespace sic::support::resources
