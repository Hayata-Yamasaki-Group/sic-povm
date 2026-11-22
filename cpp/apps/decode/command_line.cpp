// SPDX-License-Identifier: AGPL-3.0-or-later
#include "command_line.hpp"

#include <algorithm>

#include "access/source_arguments.hpp"
#include "sic/decode/placement.hpp"
#include "support/resources/platform_snapshot.hpp"
namespace sic::apps::decode {
namespace {
using sic::support::cli::default_text;
const sic::decode::Decoder::Options kDefaults;
struct WorkerDefault {
  unsigned ceiling;
  unsigned value(const sic::support::cli::Arguments& args, unsigned explicit_fallback) const {
    if (args.has("--threads")) return explicit_fallback;
    return sic::support::resources::default_threads(ceiling);
  }
  std::string description() const {
    return "process CPU allowance, capped at " + default_text(ceiling);
  }
};
constexpr WorkerDefault kDefaultWorkers{32};
constexpr uint64_t kDefaultWorkingBits = 256;
constexpr uint64_t kMinimumWorkingBits = 1;
constexpr unsigned long kMaximumNumaNode = 63;
constexpr std::string_view kDefaultSelection = "all";
constexpr std::string_view kDefaultTower = "auto";
}  // namespace

const sic::support::cli::Parser& command_line_parser() {
  static const sic::support::cli::Parser parser =
      sic::access::SourceArguments::parser(sic::support::cli::Parser{
          "sic-decode FILE.sicx OUTDIR [OPTIONS]",
          {
              {"--rays", "all|L1,L2,...",
               "Ray labels (default " + default_text(kDefaultSelection) + ").", 1, 1, false},
              {"--coordinates", "all|A-B|A,...",
               "Coordinates/ranges, comma separated (default " + default_text(kDefaultSelection) +
                   ").",
               1, 1, false},
              {"--maximum-precision", "BITS",
               "Computational precision ceiling (default " +
                   default_text(kDefaults.maximum_precision) + "; " +
                   default_text(sic::kMinimumToolPrecision) + ".." +
                   default_text(sic::kNativeMaximumPrecision) + ").",
               1, 1, false},
              {"--target-bits", "BITS",
               "Accuracy within --maximum-precision (default " + default_text(kDefaults.target) +
                   "; minimum " + default_text(sic::decode::kMinimumTargetBits) + ").",
               1, 1, false},
              {"--guard-bits", "BITS",
               "Additional working bits, at most --maximum-precision (default " +
                   (kDefaults.delta == -1
                        ? "PREC hint or " + default_text(sic::decode::kDefaultGuardBits)
                        : default_text(kDefaults.delta)) +
                   ").",
               1, 1, false},
              {"--initial-bits", "BITS",
               "Base precision, at most --maximum-precision (default " +
                   (kDefaults.base_bits == -1
                        ? "PREC hint or min(" +
                              default_text(sic::decode::kDefaultInitialPrecisionCap) +
                              ", --maximum-precision), raised to target plus guard bits"
                        : default_text(kDefaults.base_bits)) +
                   "; minimum explicit " + default_text(sic::decode::kMinimumTargetBits) + ").",
               1, 1, false},
              {"--threads", "T",
               "Worker count, 1.." + default_text(kDefaults.maximum_threads) + " (default " +
                   kDefaultWorkers.description() + ").",
               1, 1, false},
              {"--memory-mib", "M",
               "Positive memory budget in MiB (default " +
                   default_text(kDefaults.memory_budget >> 20) + ").",
               1, 1, false},
              {"--no-pin", "", "Disable worker pinning.", 0, 0, false},
              {"--stop-file", "FILE", "Poll for cancellation at decoder task boundaries.", 1, 1,
               false},
              {"--log-admissions", "", "Log memory admission decisions to stderr.", 0, 0, false},
              {"--profile", "", "Enable decoder profiling.", 0, 0, false},
              {"--tower", "auto|resident|stream",
               "Tower storage policy (default " + default_text(kDefaultTower) + ").", 1, 1, false},
              {"--mem-policy", "bind:N|preferred:N|interleave",
               "Process NUMA memory policy; node index 0.." + default_text(kMaximumNumaNode) + ".",
               1, 1, false},
              {"--check-budget", "", "Fail when peak RSS exceeds the budget.", 0, 0, false},
              {"--finalize", "", "Append certified finalization records in place.", 0, 0, false},
              {"--resume", "", "Resume interrupted finalization.", 0, 0, false},
              {"--working-bits", "B",
               "Finalization precision, " + default_text(kMinimumWorkingBits) + ".." +
                   default_text(sic::sicx::kMaximumWorkingBits) + " (default " +
                   default_text(kDefaultWorkingBits) + ").",
               1, 1, false},
              {"--ray", "LABEL WORD ANCHOR",
               "Repeatable ray: WORD comma-separated unsigned integers; "
               "ANCHOR >= 0 is required.",
               3, 3, true},
          }});
  return parser;
}
Invocation parse_command_line(int argc, char** argv) {
  return Invocation(command_line_parser().parse(argc, argv));
}
}  // namespace sic::apps::decode

