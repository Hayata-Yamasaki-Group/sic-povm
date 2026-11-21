// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdexcept>
#include <string>
namespace sic {
enum class ErrorKind {
  invalid_argument,
  invalid_format,
  integrity,
  io,
  resource,
  precision,
  cancelled,
  unsupported,
  construction
};
const char* error_kind_name(ErrorKind kind) noexcept;
// Stable category for programmatic handling; what() is diagnostic, not a protocol.
class Error : public std::runtime_error {
 public:
  Error(ErrorKind kind, const std::string& message);
  ErrorKind kind() const noexcept { return kind_; }

 private:
  ErrorKind kind_;
};
}  // namespace sic
