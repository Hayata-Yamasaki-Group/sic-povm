// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/parse_number.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <sstream>

#include "sic/core/interchange.hpp"
#include "sic/data/hoggar.hpp"
#include "sic/support/cli.hpp"
#include "sic/support/json.hpp"

using sic::ErrorKind;
using sic::support::parse_number;
void check(bool ok) {
  if (!ok) throw std::runtime_error("numeric parsing test failed");
}
void refused(const std::function<void()>& f, ErrorKind kind) {
  try {
    f();
  } catch (const sic::Error& e) {
    check(e.kind() == kind);
    return;
  }
  throw std::runtime_error("invalid number accepted");
}
int main() try {
  for (auto kind : {ErrorKind::invalid_argument, ErrorKind::invalid_format}) {
    check(parse_number<uint64_t>("18446744073709551615", "u64", kind) == UINT64_MAX);
    check(parse_number<int64_t>("-9223372036854775808", "i64", kind) == INT64_MIN);
    check(parse_number<int64_t>("9223372036854775807", "i64", kind) == INT64_MAX);
    check(parse_number<unsigned>("0", "zero", kind) == 0);
    check(parse_number<unsigned>("ff", "hex", kind, 0, 255, 16) == 255);
    check(parse_number<int>("-2", "bounded", kind, -2, 2) == -2);
    check(parse_number<int>("2", "bounded", kind, -2, 2) == 2);
    for (auto text : {"", "-1", "+1", " 1", "1 ", "1\n", "1x", "x1", "18446744073709551616"})
      refused([&] { parse_number<uint64_t>(text, "u64", kind); }, kind);
    for (auto text : {"9223372036854775808", "-9223372036854775809", "--1", "+1"})
      refused([&] { parse_number<int64_t>(text, "i64", kind); }, kind);
    refused([&] { parse_number<int>("3", "bounded", kind, -2, 2); }, kind);
    refused([&] { parse_number<int>("-3", "bounded", kind, -2, 2); }, kind);
    refused([&] { parse_number<int>("1", "bounds", kind, 2, 1); }, kind);
    refused([&] { parse_number<unsigned>("1", "base", kind, 0, 10, 1); }, kind);
    refused([&] { parse_number<unsigned>("256", "u8", kind, 0, 255); }, kind);
    refused([&] { parse_number<uint8_t>("256", "u8", kind); }, kind);
    refused([&] { parse_number<int>(std::string_view("1\0x", 3), "nul", kind); }, kind);
    check(parse_number<double>("-1.25e+2", "double", kind) == -125);
    check(parse_number<double>("4.9406564584124654e-324", "subnormal", kind) ==
          std::numeric_limits<double>::denorm_min());
    for (auto text : {"", "nan", "inf", "1e9999", "1e-9999", "+1", " 1", "1 ", "1x", "0x1p0"})
      refused([&] { parse_number<double>(text, "double", kind); }, kind);
  }
  // The installed CLI facade retains its usage type and exit status.
  try {
    sic::support::cli::number<unsigned>("1x", "N");
    check(false);
  } catch (const sic::support::cli::UsageError& e) {
    check(e.kind() == ErrorKind::invalid_argument);
    std::ostringstream sink;
    auto* old = std::cerr.rdbuf(sink.rdbuf());
    const int status = sic::support::cli::report(sic::support::cli::Parser("test"), e, "test");
    std::cerr.rdbuf(old);
    check(status == 2);
  }
  // JSON overflow keeps the JSON exception subtype as well as its category.
  try {
    sic::support::Json::string_value("9223372036854775808").as_int();
    check(false);
  } catch (const sic::support::JsonError& e) {
    check(e.kind() == ErrorKind::invalid_format);
  }
  // MAT anchors reject junk, negative wrapping, and overflow of the 1-based index.
  for (auto text : {"1x", "-1", "+1", " 1", "18446744073709551615", "18446744073709551616"})
    refused(
        [&] {
          sic::core::interchange::fiducial_mat({{1, 0}}, "id", "", "", "", text);
        },
        ErrorKind::invalid_argument);
  // Decimal limbs require the complete numeric token.
  for (auto text : {"0.1x", "0.-1", "0.+1", "0. 1"})
    refused([&] { sic::data::hoggar::hp_contains_sqrt_ratio(text, "0.00", 1, 2); },
            ErrorKind::invalid_format);
  return 0;
} catch (const std::exception& e) {
  std::cerr << e.what() << '\n';
  return 1;
}
