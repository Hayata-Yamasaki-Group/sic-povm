// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/error.hpp"
namespace sic {
const char* error_kind_name(ErrorKind kind) noexcept {
  switch (kind) {
    case ErrorKind::invalid_argument:
      return "invalid_argument";
    case ErrorKind::invalid_format:
      return "invalid_format";
    case ErrorKind::integrity:
      return "integrity";
    case ErrorKind::io:
      return "io";
    case ErrorKind::resource:
      return "resource";
    case ErrorKind::precision:
      return "precision";
    case ErrorKind::cancelled:
      return "cancelled";
    case ErrorKind::unsupported:
      return "unsupported";
    case ErrorKind::construction:
      return "construction";
  }
  return "unknown";
}
Error::Error(ErrorKind kind, const std::string& message)
    : std::runtime_error(message), kind_(kind) {}
}  // namespace sic
