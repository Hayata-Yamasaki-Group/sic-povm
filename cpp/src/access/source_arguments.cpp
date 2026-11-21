// SPDX-License-Identifier: AGPL-3.0-or-later
#include "access/source_arguments.hpp"

#include <cctype>
#include <charconv>
#include <climits>

#include "sic/sicx/io.hpp"
#include "sic/sicx/verify.hpp"
#include "sic/transport/parts.hpp"
namespace sic::access {
namespace {
using sic::support::cli::default_text;
const RemoteOptions kDefaults;
}  // namespace
using namespace sic::sicx;

namespace filesystem = std::filesystem;
sic::support::cli::Parser SourceArguments::parser(sic::support::cli::Parser base) {
  return base.with({
      {"--remote", "PARTS.json", "Read a local manifest through remote byte access.", 1, 1},
      {"--source", "URL|DIR", "Remote parts source override.", 1, 1},
      {"--cache", "DIR", "Remote extent cache; use XDG cache or ~/.cache/sicx when omitted.", 1, 1},
      {"--range-jobs", "N",
       "Concurrent requests, 1.." + default_text(kDefaults.limits.maximum_jobs) + " (default " +
           default_text(kDefaults.jobs) + ").",
       1, 1},
      {"--range-bytes", "N",
       "Extent size, 1.." + default_text(kDefaults.limits.maximum_extent_bytes) + " (default " +
           default_text(kDefaults.range_bytes) + ").",
       1, 1},
      {"--coalesce-gap", "N",
       "Maximum scheduled gap bytes, 0.." + default_text(kDefaults.limits.maximum_extent_bytes) +
           " (default " + default_text(kDefaults.coalesce_gap) + ").",
       1, 1},
      {"--prefetch-mib", "N",
       "Disk lookahead, 0.." + default_text(kDefaults.limits.maximum_prefetch_bytes >> 20) +
           " MiB (default " + default_text(kDefaults.prefetch_bytes >> 20) + "; 0 disables).",
       1, 1},
      {"--cache-max-ranges", "N",
       "Extent/plan cap, 1.." + default_text(kDefaults.limits.maximum_ranges) + " (default " +
           default_text(kDefaults.max_ranges) + ").",
       1, 1},
      {"--remote-max-fraction", "F",
       "Missing-byte fraction, (0,1] (default " + default_text(kDefaults.max_fraction) + ").", 1,
       1},
      {"--remote-timeout-ms", "N",
       "Per-attempt timeout in milliseconds (default " + default_text(kDefaults.timeout_ms) + ").",
       1, 1},
      {"--remote-retries", "N",
       "Additional attempts, 0.." + default_text(kDefaults.limits.maximum_retries) + " (default " +
           default_text(kDefaults.retries) + ").",
       1, 1},
      {"--expect-exact-root", "HEX", "Trusted exact-root pin.", 1, 1},
      {"--expect-root", "HEX", "Trusted full-root pin.", 1, 1},
      {"--fetch-full", "", "Permit verified full-download fallback."},
  });
}

SourceArguments::SourceArguments(sic::support::cli::Arguments& args) {
  manifest_ = args.get("--remote");
  sic::support::cli::require(!args.has("--remote") || !manifest_.empty(),
                             "--remote needs a manifest path");
  for (const auto& name : {"--source", "--cache", "--range-jobs", "--range-bytes", "--coalesce-gap",
                           "--prefetch-mib", "--cache-max-ranges", "--remote-max-fraction",
                           "--remote-timeout-ms", "--remote-retries", "--fetch-full"})
    sic::support::cli::require(remote() || !args.has(name),
                               "remote access options require --remote PARTS.json");
  options_.source = args.get("--source");
  options_.cache = args.get("--cache");
  options_.jobs =
      args.numeric<unsigned>("--range-jobs", kDefaults.jobs, 1, kDefaults.limits.maximum_jobs);
  options_.range_bytes = args.numeric<uint64_t>("--range-bytes", kDefaults.range_bytes, 1,
                                                kDefaults.limits.maximum_extent_bytes);
  options_.coalesce_gap = args.numeric<uint64_t>("--coalesce-gap", kDefaults.coalesce_gap, 0,
                                                 kDefaults.limits.maximum_extent_bytes);
  options_.prefetch_bytes = args.numeric<uint64_t>("--prefetch-mib", kDefaults.prefetch_bytes >> 20,
                                                   0, kDefaults.limits.maximum_prefetch_bytes >> 20)
                            << 20;
  options_.max_ranges = args.numeric<uint64_t>("--cache-max-ranges", kDefaults.max_ranges, 1,
                                               kDefaults.limits.maximum_ranges);
  options_.retries = args.numeric<unsigned>("--remote-retries", kDefaults.retries, 0,
                                            kDefaults.limits.maximum_retries);
  options_.timeout_ms =
      args.numeric<long>("--remote-timeout-ms", kDefaults.timeout_ms, 1, LONG_MAX);
  options_.max_fraction =
      args.numeric<double>("--remote-max-fraction", kDefaults.max_fraction, 0, 1);
  sic::support::cli::require(options_.max_fraction > 0, "--remote-max-fraction must be in (0,1]");
  options_.exact_root = args.get("--expect-exact-root");
  options_.root = args.get("--expect-root");
  options_.fetch_full = args.has("--fetch-full");
  if (remote()) args.pos.insert(args.pos.begin(), manifest_.string());
}

std::shared_ptr<sic::support::ByteSource> SourceArguments::open(const filesystem::path& path,
                                                                uint64_t memory_budget) const {
  if (remote()) {
    auto options = options_;
    options.memory_budget = memory_budget;
    return remote_byte_source(manifest_, options);
  }
  class PinnedLocal final : public sic::support::ByteSource {
    std::shared_ptr<sic::support::ByteSource> source_;
    std::string exact_, file_;

   public:
    PinnedLocal(const filesystem::path& path, std::string exact, std::string file)
        : source_(sic::support::local_byte_source(path)),
          exact_(std::move(exact)),
          file_(std::move(file)) {
      if ((!exact_.empty() && !sic::transport::is_hex64(exact_)) ||
          (!file_.empty() && !sic::transport::is_hex64(file_)))
        fail(sic::ErrorKind::invalid_argument, "invalid trusted root pin");
    }
    uint64_t size() const override { return source_->size(); }
    void read(uint64_t offset, std::span<uint8_t> out) const override {
      source_->read(offset, out);
    }
    void bind_identity(const std::string& exact, const std::string& file) const override {
      if ((!exact_.empty() && exact != exact_) || (!file_.empty() && file != file_))
        fail(sic::ErrorKind::integrity, "local file differs from trusted root pin");
    }
  };
  return std::make_shared<PinnedLocal>(path, options_.exact_root, options_.root);
}
}  // namespace sic::access
