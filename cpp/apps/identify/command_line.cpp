// SPDX-License-Identifier: AGPL-3.0-or-later
#include "command_line.hpp"

#include <cmath>

#include "../data_root.hpp"
#include "search/stabilizer.hpp"
namespace sic::apps::identify {
namespace {
using sic::support::cli::default_text;
const sic::search::StabilizerOptions kDefaults;
constexpr int kDefaultMoments = 4;
constexpr int kMinimumMoments = 1;
constexpr int kMaximumMoments = 64;
}  // namespace

const sic::support::cli::Parser& command_line_parser() {
  static const sic::support::cli::Parser parser{
      "sic-identify FILE [OPTIONS]",
      {
          {"--catalogue", "JSON|TSV",
           "Catalogue file (default REPO/" + default_text(kDefaultCatalogue) + ").", 1, 1, false},
          {"--repo", "DIR",
           "Data root; search installed data, then current directory when omitted.", 1, 1, false},
          {"--tol", "T",
           "Positive finite match tolerance (default " + default_text(kDefaults.z_tol) + ").", 1, 1,
           false},
          {"--entry-error", "E",
           "Nonnegative finite entry error (default " + default_text(kDefaults.entry_error) + ").",
           1, 1, false},
          {"--moments", "K",
           "Moment count, " + default_text(kMinimumMoments) + ".." + default_text(kMaximumMoments) +
               " (default " + default_text(kDefaultMoments) + ").",
           1, 1, false},
          {"--unitary-only", "", "Search unitary elements only.", 0, 0, false},
      }};
  return parser;
}
Options parse_command_line(int argc, char** argv) {
  const auto args = command_line_parser().parse(argc, argv);

  args.positionals(1, 1);
  const std::string path = args.pos[0];
  const double z_tol = args.numeric<double>("--tol", kDefaults.z_tol);
  sic::support::cli::require(z_tol > 0, "--tol must be positive");
  const double entry_error = args.numeric<double>("--entry-error", kDefaults.entry_error),
               sic_tol = 1e-8;
  const int moments =
      args.numeric<int>("--moments", kDefaultMoments, kMinimumMoments, kMaximumMoments);
  const bool anti = !args.has("--unitary-only");
  const std::string root = sic::apps::data_root(args.get("--repo"));
  const std::string catalogue = args.get("--catalogue", root + "/" + kDefaultCatalogue);

  return {path, root, catalogue, z_tol, entry_error, sic_tol, moments, anti};
}
}  // namespace sic::apps::identify
