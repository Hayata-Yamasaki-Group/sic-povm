// SPDX-License-Identifier: AGPL-3.0-or-later
#include <functional>
#include <sstream>

#include "sic/support/cli.hpp"

void check(bool ok) {
  if (!ok) throw std::runtime_error("CLI parser test failed");
}
void refused(const std::function<void()>& action) {
  try {
    action();
  } catch (const sic::support::cli::UsageError&) {
    return;
  }
  throw std::runtime_error("CLI parser accepted invalid input");
}
int main() {
  const sic::support::cli::Parser parser{"test FILE [OPTIONS]",
                                         {{"--flag", "", "flag"},
                                          {"--value", "N", "value", 1, 1},
                                          {"--ray", "A B [C]", "ray", 2, 3, true}}};
  // Exercise the actual argv API without a shell or a filesystem dependency.
  auto parse = [&](std::vector<std::string> text) {
    std::vector<char*> argv;
    for (auto& word : text) argv.push_back(word.data());
    return parser.parse(static_cast<int>(argv.size()), argv.data());
  };
  const auto args =
      parse({"test", "FILE", "--value", "-2", "--flag", "--ray", "a", "b", "c", "--ray", "d", "e"});
  check(args.pos == std::vector<std::string>{"FILE"});
  check(args.has("--flag") && args.get("--value") == "-2" && args.all("--ray").size() == 2);
  check(args.numeric<int>("--value", 0, -3, 3) == -2);
  check(parse({"test", "--", "--help", "-file"}).pos.size() == 2);
  refused([&] { parse({"test", "--unknown"}); });
  refused([&] { parse({"test", "-h"}); });
  refused([&] { parse({"test", "--flag", "--flag"}); });
  refused([&] { parse({"test", "--value"}); });
  refused([&] { parse({"test", "--ray", "a"}); });
  refused([&] { args.positionals(2, 2); });
  refused([&] { args.only({"--value"}); });
  try {
    parse({"test", "--help"});
    throw std::runtime_error("help not handled");
  } catch (const sic::support::cli::HelpRequested&) {
  }
  check(sic::support::cli::number<unsigned>("4294967295", "N") == 4294967295u);
  for (const auto* value : {"", "-1", "+1", " 1", "1 ", "1x", "4294967296"})
    refused([&] { (void)sic::support::cli::number<unsigned>(value, "N"); });
  for (const auto* value : {"nan", "inf", "1e9999", "1x", "-1"})
    refused([&] { (void)sic::support::cli::number<double>(value, "T"); });
  refused([&] { (void)sic::support::cli::number<unsigned>(std::string_view{}, "N"); });
  try {
    (void)sic::support::cli::number<int>("4", "N", -2, 3);
    check(false);
  } catch (const sic::support::cli::UsageError& error) {
    check(std::string(error.what()).find("[-2, 3]") != std::string::npos);
  }
  check(sic::support::cli::number<double>("1e-10", "T") == 1e-10);
  check(args.numeric<int>("--absent", -1, 0, 10) == -1);  // internal default sentinel
  // Classification must survive the application boundary without changing exits.
  for (const auto kind :
       {sic::ErrorKind::construction, sic::ErrorKind::precision, sic::ErrorKind::resource,
        sic::ErrorKind::invalid_argument, sic::ErrorKind::invalid_format, sic::ErrorKind::io}) {
    const sic::Error error(kind, "refused");
    std::ostringstream diagnostic;
    auto* saved = std::cerr.rdbuf(diagnostic.rdbuf());
    const int status = sic::support::cli::report(parser, error, "test");
    std::cerr.rdbuf(saved);
    check(error.kind() == kind && status == 1);
    check(diagnostic.str() == "test: [" + std::string(sic::error_kind_name(kind)) + "] refused\n");
  }
  std::ostringstream diagnostic;
  auto* saved = std::cerr.rdbuf(diagnostic.rdbuf());
  const int status =
      sic::support::cli::report(parser, sic::support::cli::UsageError("bad usage"), "test");
  std::cerr.rdbuf(saved);
  check(status == 2 && diagnostic.str().find("usage: ") != std::string::npos);
}
