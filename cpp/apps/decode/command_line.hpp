// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>

#include "access/source_arguments.hpp"
#include "sic/decode/decoder.hpp"
#include "sic/sicx/finalize.hpp"
#include "support/resources/numa_policy.hpp"
#pragma once
#include "sic/support/cli.hpp"
namespace sic::apps::decode {
sic::support::resources::NumaPolicy parse_memory_policy(const std::string& spec);
struct CoordinateInterval {
  uint64_t first, last;
};
class Invocation {
  sic::support::cli::Arguments arguments_;
  sic::support::admission::LogScope admission_log_;
  sic::support::ScopedStopFile stop_scope_;

 public:
  sic::access::SourceArguments input;
  std::chrono::steady_clock::time_point start;
  std::filesystem::path file, outdir;
  sic::decode::Decoder::Options options;
  std::string rays_arg, coords_arg;
  bool finalize, resume, check_budget;
  uint64_t working_bits;
  std::string tower;
  std::vector<CoordinateInterval> intervals;
  std::vector<sic::sicx::FinalizeRay> given;
  explicit Invocation(sic::support::cli::Arguments arguments);
  Invocation(const Invocation&) = delete;
};
const sic::support::cli::Parser& command_line_parser();
Invocation parse_command_line(int argc, char** argv);
}  // namespace sic::apps::decode
