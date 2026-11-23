// SPDX-License-Identifier: AGPL-3.0-or-later
#include <limits>
#include <chrono>
#include <new>

#include "exact/group/ray_selection.hpp"
#include "sic/support/cli.hpp"
#include "sic/support/durable_file.hpp"
#include "sic/support/json.hpp"
#include "sic/support/kernel_settings.hpp"
#include "sic/support/tool_version.hpp"
#include "support/resources/platform_snapshot.hpp"

namespace {
namespace cli = sic::support::cli;
namespace resources = sic::support::resources;
namespace sha256 = sic::support::sha256;
using sic::support::Json;
using Clock = std::chrono::steady_clock;

constexpr std::string_view kTool = "sic-ray-selector";
constexpr int kMaximumThreads = 1024;

struct WorkerDefault {
  int value() const { return resources::default_threads(kMaximumThreads); }
  std::string description() const {
    return "process CPU allowance, capped at " + cli::default_text(kMaximumThreads);
  }
};
constexpr WorkerDefault kDefaultThreads;

const cli::Parser& parser() {
  static const cli::Parser value{
      "sic-ray-selector derive|check (--group FILE | --dimension-two) [OPTIONS]\n"
      "       sic-ray-selector list --words FILE [--sha256 HEX]",
      {
          {"--group", "FILE", "Read a verified native Group checkpoint.", 1, 1, false},
          {"--sha256", "HEX", "SHA-256 pin for the input (Group default: FILE.sha256).", 1, 1,
           false},
          {"--dimension-two", "", "Use the dimension-two closed form.", 0, 0, false},
          {"--words", "FILE",
           "Bound ray list (list or check mode), or selector entry JSON (check mode).", 1, 1,
           false},
          {"--threads", "N",
           "Worker limit, 1.." + cli::default_text(kMaximumThreads) + " (default " +
               kDefaultThreads.description() + ").",
           1, 1, false},
          {"--memory-mib", "M", "Positive finite memory cap in MiB (default: discovered budget).",
           1, 1, false},
          {"--receipt", "FILE", "Write a new verification receipt valid only with exit status 0.",
           1, 1, false},
      }};
  return value;
}

int64_t elapsed_us(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

int run(const cli::Arguments& args) {
  args.positionals(1, 1);
  const auto& mode = args.pos.front();
  cli::require(mode == "derive" || mode == "check" || mode == "list",
               "expected derive, check or list");
  if (args.has("--sha256")) {
    const auto pin = args.get("--sha256");
    cli::require(pin.size() == 64 && pin.find_first_not_of("0123456789abcdef") == pin.npos,
                 "--sha256 must contain 64 lowercase hexadecimal digits");
  }
  if (mode == "list") {
    args.only({"--words", "--sha256"});
    cli::require(!args.get("--words").empty(), "--words is required in list mode");
    resources::Budget budget;
    std::string digest;
    const auto list = sic::sicx::read_ray_list(args.get("--words"), budget, false, &digest);
    if (args.has("--sha256") && digest != args.get("--sha256"))
      throw sic::Error(sic::ErrorKind::integrity, "ray list SHA-256 differs from the supplied pin");
    std::cout << sic::sicx::bound_ray_list_json(list);
    return cli::kSuccess;
  }
  const bool check = mode == "check";
  const bool d2 = args.has("--dimension-two");
  cli::require(d2 != args.has("--group"), "specify exactly one of --group or --dimension-two");
  cli::require(!d2 || !args.has("--sha256"), "--sha256 requires --group");
  cli::require(args.has("--words") == check, "--words is required in check mode only");
  cli::require(d2 || !args.get("--group").empty(), "--group must be nonempty");
  cli::require(!check || !args.get("--words").empty(), "--words must be nonempty");
  cli::require(!args.has("--receipt") || !args.get("--receipt").empty(),
               "--receipt must be nonempty");
  const int threads = args.has("--threads")
                          ? cli::number<int>(args.get("--threads"), "--threads", 1, kMaximumThreads)
                          : kDefaultThreads.value();
  const auto memory = args.get("--memory-mib");
  sic::support::KernelSettings settings;
  if (args.has("--memory-mib")) {
    (void)cli::number<double>(memory, "--memory-mib", std::numeric_limits<double>::min());
    try {
      (void)resources::megabytes(memory.c_str());
    } catch (const sic::Error&) {
      throw cli::UsageError("--memory-mib must be positive, finite and representable");
    }
    settings.emplace("SIC_MEMORY_MB", memory);
    for (const char* name : {"SIC_PARALLEL_GRAIN", "SIC_DEV_MEMORY_MB"})
      if (const char* value = sic::support::kernel_setting(name)) settings.emplace(name, value);
  }
  const sic::support::KernelSettingsScope memory_scope(
      args.has("--memory-mib") ? &settings : sic::support::current_kernel_settings);
  sic::support::ProgressSink diagnostics;
  diagnostics.emit = [](const std::string& text) { std::cerr << text; };
  const sic::support::ProgressScope progress(&diagnostics);
  resources::Budget budget;
  flint_set_num_threads(1);
  const bool read_ahead = threads > 1;
  if (!d2)
    budget.require_additional("group checkpoint authentication",
                              sha256::kFileChunkBytes * (read_ahead ? 2 : 1));
  const auto read_start = Clock::now();
  const auto group = d2 ? sic::exact::dimension_two_projection()
                        : sic::exact::GroupCheckpoint::project(args.get("--group"),
                                                               args.get("--sha256"), read_ahead);
  const auto select_start = Clock::now();
  std::string word_input_digest;
  sic::sicx::RayList list;
  if (check) {
    list = sic::sicx::read_ray_list(args.get("--words"), budget, true, &word_input_digest);
    if (list.smith != group.smith)
      throw sic::Error(sic::ErrorKind::integrity, "selector Smith basis differs from the Group");
    if (list.dimension || d2) {
      const auto expected = sic::exact::select_rays(group, threads, budget);
      if (list.dimension &&
          (list.dimension != expected.dimension || list.source != expected.source))
        throw sic::Error(sic::ErrorKind::integrity, "ray list source differs from the Group");
      if (list.words != expected.words)
        throw sic::Error(sic::ErrorKind::invalid_format,
                         "ray list differs from the selected words");
    } else {
      const int workers = sic::exact::admit_ray_selection(group, threads, budget);
      const sic::exact::RaySelection selection(group, workers);
      selection.check(list.words);
      sic::exact::sort_ray_words(list.words);
    }
  } else {
    list = sic::exact::select_rays(group, threads, budget);
  }
  const auto output = sic::sicx::ray_entry_json(list);
  const auto select_end = Clock::now();

  if (args.has("--receipt")) {
    Json receipt;
    receipt["schema"] = Json::string_value("sic-ray-selection-receipt/1.0");
    receipt["version"] = Json::string_value(std::string(cli::kToolVersionNumber));
    receipt["source_commit"] = Json::string_value(SIC_SOURCE_COMMIT);
    receipt["mode"] = Json::string_value(mode);
    receipt["group_sha256"] = Json::string_value(group.digest);
    receipt["group_source"] = Json::string_value(group.source);
    receipt["group_bytes"] = Json::integer_value(group.bytes);
    receipt["group_read_us"] = Json::integer_value(d2 ? 0 : elapsed_us(read_start, select_start));
    receipt["selection_us"] = Json::integer_value(elapsed_us(select_start, select_end));
    receipt["threads"] = Json::integer_value(threads);
    receipt["dimension"] = Json::integer_value(uint64_t{1} << group.exponent);
    receipt["output_sha256"] = Json::string_value(sha256::digest_hex(output));
    if (check) receipt["words_sha256"] = Json::string_value(word_input_digest);
    sic::support::atomic_write(args.get("--receipt"), sic::support::dump_json(receipt) + '\n',
                               false);
  }
  std::cout << output;
  return cli::kSuccess;
}
}  // namespace

int main(int argc, char** argv) {
  cli::prepare_stdout();
  if (const auto status = cli::version_request(argc, argv, kTool)) return *status;
  try {
    const int status = run(parser().parse(argc, argv));
    return cli::finish_stdout(status, kTool);
  } catch (const std::bad_alloc&) {
    return cli::report(parser(), sic::Error(sic::ErrorKind::resource, "allocation failed"), kTool);
  } catch (const std::exception& error) {
    return cli::report(parser(), error, kTool);
  }
}
