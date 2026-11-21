// SPDX-License-Identifier: AGPL-3.0-or-later
#include <exception>
#include <sstream>
#include <string>

#include "sic/sicx/format.hpp"
#include "sic/sicx/text.hpp"

namespace {
struct FailingBuffer : std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize) override {
    throw std::runtime_error("stream buffer write failed");
  }
};
struct CauseBuffer : std::streambuf {
  std::exception_ptr cause;
  explicit CauseBuffer(std::exception_ptr value) : cause(std::move(value)) {}
  std::streamsize xsputn(const char*, std::streamsize) override { std::rethrow_exception(cause); }
  int sync() override { std::rethrow_exception(cause); }
};
struct CallerFailure : sic::Error {
  CallerFailure() : sic::Error(sic::ErrorKind::integrity, "caller failure") {}
};
}  // namespace

int main() {
  using sic::ErrorKind;
  for (auto kind : {ErrorKind::invalid_argument, ErrorKind::invalid_format, ErrorKind::integrity,
                    ErrorKind::io, ErrorKind::resource, ErrorKind::precision, ErrorKind::cancelled,
                    ErrorKind::unsupported, ErrorKind::construction}) {
    bool caught = false;
    try {
      sic::sicx::fail(kind, "detail");
    } catch (const sic::Error& e) {
      caught = e.kind() == kind && std::string(e.what()) == "SICX: detail";
    }
    if (!caught) return 1;
  }
  bool caught = false;
  // The original category/subtype and native overflow survive a bad stream.
  for (bool overflow : {false, true}) {
    CauseBuffer buffer(overflow ? std::make_exception_ptr(std::overflow_error("native overflow"))
                                : std::make_exception_ptr(CallerFailure()));
    std::ostream output(&buffer);
    output.exceptions(std::ios::badbit);
    sic::sicx::StreamSink sink(output);
    caught = false;
    try {
      sink.line("write fixture");
    } catch (const CallerFailure& error) {
      caught = !overflow && error.kind() == ErrorKind::integrity &&
               std::string(error.what()) == "caller failure";
    } catch (const std::overflow_error& error) {
      caught = overflow && std::string(error.what()) == "native overflow";
    }
    if (!caught || !output.bad()) return 1;
  }
  // A tied stream fails during sentry construction, before the target is bad.
  CauseBuffer tied_buffer(std::make_exception_ptr(std::runtime_error("tied stream cause")));
  std::ostream tied(&tied_buffer);
  tied.exceptions(std::ios::badbit);
  std::ostringstream good;
  good.tie(&tied);
  sic::sicx::StreamSink good_sink(good);
  caught = false;
  try {
    good_sink.line("write fixture");
  } catch (const sic::Error&) {
    return 1;
  } catch (const std::runtime_error& error) {
    caught = std::string(error.what()) == "tied stream cause";
  }
  if (!caught || !good.good()) return 1;
  for (bool throwing : {false, true}) {
    for (auto state : {std::ios::badbit, std::ios::failbit}) {
      std::ostringstream output;
      output.setstate(state);
      if (throwing) {
        try {
          output.exceptions(state);
        } catch (const std::ios_base::failure&) {
        }
      }
      sic::sicx::StreamSink sink(output);
      caught = false;
      try {
        sink.line("write fixture");
      } catch (const sic::Error& error) {
        caught = error.kind() == ErrorKind::io;
      }
      if (!caught) return 1;
    }
    FailingBuffer buffer;
    std::ostream stream(&buffer);
    if (throwing) stream.exceptions(std::ios::badbit);
    sic::sicx::StreamSink failing_sink(stream);
    caught = false;
    try {
      failing_sink.line("write fixture");
    } catch (const sic::Error& error) {
      caught = error.kind() == ErrorKind::io;
    }
    if (!caught) return 1;
  }
  sic::sicx::Meta meta;
  meta.d = 4;
  auto expect_entry_error = [&](uint64_t coordinate, const std::string& message) {
    try {
      (void)meta.entry(coordinate);
    } catch (const sic::Error& e) {
      return e.kind() == ErrorKind::invalid_argument && std::string(e.what()) == "SICX: " + message;
    }
    return false;
  };
  if (!expect_entry_error(0, "entry unit table has not been derived")) return 1;
  auto units = std::make_shared<sic::sicx::UnitGroup>(4);
  meta.units = units;
  if (!expect_entry_error(4, "coordinate out of range")) return 1;
  units->logarithm.pop_back();
  if (!expect_entry_error(1, "entry unit table has not been derived")) return 1;
  units->logarithm.resize(4);
  units->logarithm[1] = sic::sicx::UnitGroup::kNonunit;
  if (!expect_entry_error(1, "unit logarithm unavailable")) return 1;
  meta.d = 1;
  units->d = 1;
  units->logarithm.resize(1);
  if (!expect_entry_error(0, "canonical unit lift unavailable")) return 1;
}
