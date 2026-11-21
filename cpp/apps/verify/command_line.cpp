// SPDX-License-Identifier: AGPL-3.0-or-later
#include "command_line.hpp"

#include "access/source_arguments.hpp"
namespace sic::apps::verify {
namespace {
using sic::support::cli::default_text;
constexpr unsigned kDefaultThreads = 8;
}  // namespace

const sic::support::cli::Parser& command_line_parser() {
  static const sic::support::cli::Parser parser =
      sic::access::SourceArguments::parser(sic::support::cli::Parser{
          "sic-verify FILE [OPTIONS]",
          {
              {"--maximum-precision", "BITS",
               "Computational precision ceiling (default " +
                   default_text(sic::kDefaultMaximumPrecision) + "; " +
                   default_text(sic::kMinimumToolPrecision) + ".." +
                   default_text(sic::kNativeMaximumPrecision) + ").",
               1, 1, false},
              {"--threads", "T",
               "Positive worker count (default " + default_text(kDefaultThreads) + ").", 1, 1,
               false},
              {"--quick", "", "Verify footer, index, Merkle root and EXMT only.", 0, 0, false},
              {"--sic-check", "", "Add a binary64 SIC diagnostic.", 0, 0, false},
              {"--rescaling", "", "Check canonical 2-adic rescaling.", 0, 0, false},
          }});
  return parser;
}
Invocation parse_command_line(int argc, char** argv) {
  return Invocation(command_line_parser().parse(argc, argv));
}
Invocation::Invocation(sic::support::cli::Arguments arguments)
    : arguments_(std::move(arguments)),
      input(arguments_),
      path((arguments_.positionals(1, 1), arguments_.pos[0])),
      threads(arguments_.numeric<unsigned>("--threads", kDefaultThreads, 1)),
      quick(arguments_.has("--quick")),
      sic_check(arguments_.has("--sic-check")),
      rescaling(arguments_.has("--rescaling")) {}
sic::sicx::VerificationPolicy Invocation::verification_policy() const {
  sic::sicx::VerificationPolicy policy;
  policy.precision.maximum_bits =
      arguments_.numeric<long>("--maximum-precision", sic::kDefaultMaximumPrecision,
                               sic::kMinimumToolPrecision, sic::kNativeMaximumPrecision);

  return policy;
}
}  // namespace sic::apps::verify
