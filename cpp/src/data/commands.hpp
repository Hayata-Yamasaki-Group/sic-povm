#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/cli.hpp"
namespace sic::data {
inline constexpr const char* kDefaultNumericFormats = "npy,mat,csv";
inline constexpr unsigned kDefaultLayoutThreads = 1;
inline constexpr unsigned kMaximumLayoutThreads = 65536;
int run(const std::string& command, const support::cli::Arguments& args);
}  // namespace sic::data
