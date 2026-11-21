// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/json.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

#include "sic/support/durable_file.hpp"

static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
template <class F>
static void refuses(F&& action) {
  bool threw = false;
  try {
    action();
  } catch (const std::exception&) {
    threw = true;
  }
  check(threw, "expected failure");
}

int main() try {
  namespace support = sic::support;
  const std::string canonical =
      "{\"a\":[\"0\",\"-12\"],\"utf8\":\"\xc3\xa9\",\"z\":\"\\u0000\\\"\\\\\"}";
  check(support::dump_json(support::JsonParser::parse(canonical)) == canonical,
        "canonical JSON round trip");
  auto json = support::Json::object_value();
  json["z"] = support::Json::integer_value(12);
  json["a"] = support::Json::string_value("\n");
  check(support::dump_json(json) == "{\"a\":\"\\u000a\",\"z\":\"12\"}", "canonical JSON bytes");
  for (const auto* bad : {"{\"b\":\"0\",\"a\":\"1\"}", "{\"a\":\"0\",\"a\":\"1\"}",
                          "{ \"a\":\"0\"}", "{\"a\":0}", "\"\\n\"", "\"\\u0041\"", "{}\n"})
    refuses([&] { support::JsonParser::parse(bad); });
  refuses([] { support::Json::string_value("-0").as_int(); });
  refuses([] { support::Json::string_value("01").as_int(); });
  refuses([] { support::Json::integer_value(-1).as_uint(); });

  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
