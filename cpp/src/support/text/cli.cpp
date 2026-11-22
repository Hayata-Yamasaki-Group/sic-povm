// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/cli.hpp"

#include <csignal>
#include <cstdio>

namespace sic::support::cli {
std::string Arguments::get(const std::string& name, const std::string& fallback) const {
  const auto it = options.find(name);
  return it == options.end() ? fallback : it->second.back().at(0);
}
}  // namespace sic::support::cli

namespace sic::support::cli {
const std::vector<Arguments::Values>& Arguments::all(const std::string& name) const {
  static const std::vector<Values> empty;
  const auto it = options.find(name);
  return it == options.end() ? empty : it->second;
}
}  // namespace sic::support::cli

namespace sic::support::cli {
void Arguments::positionals(std::size_t minimum, std::size_t maximum) const {
  require(pos.size() >= minimum && pos.size() <= maximum, "wrong number of positional arguments");
}
}  // namespace sic::support::cli

namespace sic::support::cli {
void Arguments::only(std::initializer_list<std::string_view> allowed) const {
  for (const auto& [name, unused] : options)
    require(std::find(allowed.begin(), allowed.end(), name) != allowed.end(),
            "option not valid in this mode: " + name);
}
}  // namespace sic::support::cli

namespace sic::support::cli {
Parser::Parser(std::string usage, std::initializer_list<Option> options, bool supports_version)
    : usage_(std::move(usage)), supports_version_(supports_version), options_(options) {}
}  // namespace sic::support::cli

namespace sic::support::cli {
std::string Parser::help() const {
  std::string out = "usage: " + usage_ + "\n\nOptions (available in the forms listed above):\n";
  for (const auto& option : options_)
    out += "  " + option.name + (option.values.empty() ? "" : " " + option.values) + "  " +
           option.description + "\n";
  if (supports_version_) out += "  --version  Print tool identity as canonical JSON and exit 0.\n";
  return out +
         "  --help  Print this "
         "help to stdout and exit 0.\n\nExit codes: 0 success; 1 failed "
         "check or runtime/input error; 2 usage error.\n";
}
}  // namespace sic::support::cli

namespace sic::support::cli {
Parser Parser::with(std::initializer_list<Option> options) const {
  Parser result = *this;
  for (const auto& option : options)
    if (std::none_of(result.options_.begin(), result.options_.end(),
                     [&](const Option& present) { return present.name == option.name; }))
      result.options_.push_back(option);
  return result;
}
}  // namespace sic::support::cli

namespace sic::support::cli {
Parser Parser::select(std::initializer_list<std::string_view> names) const {
  Parser result{usage_, {}, supports_version_};
  for (const auto& option : options_)
    if (std::find(names.begin(), names.end(), option.name) != names.end())
      result.options_.push_back(option);
  return result;
}
}  // namespace sic::support::cli

namespace sic::support::cli {
Arguments Parser::parse(int argc, char** argv, int first) const {
  Arguments out;
  bool positional_only = false;
  for (int i = first; i < argc; ++i) {
    const std::string token = argv[i];
    if (!positional_only && token == "--") {
      positional_only = true;
      continue;
    }
    if (!positional_only && token == "--help") throw HelpRequested{};
    const Option* spec = nullptr;
    if (!positional_only)
      for (const auto& option : options_)
        if (option.name == token) {
          spec = &option;
          break;
        }
    if (spec) {
      require(spec->repeat || !out.has(token), "duplicate option " + token);
      Arguments::Values values;
      while (values.size() < spec->maximum && i + 1 < argc) {
        const std::string next = argv[i + 1];
        if (next.rfind("--", 0) == 0) break;
        values.push_back(argv[++i]);
      }
      require(values.size() >= spec->minimum, "missing value for " + token + " " + spec->values);
      out.options[token].push_back(std::move(values));
    } else {
      require(positional_only || token == "-" || token.empty() || token[0] != '-',
              "unknown option " + token);
      out.pos.push_back(token);
    }
  }
  return out;
}
}  // namespace sic::support::cli

namespace sic::support::cli {
namespace {
bool flush_stdout() {
  // CLI commands use both C stdio and C++ streams and may disable their synchronization.
  std::cout.flush();
  return std::fflush(stdout) == 0 && !std::ferror(stdout) && std::cout.good();
}

int stdout_error(std::string_view tool) {
  std::cerr << tool << ": [" << sic::error_kind_name(ErrorKind::io) << "] stdout write failed\n";
  return kFailure;
}
}  // namespace

void prepare_stdout() { std::signal(SIGPIPE, SIG_IGN); }

int finish_stdout(int status, std::string_view tool) {
  return flush_stdout() ? status : stdout_error(tool);
}

int report(const Parser& parser, const std::exception& error, std::string_view tool) {
  if (dynamic_cast<const HelpRequested*>(&error)) {
    std::cout << parser.help();
    return finish_stdout(kSuccess, tool);
  }
  const bool stdout_ok = flush_stdout();
  std::cerr << tool << ": ";
  if (const auto* classified = dynamic_cast<const sic::Error*>(&error))
    std::cerr << '[' << sic::error_kind_name(classified->kind()) << "] ";
  std::cerr << error.what() << '\n';
  const bool usage = dynamic_cast<const UsageError*>(&error) != nullptr;
  if (usage) std::cerr << parser.help();
  if (!stdout_ok) stdout_error(tool);
  return usage ? kUsageError : kFailure;
}
}  // namespace sic::support::cli

namespace sic::support::cli {
void require(bool condition, const std::string& message) {
  if (!condition) throw UsageError(message);
}
}  // namespace sic::support::cli
