// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "sic/sicx/json.hpp"

namespace sic::data {
// Internal catalogue builders borrow paths for the call and return owned JSON.
// file_entry hashes the complete file; read_canonical reads the complete text
// and applies the canonical SICX JSON parser after removing trailing newlines.
sic::support::Json file_entry(const std::filesystem::path& path, const std::filesystem::path& root);
sic::support::Json read_canonical(const std::filesystem::path& path);
// Nonmatching names return UINT64_MAX; decimal overflow throws sic::Error with
// invalid_format. This sentinel is private to catalogue directory traversal.
uint64_t numeric_suffix(const std::string& name, char prefix);
}  // namespace sic::data
