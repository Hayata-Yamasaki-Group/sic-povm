// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Release JSON catalogue entries; explicit TSV is supported for explicit test inputs.
#include <string>
#include <vector>
namespace sic::data {
struct CatalogueEntry {
  int d = 0;
  std::string label, order, status,
      csv;  // JSON: absolute CSV path; TSV: relative to the caller root
};

std::vector<CatalogueEntry> read_catalogue(const std::string& path);
}  // namespace sic::data
