// SPDX-License-Identifier: AGPL-3.0-or-later
#include "command_line.hpp"

#include <flint/flint.h>

#include "access/source_arguments.hpp"
namespace sic::apps::container {
namespace {
using sic::support::cli::default_text;
constexpr unsigned kDefaultTransferThreads = 4;
constexpr unsigned kDefaultReencodeThreads = 1;
constexpr const char* kDefaultOutputRoot = "build/data";
constexpr const char* kDefaultPartsFilename = "multiplet.sicx.parts.json";
constexpr uint64_t kDefaultExtraBytes = 0;
constexpr unsigned kMinimumThreads = 1;
constexpr unsigned kMaximumThreads = 64;
const sic::transport::SplitOptions kSplitDefaults;
}  // namespace

const sic::support::cli::Parser& command_line_parser(std::string_view command) {
  static const std::initializer_list<sic::support::cli::Option> options = {

      {"--out", "DIR",
       "split: required output directory; fetch/repair: default " +
           default_text(kDefaultOutputRoot) + "/SPEC.",
       1, 1, false},
      {"--asset-prefix", "PREFIX", "split: d<d>-m<N> asset prefix.", 1, 1, false},
      {"--repository", "OWNER/REPO",
       "split: repository for the parts manifest (default " +
           default_text(kSplitDefaults.repository) + ").",
       1, 1, false},
      {"--tag", "TAG", "split: release tag (default " + default_text(kSplitDefaults.tag) + ").", 1,
       1, false},
      {"--emit-and-upload", "CMD", "split: upload command.", 1, 1, false},
      {"--confirm-size-cmd", "CMD",
       "split: required remote-size confirmation command with --emit-and-upload.", 1, 1, false},
      {"--part-size", "N",
       "split: fixed ceiling (must be " + default_text(sic::transport::kPartSize) + ").", 1, 1,
       false},
      {"--source", "URL|DIR", "fetch/repair/read-range: source override.", 1, 1, false},
      {"--maximum-precision", "BITS",
       "reencode: computational precision ceiling (default " +
           default_text(sic::kDefaultMaximumPrecision) + "; " +
           default_text(sic::kMinimumToolPrecision) + ".." +
           default_text(sic::kNativeMaximumPrecision) + ").",
       1, 1, false},
      {"--threads", "J",
       "fetch/repair/reencode: " + default_text(kMinimumThreads) + ".." +
           default_text(kMaximumThreads) + " workers; fetch/repair (default " +
           default_text(kDefaultTransferThreads) + "); reencode: default " +
           default_text(kDefaultReencodeThreads) + ".",
       1, 1, false},
      {"--data-root", "DIR", "fetch/repair: explicit manifest root; alternative to --parts.", 1, 1,
       false},
      {"--parts", "FILE",
       "fetch/repair: explicit parts manifest; with --data-root use DIR/SPEC/" +
           default_text(kDefaultPartsFilename) + ".",
       1, 1, false},
      {"--name", "NAME",
       "fetch/repair: output name (default " + default_text(sic::transport::kLogicalName) + ").", 1,
       1, false},
      {"--extra-bytes", "N",
       "fetch: extra required disk bytes (default " + default_text(kDefaultExtraBytes) + ").", 1, 1,
       false},
      {"--force", "", "fetch: replace a differing file.", 0, 0, false},

  };
  static const sic::support::cli::Parser parser =
      sic::access::SourceArguments::parser(sic::support::cli::Parser{
          "sic-container split FILE --out DIR --asset-prefix d<d>-m<N> [OPTIONS]\n       "
          "sic-container "
          "fetch|repair "
          "d<d>/m<N> [OPTIONS]\n       sic-container from-text TEXT.jsonl|TEXT.parts.json "
          "OUT.sicx\n     "
          "  sic-container "
          "merge-parts|merge-element-parts OUT.sicx PART...\n       sic-container reencode IN.sicx "
          "OUT.sicx "
          "[--threads N] [--maximum-precision BITS]\n       sic-container read-range FILE|--remote "
          "PARTS.json OFFSET LENGTH [OPTIONS]",
          options});
  static const std::map<std::string_view, sic::support::cli::Parser> commands = {
      {"split",
       sic::support::cli::Parser{"sic-container split FILE --out DIR --asset-prefix d<d>-m<N> "
                                 "[--emit-and-upload CMD --confirm-size-cmd CMD] [OPTIONS]",
                                 options}
           .select({"--out", "--asset-prefix", "--repository", "--tag", "--emit-and-upload",
                    "--confirm-size-cmd", "--part-size"})},
      {"fetch",
       sic::support::cli::Parser{"sic-container fetch d<d>/m<N> [OPTIONS]", options}.select(
           {"--source", "--threads", "--data-root", "--parts", "--out", "--name", "--extra-bytes",
            "--force"})},
      {"repair",
       sic::support::cli::Parser{"sic-container repair d<d>/m<N> [OPTIONS]", options}.select(
           {"--source", "--threads", "--data-root", "--parts", "--out", "--name"})},
      {"read-range",
       sic::access::SourceArguments::parser(sic::support::cli::Parser{
           "sic-container read-range FILE|--remote PARTS.json OFFSET LENGTH [OPTIONS]"})},
      {"from-text",
       sic::support::cli::Parser{"sic-container from-text TEXT.jsonl|TEXT.parts.json OUT.sicx"}},
      {"merge-parts", sic::support::cli::Parser{"sic-container merge-parts OUT.sicx PART..."}},
      {"merge-element-parts",
       sic::support::cli::Parser{"sic-container merge-element-parts OUT.sicx PART..."}},
      {"reencode",
       sic::support::cli::Parser{"sic-container reencode IN.sicx OUT.sicx [OPTIONS]", options}
           .select({"--threads", "--maximum-precision"})},
  };
  const auto it = commands.find(command);
  return it == commands.end() ? parser : it->second;
}
Invocation parse_command_line(int argc, char** argv) {
  const std::string_view command = argc >= 2 ? argv[1] : "";
  const auto& parser = command_line_parser(command);
  if (argc == 2 && command == "--help") throw sic::support::cli::HelpRequested{};
  static const std::map<std::string_view, Mode> modes{
      {"split", Mode::split},
      {"fetch", Mode::fetch},
      {"repair", Mode::repair},
      {"read-range", Mode::read_range},
      {"from-text", Mode::from_text},
      {"merge-parts", Mode::merge_parts},
      {"merge-element-parts", Mode::merge_element_parts},
      {"reencode", Mode::reencode}};
  const auto selected = modes.find(command);
  sic::support::cli::require(selected != modes.end(), "expected a sic-container subcommand");
  if (selected->second == Mode::from_text || selected->second == Mode::merge_parts ||
      selected->second == Mode::merge_element_parts)
    flint_set_num_threads(1);
  return {selected->second, parser.parse(argc, argv, 2)};
}

}  // namespace sic::apps::container

