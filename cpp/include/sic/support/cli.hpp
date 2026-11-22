// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "sic/error.hpp"
#include "sic/support/parse_number.hpp"

namespace sic::support::cli {

inline constexpr int kSuccess = 0, kFailure = 1, kUsageError = 2;
struct UsageError : sic::Error {
  explicit UsageError(const std::string& message)
      : sic::Error(ErrorKind::invalid_argument, message) {}
};
struct HelpRequested : std::exception {};
void require(bool condition, const std::string& message);

// Use the parser's fallback value verbatim, including round-trippable floating defaults.
template <class T>
requires std::is_arithmetic_v<T> std::string default_text(T value) {
  char buffer[128];
  const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value);
  if (result.ec != std::errc{}) throw std::logic_error("cannot format CLI default");
  return std::string(buffer, result.ptr);
}
inline std::string default_text(std::string_view value) { return std::string(value); }

// All characters must participate in the conversion; signs on unsigned values,
// whitespace, overflow, NaN and infinity are refused. Bounds are inclusive.
template <class T>
T number(std::string_view text, std::string_view name, T low = 0,
         T high = std::numeric_limits<T>::max()) {
  try {
    return support::parse_number<T>(text, name, ErrorKind::invalid_argument, low, high);
  } catch (const sic::Error& error) {
    std::ostringstream message;
    message << error.what() << " (accepted range [" << +low << ", " << +high << "])";
    throw UsageError(message.str());
  }
}

struct Option {
  std::string name, values, description;
  std::size_t minimum = 0, maximum = 0;
  bool repeat = false;
};
struct Arguments {
  using Values = std::vector<std::string>;
  std::vector<std::string> pos;
  std::map<std::string, std::vector<Values>> options;
  bool has(const std::string& name) const { return options.contains(name); }
  std::string get(const std::string& name, const std::string& fallback = "") const;
  const std::vector<Values>& all(const std::string& name) const;
  void positionals(std::size_t minimum, std::size_t maximum) const;
  void only(std::initializer_list<std::string_view> allowed) const;
  template <class T>
  T numeric(const std::string& name, T fallback, T low = 0,
            T high = std::numeric_limits<T>::max()) const {
    return has(name) ? number<T>(get(name), name, low, high) : fallback;
  }
};

class Parser {
 public:
  Parser(std::string usage, std::initializer_list<Option> options = {},
         bool supports_version = true);
  std::string help() const;
  // Compose option groups; a mode's existing spelling/description takes precedence.
  Parser with(std::initializer_list<Option> options) const;
  Parser select(std::initializer_list<std::string_view> names) const;
  Arguments parse(int argc, char** argv, int first = 1) const;

 private:
  std::string usage_;
  bool supports_version_;
  std::vector<Option> options_;
};

// Let CLI writes to a closed pipe report EPIPE.
void prepare_stdout();
// Complete buffered CLI output before returning a process exit status.
int finish_stdout(int status, std::string_view tool);
int report(const Parser& parser, const std::exception& error, std::string_view tool);

}  // namespace sic::support::cli
