// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::norm {

struct Json {
  enum Kind { string, array, object } kind = string;
  std::string text;
  std::vector<Json> list;
  std::map<std::string, Json> fields;

  const Json& at(const std::string& key) const;

  const std::string& str() const;

  const std::vector<Json>& items() const;

  void keys(std::initializer_list<std::string> wanted) const;
};

class JsonParser {
  const std::string& data;
  size_t pos = 0, nodes = 0;

  char take();

  std::string quoted();

  Json value(unsigned depth);

 public:
  explicit JsonParser(const std::string& s);

  Json parse();
};

int64_t number(const Json& j, int64_t low = 0, int64_t high = INT64_C(999999999999999999));
}  // namespace sic_check::norm
