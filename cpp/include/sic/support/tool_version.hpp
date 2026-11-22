// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <optional>
#include <string_view>
namespace sic::support::cli {
inline constexpr std::string_view kToolVersionNumber = "1.0.0";
std::optional<int> version_request(int argc, char** argv, std::string_view name);
}  // namespace sic::support::cli
