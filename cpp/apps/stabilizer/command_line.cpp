// SPDX-License-Identifier: AGPL-3.0-or-later
#include "command_line.hpp"

#include <cmath>

namespace sic::apps::stabilizer {
namespace {
using sic::support::cli::default_text;
const sic::search::StabilizerOptions kDefaults;
}  // namespace

const sic::support::cli::Parser& command_line_parser() {
  static const sic::support::cli::Parser parser{
      "sic-stabilizer FILE [OPTIONS] | sic-stabilizer --dim D [OPTIONS]",
      {
          {"--catalogue", "JSON|TSV", "Explicit ray catalogue; required with --dim.", 1, 1, false},
          {"--repo", "DIR", "Base directory for relative TSV paths (default current directory).", 1,
           1, false},
          {"--out", "FILE", "Output stabilizer file; write stdout when omitted.", 1, 1, false},
          {"--dim", "D", "Positive dimension in catalogue mode; required when FILE omitted.", 1, 1,
           false},
          {"--tol", "T",
           "Positive finite match tolerance (default " + default_text(kDefaults.z_tol) + ").", 1, 1,
           false},
          {"--entry-error", "E",
           "Nonnegative finite entry error (default " + default_text(kDefaults.entry_error) + ").",
           1, 1, false},
      }};
  return parser;
}
Options parse_command_line(int argc, char** argv) {
  const auto args = command_line_parser().parse(argc, argv);

  args.positionals(0, 1);
  const std::string path = args.pos.empty() ? "" : args.pos[0];
  const int dim = args.numeric<int>("--dim", 0, 1);
  sic::support::cli::require(path.empty() != (dim == 0), "specify FILE or --dim D");
  sic::search::StabilizerOptions o;
  o.z_tol = args.numeric<double>("--tol", kDefaults.z_tol);
  sic::support::cli::require(o.z_tol > 0, "--tol must be positive");
  o.entry_error = args.numeric<double>("--entry-error", kDefaults.entry_error);
  const std::string catalogue = args.get("--catalogue");
  sic::support::cli::require(!path.empty() || !catalogue.empty(),
                             "--catalogue is required with --dim");
  const std::string root = args.get("--repo");
  const std::string out_path = args.get("--out");

  return {path, root, out_path, catalogue, dim, o};
}
}  // namespace sic::apps::stabilizer