#include <filesystem>

#include "support/resources/numa_policy.hpp"
#include "support/resources/platform_snapshot.hpp"
namespace sic::apps::decode {
sic::support::resources::NumaPolicy parse_memory_policy(const std::string& spec) try {
  constexpr int kPreferred = 1, kBind = 2, kInterleave = 3;
  unsigned long mask = 0;
  int mode = 0;
  const unsigned nodes = sic::support::resources::numa_node_count();
  auto node = [&](const std::string& text) {
    const unsigned long n =
        sic::support::cli::number<unsigned long>(text, "--mem-policy node", 0, kMaximumNumaNode);
    if (n >= std::max(nodes, 1u))
      throw sic::support::cli::UsageError("--mem-policy: no NUMA node " + text);
    return 1ul << n;
  };
  if (spec == "interleave") {
    mode = kInterleave;
    mask = nodes > kMaximumNumaNode ? ~0ul : (1ul << std::max(nodes, 1u)) - 1;
  } else if (spec.rfind("bind:", 0) == 0) {
    mode = kBind;
    mask = node(spec.substr(5));
  } else if (spec.rfind("preferred:", 0) == 0) {
    mode = kPreferred;
    mask = node(spec.substr(10));
  } else
    throw sic::support::cli::UsageError("invalid --mem-policy " + spec);

  return {mode, mask};
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::apps::decode

#include <sstream>
namespace sic::apps::decode {

std::vector<uint64_t> parse_word(const std::string& text) {
  sic::support::cli::require(!text.empty() && text.back() != ',', "invalid word " + text);
  std::vector<uint64_t> word;
  std::stringstream in(text);
  for (std::string item; std::getline(in, item, ',');) {
    if (item.empty() || item.find_first_not_of("0123456789") != std::string::npos)
      throw sic::support::cli::UsageError("invalid word " + text);
    word.push_back(sic::support::cli::number<uint64_t>(item, "--ray WORD"));
  }
  return word;
}
Invocation::Invocation(sic::support::cli::Arguments arguments)
    : arguments_(std::move(arguments)),
      admission_log_(arguments_.has("--log-admissions")),
      stop_scope_(arguments_.get("--stop-file", "")),
      input((sic::support::check_stop(), arguments_)),
      start((arguments_.positionals(2, 2), std::chrono::steady_clock::now())),
      file((flint_set_num_threads(1), arguments_.pos[0])),
      outdir(arguments_.pos[1]) {
  options.cancellation =
      std::make_shared<sic::support::StopToken>(arguments_.get("--stop-file", ""));
  rays_arg = arguments_.get("--rays", std::string(kDefaultSelection));
  coords_arg = arguments_.get("--coordinates", std::string(kDefaultSelection));
  finalize = arguments_.has("--finalize");
  resume = arguments_.has("--resume");
  check_budget = arguments_.has("--check-budget");
  working_bits = arguments_.numeric<uint64_t>("--working-bits", kDefaultWorkingBits,
                                              kMinimumWorkingBits, sic::sicx::kMaximumWorkingBits);
  options.maximum_precision =
      arguments_.numeric<slong>("--maximum-precision", kDefaults.maximum_precision,
                                sic::kMinimumToolPrecision, sic::kNativeMaximumPrecision);
  options.target =
      arguments_.numeric<slong>("--target-bits", kDefaults.target, sic::decode::kMinimumTargetBits,
                                options.maximum_precision);
  options.delta =
      arguments_.numeric<slong>("--guard-bits", kDefaults.delta, 0, options.maximum_precision);
  options.base_bits =
      arguments_.numeric<slong>("--initial-bits", kDefaults.base_bits,
                                sic::decode::kMinimumTargetBits, options.maximum_precision);
  options.threads =
      arguments_.numeric<unsigned>("--threads", kDefaultWorkers.value(arguments_, options.threads),
                                   1, kDefaults.maximum_threads);
  options.memory_budget = arguments_.numeric<uint64_t>(
                              "--memory-mib", kDefaults.memory_budget >> 20, 1, UINT64_MAX >> 20)
                          << 20;
  options.pin = !arguments_.has("--no-pin");
  options.progress = &std::cerr;
  options.tune_process_allocator = true;
  options.profile = arguments_.has("--profile");
  tower = arguments_.get("--tower", std::string(kDefaultTower));
  sic::support::cli::require(tower == "auto" || tower == "resident" || tower == "stream",
                             "--tower must be resident, stream or auto");
  options.tower_resident = tower == "auto" ? -1 : tower == "resident" ? 1 : 0;
  if (coords_arg != "all") {
    sic::support::cli::require(!coords_arg.empty() && coords_arg.back() != ',',
                               "invalid --coordinates");
    std::stringstream in(coords_arg);
    for (std::string item; std::getline(in, item, ',');) {
      const auto dash = item.find('-');
      const auto a = sic::support::cli::number<uint64_t>(item.substr(0, dash), "--coordinates", 0,
                                                         UINT64_MAX - 1);
      const auto b = dash == std::string::npos
                         ? a
                         : sic::support::cli::number<uint64_t>(item.substr(dash + 1),
                                                               "--coordinates", a, UINT64_MAX - 1);
      intervals.push_back({a, b});
    }
  }
  for (const auto& values : arguments_.all("--ray")) {
    sic::support::cli::require(values.size() == 3, "--ray needs LABEL WORD ANCHOR");
    sic::sicx::FinalizeRay r;
    r.label = values[0];
    r.word = parse_word(values[1]);
    r.anchor = sic::support::cli::number<int64_t>(values[2], "--ray ANCHOR", 0);
    given.push_back(r);
  }
  if (arguments_.has("--mem-policy")) {
    const auto spec = arguments_.get("--mem-policy");
    sic::support::resources::apply_numa_policy(parse_memory_policy(spec), spec);
  }
  sic::support::cli::require(!resume || finalize, "--resume requires --finalize");
  sic::support::cli::require(!finalize || !input.remote(),
                             "--finalize requires a local complete SICX file");
  if (finalize) {
    if (given.empty())
      throw sic::support::cli::UsageError("--finalize needs --ray LABEL WORD ANCHOR for every ray");
    if (coords_arg != "all" || rays_arg != "all")
      throw sic::support::cli::UsageError("--finalize decodes every coordinate of every ray");
    if (sic::sicx::finalization_in_progress(file) && !resume)
      throw sic::Error(sic::ErrorKind::invalid_argument,
                       "an interrupted finalization exists; rerun with --resume");
  }
}
}  // namespace sic::apps::decode
