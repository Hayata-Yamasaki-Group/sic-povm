// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/access/remote.hpp"
#include "sic/support/cli.hpp"
namespace sic::access {
// Shared CLI options are composed with each application's mode parser.
// Remote manifests occupy the normal FILE position after argument validation.
class SourceArguments {
 public:
  explicit SourceArguments(sic::support::cli::Arguments& args);
  static sic::support::cli::Parser parser(sic::support::cli::Parser base);
  bool remote() const { return !manifest_.empty(); }
  bool trusted_exact() const { return !options_.exact_root.empty(); }
  bool trusted_file() const { return !options_.root.empty(); }
  std::shared_ptr<sic::support::ByteSource> open(const std::filesystem::path& local_path,
                                                 uint64_t memory_budget = UINT64_MAX) const;

 private:
  RemoteOptions options_;
  std::filesystem::path manifest_;
};

}  // namespace sic::access
