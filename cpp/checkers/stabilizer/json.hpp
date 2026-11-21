// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Checker-local JSON reader. No production or peer checker code is used.
#include <map>
#include <string>
#include <vector>

namespace sic_check::stabilizer {
struct Json {
  enum class Kind { string, array, object } kind = Kind::object;
  std::string text;
  std::vector<Json> items;
  std::map<std::string, Json> members;
  const Json& at(const std::string& key) const;
  const std::string& string() const;
  const std::vector<Json>& array() const;
};
Json read_json(const std::string& path);
}  // namespace sic_check::stabilizer
