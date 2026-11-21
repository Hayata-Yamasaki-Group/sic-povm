// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "access/source_arguments.hpp"
#include "sic/support/cli.hpp"
#include "transport/fetch.hpp"
#include "transport/repair.hpp"
namespace sic::apps::container {
enum class Mode {
  read_range,
  split,
  fetch,
  repair,
  from_text,
  merge_parts,
  merge_element_parts,
  reencode
};
struct RangeInput {
  sic::access::SourceArguments input;
  const std::string& path;
  explicit RangeInput(sic::support::cli::Arguments& arguments);
};
struct ReencodeOptions {
  const std::string& input;
  const std::string& output;
  unsigned jobs;
};
class Invocation {
  sic::support::cli::Arguments arguments_;

 public:
  Mode mode;
  Invocation(Mode selected, sic::support::cli::Arguments arguments);
  sic::transport::SplitOptions split_options();
  sic::transport::FetchOptions fetch_options();
  sic::transport::TransferJob repair_job();
  const std::vector<std::string>& from_text_paths() const;
  const std::vector<std::string>& merge_paths() const;
  RangeInput range_input();
  sic::support::ByteRange range() const;
  ReencodeOptions reencode_options() const;
  sic::sicx::VerificationPolicy reencode_policy() const;
};
const sic::support::cli::Parser& command_line_parser(std::string_view command = {});
Invocation parse_command_line(int argc, char** argv);
}  // namespace sic::apps::container
