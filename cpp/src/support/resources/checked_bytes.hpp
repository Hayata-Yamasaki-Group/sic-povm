// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>

#include "sic/error.hpp"
#include "sic/support/admission.hpp"
#include "support/resources/resource_units.hpp"
namespace sic::support::resources {
struct AdmissionError : sic::Error {
  explicit AdmissionError(const std::string& message);
};

using Bytes = ByteCount;
constexpr Bytes kMib = 1024 * 1024;
constexpr Bytes kGib = 1024 * kMib;

using sic::support::admission::add;
using sic::support::admission::mul;

std::uint64_t integer(std::string_view text, sic::ErrorKind kind = sic::ErrorKind::invalid_format);
Bytes megabytes(const char* text);
int thread_count(const char* text, const char* name);
}  // namespace sic::support::resources
