// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "access/source_arguments.hpp"
#include "sic/sicx/verify.hpp"
namespace sic::apps::verify {
class Invocation {
  sic::support::cli::Arguments arguments_;

 public:
  sic::access::SourceArguments input;
  const std::string& path;
  unsigned threads;
  bool quick, sic_check, rescaling;
  explicit Invocation(sic::support::cli::Arguments arguments);
  Invocation(const Invocation&) = delete;
  sic::sicx::VerificationPolicy verification_policy() const;
};
const sic::support::cli::Parser& command_line_parser();
Invocation parse_command_line(int argc, char** argv);
}  // namespace sic::apps::verify
