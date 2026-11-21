// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Strict binary64 fiducial readers: CSV, NPY and dimension-prefixed text.
// CSV/NPY codecs are shared with the interchange writers. Catalogue paths are
// relative to the caller's repository/data root; no library environment reads.
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "sic/core/interchange.hpp"
#include "sic/limits.hpp"
namespace sic::core {
Vector read_vector(std::istream& in);
Vector read_vector(std::istream& in, const InputLimits& limits);
Vector read_vector_file(const std::string& path);
void write_vector(std::ostream& out, const Vector& v, int precision = 17);
}  // namespace sic::core
namespace sic::core::files {
bool ends_with(const std::string& s, const std::string& suffix);
Vector read_fiducial(const std::string& path);
}  // namespace sic::core::files
