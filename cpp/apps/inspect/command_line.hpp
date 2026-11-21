// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/data/layout.hpp"
#include "sic/sicx/exact.hpp"
#include "sic/support/cli.hpp"
namespace sic::apps::inspect {
enum class Mode {
  text_json,
  json_rays,
  json_all,
  exact_json,
  block,
  entry,
  release_layout,
  catalogue,
  info
};
struct TextOptions {
  uint64_t part_bytes;
  unsigned threads;
};
struct ExactOptions {
  std::string in_path, out_path;
  sic::sicx::exact::BuildOptions options;
};
class Invocation {
  sic::support::cli::Arguments arguments_;

 public:
  Mode mode;
  std::string file;
  explicit Invocation(sic::support::cli::Arguments arguments);
  TextOptions text_options() const;
  std::string json_multiplet() const;
  ExactOptions exact_options() const;
  size_t chunk_index() const;
  sic::data::ReleaseOptions release_options() const;
  std::filesystem::path catalogue_root() const;
  void validate_info() const;
  void validate_chunk_index(size_t index, size_t count) const;
  std::string text_output() const;
  std::string all_json_output() const;
  std::string rays_directory() const;
  std::string release_root() const;
};
const sic::support::cli::Parser& command_line_parser();
Invocation parse_command_line(int argc, char** argv);
}  // namespace sic::apps::inspect
