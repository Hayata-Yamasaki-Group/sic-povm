// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "../../apps/identify/command_line.hpp"
#include "../../apps/overlaps/command_line.hpp"
#include "../../apps/stabilizer/command_line.hpp"
#ifdef SIC_HAVE_FLINT
#include "../../apps/container/command_line.hpp"
#include "../../apps/decode/command_line.hpp"
#include "../../apps/inspect/command_line.hpp"
#include "../../apps/verify/command_line.hpp"
#include "construct/runtime/command_line.hpp"
#include "construct/selection/alignment_search.hpp"
#include "support/resources/platform_snapshot.hpp"
#endif

namespace cli = sic::support::cli;
namespace apps = sic::apps;
namespace {
void check(bool condition, const std::string& detail) {
  if (!condition) throw std::runtime_error(detail);
}
template <class Parse>
auto invoke(Parse parse, std::vector<std::string> words) {
  std::vector<char*> argv;
  for (auto& word : words) argv.push_back(word.data());
  return parse(static_cast<int>(argv.size()), argv.data());
}
std::string help_default(const cli::Parser& parser, const std::string& option) {
  const auto help = parser.help();
  const auto first = help.find("\n  " + option + " ");
  check(first != std::string::npos, "missing option " + option);
  const auto line = help.substr(first, help.find('\n', first + 1) - first);
  const auto start = line.find("(default ");
  check(start != std::string::npos, "missing default for " + option);
  const auto value = start + std::string("(default ").size();
  return line.substr(value, line.find_first_of(" ;)\n", value) - value);
}
template <class T>
void matches(const cli::Parser& parser, const std::string& option, T actual) {
  const auto text = help_default(parser, option);
  const auto displayed = cli::number<T>(text, option);
  check(displayed == actual, option + ": displayed " + text + " disagrees with omitted option");
}
}  // namespace
int main(int argc, char** argv) {
  check(argc == 3, "expected repository root and work directory");
  const std::string repo = argv[1], work = argv[2];
  std::filesystem::remove_all(work);
  std::filesystem::create_directories(work);
  const auto overlaps = invoke(apps::overlaps::parse_command_line, {"tool", "missing"});
  matches(apps::overlaps::command_line_parser(), "--tol", overlaps.tol);
  const auto identify =
      invoke(apps::identify::parse_command_line, {"tool", "missing", "--repo", repo});
  matches(apps::identify::command_line_parser(), "--tol", identify.z_tol);
  matches(apps::identify::command_line_parser(), "--entry-error", identify.entry_error);
  matches(apps::identify::command_line_parser(), "--moments", identify.moments);
  const auto catalogue = help_default(apps::identify::command_line_parser(), "--catalogue");
  check(catalogue.starts_with("REPO/"), "catalogue default is relative to repository");
  check(identify.catalogue == identify.root + "/" + catalogue.substr(5),
        "identify catalogue default");
  for (const auto& value : {"1", "64"}) {
    const auto boundary = invoke(apps::identify::parse_command_line,
                                 {"tool", "missing", "--repo", repo, "--moments", value});
    check(boundary.moments == std::stoi(value), "moment boundary changed");
  }
  for (const auto& value : {"0", "65"}) {
    bool refused = false;
    try {
      (void)invoke(apps::identify::parse_command_line,
                   {"tool", "missing", "--repo", repo, "--moments", value});
    } catch (const cli::UsageError&) {
      refused = true;
    }
    check(refused, "moment boundary refusal changed");
  }
  const auto stabilizer =
      invoke(apps::stabilizer::parse_command_line, {"tool", "missing", "--repo", repo});
  matches(apps::stabilizer::command_line_parser(), "--tol", stabilizer.o.z_tol);
  matches(apps::stabilizer::command_line_parser(), "--entry-error", stabilizer.o.entry_error);
  check(stabilizer.catalogue == stabilizer.root + "/" + catalogue.substr(5),
        "stabilizer catalogue default");
  const auto file_stabilizer = invoke(apps::stabilizer::parse_command_line, {"tool", "missing"});
  check(apps::stabilizer::command_line_parser().help().find(
            "file mode default \"" + file_stabilizer.root + "\".") != std::string::npos,
        "stabilizer file-mode repository default");
  check(help_default(apps::stabilizer::command_line_parser(), "--catalogue") == catalogue,
        "catalogue help defaults disagree");
#ifdef SIC_HAVE_FLINT
  const auto verify = invoke(apps::verify::parse_command_line, {"tool", "missing"});
  matches(apps::verify::command_line_parser(), "--threads", verify.threads);
  matches(apps::verify::command_line_parser(), "--maximum-precision",
          verify.verification_policy().precision.maximum_bits);
  const auto inspect =
      invoke(apps::inspect::parse_command_line, {"tool", "missing", "--text-json", "-"});
  matches(apps::inspect::command_line_parser(), "--threads", inspect.text_options().threads);
  const auto release = invoke(apps::inspect::parse_command_line,
                              {"tool", "missing", "--release-layout", work, "--multiplet", "1"});
  check(apps::inspect::command_line_parser().help().find(
            "assets directory (default \"" + release.release_options().assets_dir.string() +
            "\": hash only)") != std::string::npos,
        "inspect assets-directory default");
  // Constructor validation only: no worker pool or decode is started here.
  const auto decode =
      invoke(apps::decode::parse_command_line, {"tool", "missing", work, "--threads", "1"});
  const char* job_value = std::getenv("SLURM_CPUS_PER_TASK");
  const bool had_job_value = job_value != nullptr;
  const std::string saved_job_value = job_value ? job_value : "";
  check(setenv("SLURM_CPUS_PER_TASK", "malformed-cpu-allocation", 1) == 0, "set scheduler control");
  const auto explicit_decode =
      invoke(apps::decode::parse_command_line, {"tool", "missing", work, "--threads", "1"});
  check(explicit_decode.options.threads == 1, "explicit workers must bypass CPU-default discovery");
  check((had_job_value ? setenv("SLURM_CPUS_PER_TASK", saved_job_value.c_str(), 1)
                       : unsetenv("SLURM_CPUS_PER_TASK")) == 0,
        "restore scheduler control");
  const auto& decode_help = apps::decode::command_line_parser();
  matches(decode_help, "--maximum-precision", decode.options.maximum_precision);
  matches(decode_help, "--target-bits", decode.options.target);
  matches(decode_help, "--memory-mib", decode.options.memory_budget >> 20);
  matches(decode_help, "--working-bits", decode.working_bits);
  for (const auto& value : {"1", "1048576"}) {
    const auto boundary =
        invoke(apps::decode::parse_command_line,
               {"tool", "missing", work, "--threads", "1", "--working-bits", value});
    check(boundary.working_bits == std::stoull(value), "working-bit boundary changed");
  }
  for (const auto& value : {"0", "1048577"}) {
    bool refused = false;
    try {
      (void)invoke(apps::decode::parse_command_line,
                   {"tool", "missing", work, "--threads", "1", "--working-bits", value});
    } catch (const cli::UsageError&) {
      refused = true;
    }
    check(refused, "working-bit boundary refusal changed");
  }
  check(help_default(decode_help, "--rays") == decode.rays_arg, "ray selection default");
  check(help_default(decode_help, "--coordinates") == decode.coords_arg, "coordinate default");
  check(help_default(decode_help, "--tower") == decode.tower, "tower default");
  check(decode.options.delta == -1 && decode.options.base_bits == -1, "automatic PREC hints");
  check(decode_help.help().find("PREC hint or " +
                                cli::default_text(sic::decode::kDefaultGuardBits)) !=
                std::string::npos &&
            decode_help.help().find("PREC hint or min(" +
                                    cli::default_text(sic::decode::kDefaultInitialPrecisionCap)) !=
                std::string::npos,
        "decoder help describes library-owned PREC fallbacks");
  const auto container = invoke(apps::container::parse_command_line,
                                {"tool", "reencode", "missing", work, "--threads", "1"});
  matches(apps::container::command_line_parser("reencode"), "--maximum-precision",
          container.reencode_policy().precision.maximum_bits);
  const auto reencode =
      invoke(apps::container::parse_command_line, {"tool", "reencode", "missing", work});
  check(apps::container::command_line_parser("reencode")
                .help()
                .find("reencode: default " + cli::default_text(reencode.reencode_options().jobs) +
                      ".") != std::string::npos,
        "reencode thread default");
  const auto automatic_decode = invoke(apps::decode::parse_command_line, {"tool", "missing", work});
  const auto decode_usage = decode_help.help();
  const auto cap_start = decode_usage.find("capped at ") + std::string("capped at ").size();
  const auto cap = cli::number<unsigned>(
      decode_usage.substr(cap_start, decode_usage.find(')', cap_start) - cap_start), "thread cap");
  check(automatic_decode.options.threads ==
            std::min<sic::support::resources::Count>(
                cap, sic::support::resources::process_cpu_allowance()),
        "decoder process allowance/default ceiling disagrees with help");
  const auto& transfer_help = apps::container::command_line_parser("fetch");
  const auto saved_directory = std::filesystem::current_path();
  const auto transfer_work = std::filesystem::absolute(work) / "transfer";
  std::filesystem::create_directories(transfer_work);
  sic::transport::SplitOptions split;
  split.file = repo + "/cpp/tests/fixtures/sicx/d4.sicx";
  auto parts_default = help_default(transfer_help, "--parts");
  check(parts_default.starts_with("DATA_ROOT/SPEC/"), "parts default root/spec policy");
  parts_default.replace(0, std::string("DATA_ROOT").size(),
                        help_default(transfer_help, "--data-root"));
  parts_default.replace(parts_default.find("SPEC"), std::string("SPEC").size(), "d4/m1");
  split.parts_path = transfer_work / parts_default;
  split.prefix = "d4-m1";
  std::filesystem::create_directories(split.parts_path.parent_path());
  (void)sic::transport::split(split);
  std::filesystem::current_path(transfer_work);
  for (const std::string command : {"fetch", "repair"}) {
    auto transfer =
        invoke(apps::container::parse_command_line, {"tool", command, "d4/m1", "--out", "output"});
    const auto job = command == "fetch" ? transfer.fetch_options().job : transfer.repair_job();
    matches(transfer_help, "--threads", job.jobs);
    check(help_default(transfer_help, "--name") == job.name, "transfer name default");
  }
  for (const std::string command : {"fetch", "repair"}) {
    auto transfer =
        invoke(apps::container::parse_command_line, {"tool", command, "d4/m1", "--threads", "1"});
    const auto job = command == "fetch" ? transfer.fetch_options().job : transfer.repair_job();
    const auto usage = transfer_help.help();
    check(usage.find("default " + job.out_dir.parent_path().parent_path().string() + "/SPEC") !=
              std::string::npos,
          "transfer output-directory default");
    check(job.out_dir.filename() == "m1" && job.out_dir.parent_path().filename() == "d4",
          "transfer output-directory spec suffix");
  }
  std::filesystem::current_path(saved_directory);
  const auto remote = sic::access::SourceArguments::parser(cli::Parser{"tool"});
  const sic::access::RemoteOptions remote_options;
  matches(remote, "--range-jobs", remote_options.jobs);
  matches(remote, "--range-bytes", remote_options.range_bytes);
  matches(remote, "--coalesce-gap", remote_options.coalesce_gap);
  matches(remote, "--prefetch-mib", remote_options.prefetch_bytes >> 20);
  matches(remote, "--cache-max-ranges", remote_options.max_ranges);
  matches(remote, "--remote-max-fraction", remote_options.max_fraction);
  matches(remote, "--remote-timeout-ms", remote_options.timeout_ms);
  matches(remote, "--remote-retries", remote_options.retries);
  auto producer = invoke(
      [](int count, char** words) { return sic::construct::ProducerCommandLine(count, words); },
      {"tool", "4", "--out", work + "/not-created.sicx", "--threads", "1"});
  const auto producer_help = sic::construct::ProducerCommandLine::parser();
  check(producer_help.help().find("(default S=1.." +
                                  cli::default_text(producer.policy.dimension.maximum_exponent) +
                                  ";") != std::string::npos,
        "positional exponent allowance default");
  matches(producer_help, "--maximum-exponent", producer.policy.dimension.maximum_exponent);
  matches(producer_help, "--maximum-precision", producer.policy.precision.maximum_bits);
  matches(producer_help, "--valuation-table-mib", producer.policy.valuation.table_bytes >> 20);
  matches(producer_help, "--valuation-total-mib", producer.policy.valuation.total_bytes >> 20);
  matches(producer_help, "--alignment-memory-mib", producer.alignment.memory_bytes >> 20);
  matches(producer_help, "--alignment-batch-mib", producer.alignment.batch_bytes >> 20);
  matches(producer_help, "--alignment-worker-mib", producer.alignment.worker_bytes >> 20);
  matches(producer_help, "--alignment-train", producer.alignment.train);
  matches(producer_help, "--alignment-candidates", producer.alignment.candidates);
  matches(producer_help, "--alignment-sweeps", producer.alignment.sweeps);
  matches(producer_help, "--alignment-seed", producer.alignment.seed);
  check(help_default(producer_help, "--generator-selector") == producer.alignment.selector,
        "generator selector default");
  const sic::construct::alignment::AlignInput search;
  check(search.seed == producer.alignment.seed && search.train == producer.alignment.train &&
            search.cand == producer.alignment.candidates &&
            search.sweeps == producer.alignment.sweeps &&
            search.batch_bytes == producer.alignment.batch_bytes &&
            search.worker_bytes == producer.alignment.worker_bytes,
        "alignment parser/model/search defaults disagree");
#endif
  std::cout << "PASS help values equal actual omitted-option parser/model defaults\n";
}
