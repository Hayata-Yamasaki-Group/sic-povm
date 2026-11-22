// SPDX-License-Identifier: AGPL-3.0-or-later
#include <flint/flint.h>

#include "data/commands.hpp"
#include "sic/support/tool_version.hpp"

namespace {
using sic::support::cli::default_text;
using sic::support::cli::Parser;
const Parser kParser{
    "sic-data COMMAND [ARGS]\n       COMMAND: formats, write, check, bundle, check-bundle, "
    "layout, catalogue, list\n       sic-data COMMAND --help shows positional grammar and options"};
const std::map<std::string_view, Parser> kCommands{
    {"formats", Parser{"sic-data formats"}},
    {"write", Parser{"sic-data write RAY.json OUTDIR [--formats LIST]",
                     {{"--formats", "LIST",
                       "Write npy,mat,csv,h5 (default " +
                           default_text(sic::data::kDefaultNumericFormats) + ").",
                       1, 1}}}},
    {"check", Parser{"sic-data check RAY.json FILE..."}},
    {"bundle", Parser{"sic-data bundle OUT.npz D [RAY.json...] [--exceptional RAY.json GROUP]...\n"
                      "       At least one regular or exceptional ray is required.",
                      {{"--exceptional", "RAY.json GROUP", "Exceptional ray and covariance group.",
                        2, 2, true}}}},
    {"check-bundle",
     Parser{"sic-data check-bundle FILE.npz [RAY.json...] [--exceptional RAY.json]...\n"
            "       At least one regular or exceptional ray is required.",
            {{"--exceptional", "RAY.json", "Exceptional ray.", 1, 1, true}}}},
    {"layout", Parser{"sic-data layout INPUT.sicx ROOT --multiplet N [--threads T]",
                      {{"--multiplet", "N", "Positive multiplet number (required).", 1, 1},
                       {"--threads", "T",
                        "Worker count (default " + default_text(sic::data::kDefaultLayoutThreads) +
                            "; 1.." + default_text(sic::data::kMaximumLayoutThreads) + ").",
                        1, 1}}}},
    {"catalogue", Parser{"sic-data catalogue ROOT"}},
    {"list", Parser{"sic-data list CATALOGUE.json"}},
};
}  // namespace
int main(int argc, char** argv) {
  sic::support::cli::prepare_stdout();
  if (const auto status = sic::support::cli::version_request(argc, argv, "sic-data"))
    return *status;
  const std::string_view command = argc > 1 ? argv[1] : "";
  const auto selected = kCommands.find(command);
  const auto& usage = selected == kCommands.end() ? kParser : selected->second;
  try {
    flint_set_num_threads(1);
    if (command == "--help") throw sic::support::cli::HelpRequested{};
    sic::support::cli::require(selected != kCommands.end(), "expected a sic-data command");
    const int status = sic::data::run(std::string(command), usage.parse(argc, argv, 2));
    return sic::support::cli::finish_stdout(status, "sic-data");
  } catch (const std::exception& e) {
    return sic::support::cli::report(usage, e, "sic-data");
  }
}
