// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdlib>
#include <functional>
#include <iostream>

#include "access/http_transfer.hpp"
#include "data/catalogue.hpp"
#include "sic/decode/placement.hpp"
#include "sic/sicx/json.hpp"
#include "sic/support/parse_number.hpp"
#include "sic/transport/parts.hpp"
#include "sicx/io/io.hpp"
#include "transport/parts.hpp"
void check(bool ok) {
  if (!ok) throw std::runtime_error("record parsing test failed");
}
void refused(const std::function<void()>& f, sic::ErrorKind kind) {
  try {
    f();
  } catch (const sic::Error& e) {
    check(e.kind() == kind);
    return;
  }
  throw std::runtime_error("invalid record accepted");
}
int main() try {
  using sic::ErrorKind;
  refused([] { sic::sicx::double_decimal17(std::numeric_limits<double>::infinity()); },
          ErrorKind::invalid_format);
  check(sic::data::numeric_suffix("wrong", 'd') == UINT64_MAX);
  check(sic::data::numeric_suffix("d1x", 'd') == UINT64_MAX);
  check(sic::data::numeric_suffix("d12", 'd') == 12);
  refused([] { sic::data::numeric_suffix("d18446744073709551616", 'd'); },
          ErrorKind::invalid_format);
  check(sic::decode::Placement::parse_list("0-2,4") == std::vector<int>({0, 1, 2, 4}));
  refused([] { sic::decode::Placement::parse_list(",2-1,,"); }, ErrorKind::invalid_format);
  check(sic::decode::Placement::parse_list("1023-1025,2147483647") == std::vector<int>({1023}));
  for (auto text : {"1x", "1-2x", "-1", "+1", " 1", "1 ", "999999999999999999999"})
    refused([&] { sic::decode::Placement::parse_list(text); }, ErrorKind::invalid_format);
  for (auto text : {"-1", "1x", "18446744073709551616"})
    refused([&] { sic::transport::parse_u64(text); }, ErrorKind::invalid_argument);
#ifdef SIC_ENABLE_TEST_HOOKS
  for (auto text : {"-1", "1x", "18446744073709551616"}) {
    setenv("SIC_TEST_PARSE_RECORD", text, 1);
    refused([] { sic::sicx::env_u64("SIC_TEST_PARSE_RECORD"); }, ErrorKind::invalid_argument);
  }
  setenv("SIC_TEST_PARSE_RECORD", "", 1);
  check(sic::sicx::env_u64("SIC_TEST_PARSE_RECORD") == 0);
  unsetenv("SIC_TEST_PARSE_RECORD");
  check(sic::sicx::env_u64("SIC_TEST_PARSE_RECORD") == 0);
  for (auto text : {"", "1x", "999999999999999999999"}) {
    setenv("SIC_TEST_ABORT_AFTER_PARTS", text, 1);
    refused([] { sic::transport::sync_parts(-1, {}, ".", 1, false, "."); },
            ErrorKind::invalid_argument);
  }
  unsetenv("SIC_TEST_ABORT_AFTER_PARTS");
#endif
#ifdef SIC_HAVE_CURL
  for (auto line : {"HTTP/1.1 206junk OK\r\n", "HTTP/1.1 -206 OK\r\n", "HTTP/1.1 +206 OK\r\n",
                    "HTTP/1.1  206 OK\r\n", "HTTP/1.1 99 OK\r\n", "HTTP/1.1 1000 OK\r\n",
                    "HTTP/1.1 999999999999999999999 OK\r\n"}) {
    sic::access::Transfer t;
    std::string text(line);
    check(sic::access::Transfer::header(text.data(), 1, text.size(), &t) == 0);
    check(bool(t.exception));
    refused([&] { std::rethrow_exception(t.exception); }, ErrorKind::invalid_format);
  }
  for (auto line : {"HTTP/1.1 206 Partial Content\r\n", "HTTP/2 206\r\n"}) {
    sic::access::Transfer t;
    std::string text(line);
    check(sic::access::Transfer::header(text.data(), 1, text.size(), &t) == text.size());
    check(t.status == 206 && !t.exception);
  }
#endif
  return 0;
} catch (const std::exception& e) {
  std::cerr << e.what() << '\n';
  return 1;
}
