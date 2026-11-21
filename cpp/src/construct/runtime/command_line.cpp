// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/runtime/command_line.hpp"

#include "construct/model/options.hpp"
#include "construct/model/worker_policy.hpp"

namespace sic::construct {
namespace {
using sic::support::cli::default_text;
const ProductionPolicy kDefaultPolicy;
const AlignmentOptions kDefaultAlignment;
}  // namespace
void ProducerCommandLine::derive_storage() {
  // Enable bounded storage capabilities on compact layouts. Actual retention is decided by
  // the shared planner from the measured precision/tower and fresh budget in the final pass.
  const auto selected_exponent = exponent;
  bool compact = true;
  if (selected_exponent > 1 && kernel_options_.at("SIC_ORDER") == "maximal") {
    sic::exact::Arithmetic arithmetic(static_cast<int>(selected_exponent), false, policy.dimension);
    compact = sic::exact::reduced_order::configure_arith(arithmetic).k == 1;
  }
  const bool bounded = compact && memory_control() != MemoryControl::keep_all;
  for (const char* key :
       {"SIC_PHASED_STORAGE", "SIC_STREAM_ACTIONS", "SIC_STREAM_REPS", "SIC_STREAM_STEPS",
        "SIC_COMPACT_WORK", "SIC_STREAM_TABLES", "SIC_ROOT_BACKING", "SIC_NEWTON_MEMORY_OWNERS",
        "SIC_NEWTON_PHASE_MEMORY", "SIC_SPOOL_LATTICE"})
    kernel_options_[key] = bounded ? "1" : "0";
  kernel_options_["SIC_REP_BAND_BITS"] = "0";
#ifdef SIC_ENABLE_TEST_HOOKS
  if (const char* bits = std::getenv("SIC_TEST_REP_BAND_BITS")) {
    if (sic::support::resources::integer(bits) > 8)
      throw sic::Error(sic::ErrorKind::invalid_argument, "SIC_TEST_REP_BAND_BITS must be in 0..8");
    kernel_options_["SIC_REP_BAND_BITS"] = bits;
  }
#endif
}

void ProducerCommandLine::validate_output(const std::string& out, bool resume) {
  namespace filesystem = std::filesystem;
  QuarantineOutput::validate_target(out);
  if (out.empty())
    throw sic::Error(sic::ErrorKind::invalid_argument, "--out requires a nonempty file path");
  std::error_code error;
  const auto status = filesystem::symlink_status(out, error);
  if (error && error != std::errc::no_such_file_or_directory)
    throw sic::Error(sic::ErrorKind::io, "cannot inspect --out: " + error.message());
  if (status.type() != filesystem::file_type::not_found)
    throw sic::Error(sic::ErrorKind::io, "--out already exists: " + out);
  const auto parent = filesystem::path(out).has_parent_path() ? filesystem::path(out).parent_path()
                                                              : filesystem::path(".");
  if (!filesystem::is_directory(parent))
    throw sic::Error(sic::ErrorKind::io, "--out parent is not a directory: " + parent.string());
  auto probe = (parent / ".sic-producer-write-XXXXXX").string();
  const int fd = mkstemp(probe.data());
  if (fd < 0)
    throw sic::Error(sic::ErrorKind::io, "--out directory is not writable: " + parent.string());
  const int closed = close(fd), removed = unlink(probe.c_str());
  if (closed || removed)
    throw sic::Error(sic::ErrorKind::io, "cannot clean up --out directory write probe");
  if (resume && filesystem::symlink_status(out + ".sicx-quarantine").type() !=
                    filesystem::file_type::not_found) {
    for (const char* suffix : {".sicx-quarantine", ".sicx-quarantine.spool.partial",
                               ".sicx-quarantine.spool.partial.journal"}) {
      struct stat artifact {};
      if (::lstat((out + suffix).c_str(), &artifact))
        throw sic::Error(errno == ENOENT ? sic::ErrorKind::integrity : sic::ErrorKind::io,
                         "completed resume requires its durable spool and journal");
      if (!S_ISREG(artifact.st_mode))
        throw sic::Error(sic::ErrorKind::integrity,
                         "completed resume requires its durable spool and journal");
    }
  }
  if (!resume &&
      (filesystem::exists(out + ".partial") || filesystem::exists(out + ".partial.journal") ||
       filesystem::exists(out + ".spool.partial") ||
       filesystem::exists(out + ".spool.partial.journal") ||
       filesystem::exists(out + ".sicx-quarantine") ||
       filesystem::exists(out + ".sicx-quarantine.partial") ||
       filesystem::exists(out + ".sicx-quarantine.partial.journal") ||
       filesystem::exists(out + ".sicx-quarantine.spool.partial") ||
       filesystem::exists(out + ".sicx-quarantine.spool.partial.journal")))
    throw sic::Error(sic::ErrorKind::io, "partial output exists; use --resume");
}

sic::support::cli::Parser ProducerCommandLine::parser() {
  return {
      "sic-construct S --out FILE [OPTIONS] (default S=1.." +
          default_text(kDefaultPolicy.dimension.maximum_exponent) +
          "; only d=2^S construction)\n"
          "glibc heap policy: trim 16 MiB, mmap 32 MiB before threads; any MALLOC_* or "
          "glibc.malloc.* tunable disables both defaults.",
      {{"--out", "FILE", "Required new output; its parent must be writable.", 1, 1},
       {"--order", "standard|maximal", "Default order follows the canonical dimension profile.", 1,
        1},
       {"--threads", "N", "Positive worker limit, honored subject to memory admission.", 1, 1},
       {"--memory-mib", "M", "Positive finite memory cap in MiB; default discovered budget.", 1, 1},
       {"--maximum-exponent", "S",
        "Construction dimension allowance (default " +
            default_text(kDefaultPolicy.dimension.maximum_exponent) + "; wire maximum " +
            default_text(sic::exact::DimensionPolicy::kWireMaximumExponent) + ").",
        1, 1},
       {"--maximum-precision", "BITS",
        "Computational branch precision ceiling (default " +
            default_text(kDefaultPolicy.precision.maximum_bits) + "; " +
            default_text(sic::kMinimumToolPrecision) + ".." +
            default_text(sic::kNativeMaximumPrecision) + ").",
        1, 1},
       {"--valuation-table-mib", "M",
        "Per-table rescaling allowance (default " +
            default_text(kDefaultPolicy.valuation.table_bytes >> 20) + " MiB).",
        1, 1},
       {"--valuation-total-mib", "M",
        "Total rescaling allowance (default " +
            default_text(kDefaultPolicy.valuation.total_bytes >> 20) + " MiB).",
        1, 1},
       {"--generator-selector", "step-selection|conjugate-alignment",
        "Generator selection (default " + default_text(kAlignmentSelector) + ").", 1, 1},
       {"--alignment-memory-mib", "M",
        "Complete incremental search budget in MiB (default " +
            default_text(kDefaultAlignment.memory_bytes >> 20) +
            "); cache when it fits, replay "
            "otherwise.",
        1, 1},
       {"--alignment-cache-mib", "M", "Optional F-cache ceiling in MiB; 0 forces replay.", 1, 1},
       {"--alignment-batch-mib", "M",
        "Scoring-batch target in MiB (default " + default_text(kAlignmentBatchBytes >> 20) + ").",
        1, 1},
       {"--alignment-worker-mib", "M",
        "Candidate-worker target in MiB (default " + default_text(kAlignmentWorkerBytes >> 20) +
            ").",
        1, 1},
       {"--alignment-train", "N",
        "Training leaves per representative channel (default " + default_text(kAlignmentTrain) +
            "; 1.." + default_text(kDefaultPolicy.alignment_samples) + ").",
        1, 1},
       {"--alignment-candidates", "N",
        "Candidate cap per level (default " + default_text(kAlignmentCandidates) + "; 1.." +
            default_text(kDefaultPolicy.alignment_samples) + ").",
        1, 1},
       {"--alignment-sweeps", "N",
        "Coordinate-descent sweeps (default " + default_text(kAlignmentSweeps) + "; 0.." +
            default_text(kDefaultPolicy.alignment_sweeps) + ").",
        1, 1},
       {"--alignment-seed", "N", "Search seed (default " + default_text(kAlignmentSeed) + ").", 1,
        1},
       {"--alignment-trace", "", "Log high-water memory checks during search."},
       {"--resume", "", "Adopt an existing partial output (S>=2)."},
       {"--group-cache", "FILE", "Verified group checkpoint.", 1, 1},
       {"--stop-file", "FILE", "Stop cooperatively while this file exists.", 1, 1},
       {"--progress", "", "Print progress diagnostics."},
       {"--radicals", "FILE", "S=1 radical record override; default embedded pinned d2.sicrad.", 1,
        1},
       {"--log-admissions", "", "Log memory, disk and worker admission decisions to stderr.", 0, 0},
       {"--analytic-cache", "DIR", "Coarse analytic checkpoint.", 1, 1},
       {"--completed-class-cache", "DIR", "Current-producer certified class cache.", 1, 1},
       {"--require-completed-cache", "", "Require all selected completed classes."}}};
}

ProducerCommandLine::ProducerCommandLine(int argc, char** argv,
                                         sic::construct::ProductionPolicy chosen_policy) try
    : policy(std::move(chosen_policy)), alignment(kDefaultAlignment) {
  static const std::map<std::string, std::string> valued = {
      {"--out", "SIC_OUT"},
      {"--order", "SIC_ORDER"},
      {"--threads", "SIC_THREADS"},
      {"--memory-mib", "SIC_MEMORY_MB"},
      {"--group-cache", "SIC_GROUP_CACHE"},
      {"--stop-file", "SIC_STOP_FILE"},
      {"--radicals", "SIC_D2_RADICALS"},
      {"--analytic-cache", "SIC_ANALYTIC_CACHE"},
      {"--completed-class-cache", "SIC_COMPLETED_CLASS_CACHE"},
      {"--require-completed-cache", "SIC_REQUIRE_COMPLETED_CACHE"}};
  namespace cli = sic::support::cli;
  for (int i = 1; i < argc; ++i)
    if (std::string_view(argv[i]) == "--dimension" || std::string_view(argv[i]) == "--d")
      throw cli::UsageError("general-d construction is unsupported; supply exponent S for d=2^S");
  const auto parsed = parser().parse(argc, argv);
  parsed.positionals(1, 1);
  for (const auto& [flag, key] : valued) {
    if (!parsed.has(flag)) continue;
    const auto value = flag == "--require-completed-cache" ? "1" : parsed.get(flag);
    cli::require(!value.empty(), "empty value for " + flag);
    kernel_options_[key] = value;
  }
  alignment.selector = parsed.get("--generator-selector", kAlignmentSelector);
  cli::require(
      alignment.selector == "conjugate-alignment" || alignment.selector == "step-selection",
      "unknown --generator-selector");
  auto count = [&](const char* flag, uint64_t& value, uint64_t lower, uint64_t upper) {
    if (parsed.has(flag)) value = cli::number<uint64_t>(parsed.get(flag), flag, lower, upper);
  };
  if (parsed.has("--maximum-precision"))
    policy.precision.maximum_bits =
        cli::number<long>(parsed.get("--maximum-precision"), "--maximum-precision",
                          sic::kMinimumToolPrecision, sic::kNativeMaximumPrecision);
  count("--alignment-train", alignment.train, 1, policy.alignment_samples);
  count("--alignment-candidates", alignment.candidates, 1, policy.alignment_samples);
  uint64_t sweeps = alignment.sweeps;
  count("--alignment-sweeps", sweeps, 0, policy.alignment_sweeps);
  alignment.sweeps = static_cast<int>(sweeps);
  count("--alignment-seed", alignment.seed, 0, INT64_MAX);
  auto memory = [&](const char* flag, uint64_t& bytes, bool zero = false) {
    if (parsed.has(flag)) {
      const auto mb = cli::number<uint64_t>(parsed.get(flag), flag, zero ? 0 : 1,
                                            UINT64_MAX / (uint64_t{1} << 20));
      bytes = mb * (uint64_t{1} << 20);
    }
  };
  memory("--valuation-table-mib", policy.valuation.table_bytes);
  memory("--valuation-total-mib", policy.valuation.total_bytes);
  memory("--alignment-memory-mib", alignment.memory_bytes);
  memory("--alignment-batch-mib", alignment.batch_bytes);
  memory("--alignment-worker-mib", alignment.worker_bytes);
  if (parsed.has("--alignment-cache-mib")) {
    uint64_t bytes = 0;
    memory("--alignment-cache-mib", bytes, true);
    alignment.cache_bytes = bytes;
  }
  alignment.trace = parsed.has("--alignment-trace");
  log_admissions = parsed.has("--log-admissions");
  resume_ = parsed.has("--resume");
  if (parsed.has("--progress")) kernel_options_["SIC_PROGRESS"] = "1";
  if (parsed.has("--maximum-exponent"))
    policy.dimension.maximum_exponent =
        cli::number<unsigned>(parsed.get("--maximum-exponent"), "--maximum-exponent", 1,
                              sic::exact::DimensionPolicy::kWireMaximumExponent);
  const auto s = cli::number<unsigned>(parsed.pos[0], "S", 1, policy.dimension.maximum_exponent);
  (void)policy.dimension.checked(s);
  if (s == 1 && resume_)
    throw cli::UsageError("--resume is unsupported by the closed-form d=2 producer");
  if (s != 1 && kernel_options_.contains("SIC_D2_RADICALS"))
    throw cli::UsageError("--radicals is only supported for S=1");
  if (const auto it = kernel_options_.find("SIC_THREADS"); it != kernel_options_.end())
    (void)cli::number<int>(it->second, "--threads", 1);
  if (const auto it = kernel_options_.find("SIC_MEMORY_MB"); it != kernel_options_.end()) try {
      (void)cli::number<double>(it->second, "--memory-mib", std::numeric_limits<double>::min());
      (void)sic::support::resources::megabytes(it->second.c_str());
    } catch (const std::exception&) {
      throw cli::UsageError("--memory-mib must be positive, finite and representable");
    }
  if (const auto it = kernel_options_.find("SIC_REQUIRE_COMPLETED_CACHE");
      it != kernel_options_.end())
    if (it->second != "0" && it->second != "1")
      throw cli::UsageError("--require-completed-cache must be 0 or 1");
  if (const auto it = kernel_options_.find("SIC_ORDER"); it != kernel_options_.end())
    if (it->second != "standard" && it->second != "maximal")
      throw cli::UsageError("--order must be standard or maximal");
  if (!kernel_options_.contains("SIC_OUT")) throw cli::UsageError("--out is required");
  if (const auto it = kernel_options_.find("SIC_STOP_FILE"); it != kernel_options_.end())
    if (std::filesystem::exists(it->second))
      throw sic::Error(sic::ErrorKind::cancelled, "requested stop: --stop-file exists");
  // Validate documented environment inputs before output probes or CLI environment setup.
  ProducerOptions(s, alignment, policy).validate_settings();
  validate_output(kernel_options_.at("SIC_OUT"), resume_);
  exponent = s;
  if (!kernel_options_.contains("SIC_ORDER"))
    kernel_options_["SIC_ORDER"] =
        sic::exact::CanonicalProfile::maximal_order(s) ? "maximal" : "standard";
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

void ProducerCommandLine::install_kernel_options(char** environment) {
  // This allowlist is available only with SIC_ENABLE_TEST_HOOKS=ON.
  // The ceil precision-refusal key is direct-library only, and is scrubbed here.
#ifdef SIC_ENABLE_TEST_HOOKS
  static const std::set<std::string> test_hooks = {
      "SIC_CLASS_THREADS",           "SIC_TEST_REP_BAND_BITS",
      "SIC_NEWTON_MATRIX_BITS",      "SIC_DIRECT_ETA_TEST_FAULT",
      "SIC_TEST_NEWTON_DEFER_CLASS", "SIC_TEST_NEWTON_DEFER_ENTRY_AFTER_HINT",
      "SIC_TEST_FAIL_SIC_CHECK",     "SIC_TEST_KILL_AFTER_WRITE",
      "SIC_TEST_PRECISION_RETRY",    "SIC_FAULT_KILL_AFTER_CHUNKS",
      "SIC_FAULT_KILL_AT_BYTE",      "SIC_TEST_ABORT_AFTER_PARTS"};
#else
  static const std::set<std::string> test_hooks;
#endif
  // Developer memory selectors are instrumented-build-only. OFF scrubs all hook keys.
#ifdef SIC_ENABLE_TEST_HOOKS
  for (const char* key : {"SIC_DEV_MEMORY_MB", "SIC_DEV_KEEP_ALL", "SIC_DEV_SPILL"})
    if (const char* value = std::getenv(key)) kernel_options_[key] = value;
#endif
  std::vector<std::string> inherited;
  for (char** entry = environment; *entry; ++entry) {
    const std::string value = *entry, key = value.substr(0, value.find('='));
    if (key.starts_with("SIC_") && !test_hooks.contains(key)) inherited.push_back(key);
  }
  for (const auto& key : inherited)
    if (unsetenv(key.c_str())) {
      const int error = errno;
      throw sic::Error(error == ENOMEM   ? sic::ErrorKind::resource
                       : error == EINVAL ? sic::ErrorKind::invalid_argument
                                         : sic::ErrorKind::io,
                       "cannot clear inherited producer setting " + key);
    }
  if (!kernel_options_.contains("SIC_THREADS"))
    kernel_options_["SIC_THREADS"] = std::to_string(default_producer_workers(exponent));
  kernel_options_["SIC_ENTRY_ARB"] = "1";

  if (resume_) kernel_options_["SIC_SICX_RESUME"] = "1";

  {
    sic::support::KernelSettingsScope selected_settings(&kernel_options_);
    derive_storage();
  }
  for (const auto& [key, value] : kernel_options_)
    if (setenv(key.c_str(), value.c_str(), 1)) {
      const int error = errno;
      throw sic::Error(error == ENOMEM   ? sic::ErrorKind::resource
                       : error == EINVAL ? sic::ErrorKind::invalid_argument
                                         : sic::ErrorKind::io,
                       "cannot install kernel option " + key);
    }
}

}  // namespace sic::construct