#include "transport/fetch.hpp"
#include "transport/repair.hpp"
namespace sic::apps::container {

namespace filesystem = std::filesystem;
using Args = sic::support::cli::Arguments;
sic::transport::TransferJob setup(const Args& a, std::string_view extra_bytes) try {
  if (a.pos.size() != 1)
    throw sic::support::cli::UsageError("expected one multiplet argument d<d>/m<N>");
  const std::string spec = a.pos[0];
  const size_t slash = spec.find('/');
  if (slash == std::string::npos ||
      !sic::transport::valid_prefix(spec.substr(0, slash) + "-" + spec.substr(slash + 1)))
    throw sic::support::cli::UsageError("multiplet must look like d1024/m1");
  (void)a.numeric<unsigned>("--threads", kDefaultTransferThreads, kMinimumThreads, kMaximumThreads);
  (void)sic::support::cli::number<uint64_t>(extra_bytes, "--extra-bytes");
  sic::support::cli::require(a.get("--parts").empty() != a.get("--data-root").empty(),
                             "specify exactly one of --parts FILE or --data-root DIR");
  const filesystem::path parts_path =
      !a.get("--parts").empty()
          ? filesystem::path(a.get("--parts"))
          : filesystem::path(a.get("--data-root")) / spec / kDefaultPartsFilename;
  sic::transport::TransferJob j;
  j.parts = sic::transport::parse_parts(sic::transport::read_text(parts_path));
  const std::string want = spec.substr(0, slash) + "-" + spec.substr(slash + 1);
  if (j.parts.parts[0].asset != want + ".sicx.part-0000")
    throw sic::Error(sic::ErrorKind::integrity,
                     "the parts file is for " + j.parts.parts[0].asset + ", not " + want);
  j.out_dir = a.get("--out", (filesystem::path(kDefaultOutputRoot) / spec).string());
  j.name = a.get("--name", sic::transport::kLogicalName);
  if (j.name.empty() || j.name.find_first_of("/\\") != std::string::npos || j.name == "." ||
      j.name == "..")
    throw sic::support::cli::UsageError("bad --name");
  j.source = a.get("--source");
  const std::string jobs = a.get("--threads", default_text(kDefaultTransferThreads));
  j.jobs = sic::support::cli::number<unsigned>(jobs, "--threads", kMinimumThreads, kMaximumThreads);
  if (j.jobs == 0) throw sic::support::cli::UsageError("--threads must be positive");
  filesystem::create_directories(j.out_dir);
  j.final_path = j.out_dir / j.name;
  return j;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
sic::transport::SplitOptions Invocation::split_options() {
  auto& a = arguments_;

  if (a.pos.size() != 1 || a.get("--out").empty() || a.get("--asset-prefix").empty())
    throw sic::support::cli::UsageError(
        "usage: sic-container split FILE --out DIR --asset-prefix d<d>-m<N> [--repository R] "
        "[--tag T] "
        "[--emit-and-upload CMD --confirm-size-cmd CMD]");
  sic::transport::SplitOptions o;
  o.file = a.pos[0];
  o.assets_dir = a.get("--out");
  o.parts_path = o.assets_dir / kDefaultPartsFilename;
  o.prefix = a.get("--asset-prefix");
  sic::support::cli::require(sic::transport::valid_prefix(o.prefix),
                             "--asset-prefix must look like d<d>-m<N>");
  o.repository = a.get("--repository", kSplitDefaults.repository);
  o.tag = a.get("--tag", kSplitDefaults.tag);
  o.upload_cmd = a.get("--emit-and-upload");
  o.confirm_cmd = a.get("--confirm-size-cmd");
  sic::support::cli::require(!o.upload_cmd.empty() == !o.confirm_cmd.empty(),
                             "--emit-and-upload and --confirm-size-cmd must be used together");
  if (a.has("--part-size")) {
    o.part_size = sic::support::cli::number<uint64_t>(a.get("--part-size"), "--part-size", 1);
    if (o.part_size != sic::transport::kPartSize)
      throw sic::support::cli::UsageError("the SICX 1.0 part ceiling is fixed at " +
                                          default_text(sic::transport::kPartSize));
  }

  return o;
}
sic::transport::FetchOptions Invocation::fetch_options() {
  auto& a = arguments_;

  sic::transport::TransferJob j =
      setup(a, a.get("--extra-bytes", default_text(kDefaultExtraBytes)));
  constexpr uint64_t kDiskReservationMargin = 64u << 20;
  const uint64_t flight = sic::transport::inflight(j);
  sic::support::cli::require(flight <= UINT64_MAX - kDiskReservationMargin &&
                                 j.parts.size <= UINT64_MAX - kDiskReservationMargin - flight,
                             "disk reservation exceeds the byte-count range");
  const uint64_t extra =
      a.numeric<uint64_t>("--extra-bytes", kDefaultExtraBytes, 0,
                          UINT64_MAX - j.parts.size - flight - kDiskReservationMargin);

  return {std::move(j), extra, a.has("--force")};
}
sic::transport::TransferJob Invocation::repair_job() {
  return setup(arguments_, default_text(kDefaultExtraBytes));
}
sic::support::ByteRange Invocation::range() const {
  const uint64_t offset = sic::support::cli::number<uint64_t>(arguments_.pos[1], "OFFSET"),
                 length = sic::support::cli::number<uint64_t>(arguments_.pos[2], "LENGTH");
  const sic::support::ByteRange range{offset, length};

  return range;
}

Invocation::Invocation(Mode selected, sic::support::cli::Arguments arguments)
    : arguments_(std::move(arguments)), mode(selected) {}
const std::vector<std::string>& Invocation::from_text_paths() const {
  arguments_.positionals(2, 2);
  return arguments_.pos;
}
const std::vector<std::string>& Invocation::merge_paths() const {
  arguments_.positionals(2, SIZE_MAX);
  return arguments_.pos;
}
RangeInput::RangeInput(sic::support::cli::Arguments& arguments)
    : input(arguments), path((arguments.positionals(3, 3), arguments.pos[0])) {}
RangeInput Invocation::range_input() { return RangeInput(arguments_); }
ReencodeOptions Invocation::reencode_options() const {
  arguments_.positionals(2, 2);
  const auto jobs = sic::support::cli::number<unsigned>(
      arguments_.get("--threads", default_text(kDefaultReencodeThreads)), "--threads",
      kMinimumThreads, kMaximumThreads);

  return {arguments_.pos[0], arguments_.pos[1], jobs};
}
sic::sicx::VerificationPolicy Invocation::reencode_policy() const {
  sic::sicx::VerificationPolicy policy;
  policy.precision.maximum_bits =
      arguments_.numeric<long>("--maximum-precision", sic::kDefaultMaximumPrecision,
                               sic::kMinimumToolPrecision, sic::kNativeMaximumPrecision);

  return policy;
}
}  // namespace sic::apps::container
