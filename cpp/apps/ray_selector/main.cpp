// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>
#include <fstream>
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
using sic::exact::SmithWord;
using sic::support::Json;
using Clock = std::chrono::steady_clock;

constexpr std::string_view kTool = "sic-ray-selector";
constexpr size_t kWordInputBytes = resources::kMib;
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
      "sic-ray-selector derive|check (--group FILE | --dimension-two) [OPTIONS]",
      {
          {"--group", "FILE", "Read a verified native Group checkpoint.", 1, 1, false},
          {"--sha256", "HEX", "Group SHA-256 pin (default: FILE.sha256).", 1, 1, false},
          {"--dimension-two", "", "Use the dimension-two closed form.", 0, 0, false},
          {"--words", "FILE", "Required in check mode: selector entry JSON.", 1, 1, false},
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

std::string word_text(const SmithWord& word) {
  std::string text;
  for (long digit : word) {
    if (!text.empty()) text += ',';
    text += std::to_string(digit);
  }
  return text;
}

SmithWord parse_word(std::string_view text, size_t rank) {
  SmithWord word;
  for (;;) {
    const auto comma = text.find(',');
    const auto digit = text.substr(0, comma);
    if (word.size() == rank || digit.empty() || (digit.size() > 1 && digit.front() == '0') ||
        digit.find_first_not_of("0123456789") != digit.npos)
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid ray word");
    word.push_back(sic::support::parse_number<long>(digit, "ray word digit",
                                                    sic::ErrorKind::invalid_format, 0));
    if (comma == text.npos) return word;
    text.remove_prefix(comma + 1);
  }
}

std::vector<SmithWord> read_words(const std::string& path, const std::vector<long>& smith,
                                  const resources::Budget& budget, std::string& digest) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw sic::Error(sic::ErrorKind::io, "cannot open selector entry");
  std::string text(kWordInputBytes + 1, '\0');
  stream.read(text.data(), text.size());
  if (stream.bad()) throw sic::Error(sic::ErrorKind::io, "cannot read selector entry");
  text.resize(stream.gcount());
  if (text.size() > kWordInputBytes)
    throw sic::Error(sic::ErrorKind::resource, "selector entry exceeds 1 MiB");
  // Charge the parser's nodes, strings, map entries and transient vector growth.
  // Even an adversarial document has at most one new value per input byte.
  budget.require_additional("selector entry JSON",
                            resources::mul(text.size(), 4 * sizeof(Json) + 128));
  digest = sha256::digest_hex(text);
  const auto entry = sic::support::JsonParser::parse_integer_document(text);
  entry.keys({"smith", "words"}, {"map", "smithSource"});
  const auto& factors = entry.at("smith").as_array();
  for (const auto& factor : factors)
    if (factor.kind != Json::Kind::integer_value)
      throw sic::Error(sic::ErrorKind::invalid_format, "Smith factors must be JSON integers");
  if (entry.at("smith").as_uints<long>() != smith)
    throw sic::Error(sic::ErrorKind::integrity, "selector Smith basis differs from the Group");
  std::vector<SmithWord> words;
  for (const auto& word : entry.at("words").as_array())
    words.push_back(parse_word(word.as_str(), smith.size()));
  return words;
}

Json entry_json(const std::vector<long>& smith, const std::vector<SmithWord>& words) {
  Json entry;
  entry["smith"] = Json::integer_array(smith);
  entry["words"] = Json::array_value();
  for (const auto& word : words)
    entry["words"].array.push_back(Json::string_value(word_text(word)));
  return entry;
}

int64_t elapsed_us(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

int run(const cli::Arguments& args) {
  args.positionals(1, 1);
  const auto& mode = args.pos.front();
  cli::require(mode == "derive" || mode == "check", "expected derive or check");
  const bool check = mode == "check";
  const bool d2 = args.has("--dimension-two");
  cli::require(d2 != args.has("--group"), "specify exactly one of --group or --dimension-two");
  cli::require(!d2 || !args.has("--sha256"), "--sha256 requires --group");
  cli::require(args.has("--words") == check, "--words is required in check mode only");
  cli::require(d2 || !args.get("--group").empty(), "--group must be nonempty");
  cli::require(!check || !args.get("--words").empty(), "--words must be nonempty");
  cli::require(!args.has("--receipt") || !args.get("--receipt").empty(),
               "--receipt must be nonempty");
  if (args.has("--sha256")) {
    const auto pin = args.get("--sha256");
    cli::require(pin.size() == 64 && pin.find_first_not_of("0123456789abcdef") == pin.npos,
                 "--sha256 must contain 64 lowercase hexadecimal digits");
  }
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
  sic::exact::GroupProjection group;
  if (d2) {
    group.exponent = 1;
    group.smith = {2};
  } else {
    group =
        sic::exact::GroupCheckpoint::project(args.get("--group"), args.get("--sha256"), read_ahead);
  }
  const auto select_start = Clock::now();
  const int workers = d2 ? 1 : std::min<long>(threads, group.classes * group.smith.size());
  // Reduced forms, exact ideal witnesses, and worker scratch. No element table.
  const auto arithmetic_bytes =
      d2 ? 0
         : resources::add(resources::mul(group.classes, 4096 + 32 * group.smith.size()),
                          resources::mul(uint64_t{1} << group.exponent, 32768));
  budget.require_additional(
      "ray selection",
      resources::add(arithmetic_bytes,
                     resources::add(workers * resources::kMib, check ? kWordInputBytes + 1 : 0)));
  std::string word_input_digest;
  auto words = check ? read_words(args.get("--words"), group.smith, budget, word_input_digest)
                     : std::vector<SmithWord>{};
  if (d2) {
    const std::vector<SmithWord> identity{{0}};
    if (check && words != identity)
      throw sic::Error(sic::ErrorKind::invalid_format, "dimension two requires exactly word 0");
    words = identity;
  } else {
    const sic::exact::RaySelection selection(group, workers);
    if (!check) words = selection.derive();
    selection.check(words);
    std::sort(words.begin(), words.end());
  }
  const auto output = sic::support::dump_integer_json(entry_json(group.smith, words)) + '\n';
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
