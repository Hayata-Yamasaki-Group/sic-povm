// SPDX-License-Identifier: AGPL-3.0-or-later
#include "command_line.hpp"
namespace sic::apps::inspect {
namespace {
using sic::support::cli::default_text;
constexpr unsigned kDefaultThreads = 1;
constexpr uint64_t kDefaultPartBytes = 0;
const sic::data::ReleaseOptions kReleaseDefaults;
}  // namespace

const sic::support::cli::Parser& command_line_parser() {
  static const sic::support::cli::Parser parser{
      "sic-inspect FILE MODE [OPTIONS] | sic-inspect --exact-json IN.sicx OUT.json [OPTIONS] | "
      "sic-inspect --catalogue ROOT",
      {
          {"--text-json", "OUT|-", "Dump canonical JSON Lines.", 1, 1, false},
          {"--part-bytes", "N",
           "text-json: positive part byte limit (default " + default_text(kDefaultPartBytes) +
               ": unsplit).",
           1, 1, false},
          {"--threads", "T",
           "text-json: positive worker count (default " + default_text(kDefaultThreads) + ").", 1,
           1, false},
          {"--json-rays", "DIR", "Write per-ray JSON; requires --multiplet mN.", 1, 1, false},
          {"--json-all", "OUT", "Write all-in-one JSON; requires --multiplet mN.", 1, 1, false},
          {"--exact-json", "", "Write exact.json; two positional paths IN and OUT.", 0, 0, false},
          {"--multiplet", "N|mN",
           "json-rays/json-all/exact-json: required mN; release-layout: positive N.", 1, 1, false},
          {"--no-check", "", "exact-json: skip numerical check.", 0, 0, false},
          {"--block", "I", "Write text-form chunk at nonnegative index I.", 1, 1, false},
          {"--entry", "I", "Write index entry and audit path at nonnegative index I.", 1, 1, false},
          {"--release-layout", "ROOT", "Write release tree.", 1, 1, false},
          {"--provenance-conductors", "LIST", "release-layout: provenance conductor list.", 1, 1,
           false},
          {"--provenance-conductor", "C", "release-layout: chosen conductor.", 1, 1, false},
          {"--assets-dir", "DIR",
           "release-layout: assets directory (default \"" +
               default_text(kReleaseDefaults.assets_dir.string()) + "\": hash only).",
           1, 1, false},
          {"--release-repo", "OWNER/REPO",
           "release-layout: repository (default " + default_text(kReleaseDefaults.repository) +
               ").",
           1, 1, false},
          {"--release-tag", "TAG",
           "release-layout: tag (default " + default_text(kReleaseDefaults.tag) + ").", 1, 1,
           false},
          {"--part-size", "N",
           "release-layout: positive part byte count (default " +
               default_text(kReleaseDefaults.part_size) + ").",
           1, 1, false},
          {"--catalogue", "ROOT", "Rebuild catalogue.json.", 1, 1, false},
          {"--info", "", "Show header, EXMT and index.", 0, 0, false},
      }};
  return parser;
}
Invocation parse_command_line(int argc, char** argv) {
  return Invocation(command_line_parser().parse(argc, argv));
}
}  // namespace sic::apps::inspect

#include <sstream>
namespace sic::apps::inspect {

Invocation::Invocation(sic::support::cli::Arguments arguments) : arguments_(std::move(arguments)) {
  unsigned modes = 0;
  for (const auto* mode_option :
       {"--text-json", "--json-rays", "--json-all", "--exact-json", "--block", "--entry",
        "--release-layout", "--catalogue", "--info"})
    modes += arguments_.has(mode_option);
  sic::support::cli::require(modes == 1, "specify exactly one output mode");
  arguments_.positionals(arguments_.has("--catalogue")    ? 0
                         : arguments_.has("--exact-json") ? 2
                                                          : 1,
                         arguments_.has("--catalogue")    ? 0
                         : arguments_.has("--exact-json") ? 2
                                                          : 1);
  file = arguments_.pos.empty() ? "" : arguments_.pos[0];

  if (arguments_.has("--text-json")) mode = Mode::text_json;
  if (arguments_.has("--json-rays")) mode = Mode::json_rays;
  if (arguments_.has("--json-all")) mode = Mode::json_all;
  if (arguments_.has("--exact-json")) mode = Mode::exact_json;
  if (arguments_.has("--block")) mode = Mode::block;
  if (arguments_.has("--entry")) mode = Mode::entry;
  if (arguments_.has("--release-layout")) mode = Mode::release_layout;
  if (arguments_.has("--catalogue")) mode = Mode::catalogue;
  if (arguments_.has("--info")) mode = Mode::info;
}
TextOptions Invocation::text_options() const {
  arguments_.only({"--text-json", "--part-bytes", "--threads"});
  const uint64_t part_bytes = arguments_.numeric<uint64_t>("--part-bytes", kDefaultPartBytes, 1);
  const unsigned threads = arguments_.numeric<unsigned>("--threads", kDefaultThreads, 1);

  return {part_bytes, threads};
}

std::string Invocation::json_multiplet() const {
  arguments_.only({"--json-rays", "--json-all", "--multiplet"});
  const std::string multiplet = arguments_.get("--multiplet");
  try {
    sic::sicx::require_multiplet(multiplet);
  } catch (const sic::Error& error) {
    throw sic::support::cli::UsageError(std::string("JSON modes require --multiplet mN: ") +
                                        error.what());
  }

  return multiplet;
}

ExactOptions Invocation::exact_options() const {
  arguments_.only({"--exact-json", "--multiplet", "--no-check"});
  std::string in_path = file, out_path = arguments_.pos[1];
  sic::sicx::exact::BuildOptions options;
  options.numeric_check = !arguments_.has("--no-check");
  if (arguments_.has("--multiplet")) {
    options.multiplet = arguments_.get("--multiplet");
    sic::support::cli::require(options.multiplet.starts_with("m"),
                               "--multiplet must be mN with N >= 1");
    (void)sic::support::cli::number<uint64_t>(std::string_view(options.multiplet).substr(1),
                                              "--multiplet", 1);
  }
  sic::sicx::require_multiplet(options.multiplet);

  return {std::move(in_path), std::move(out_path), std::move(options)};
}

size_t Invocation::chunk_index() const {
  arguments_.only({"--block", "--entry"});
  const size_t i = sic::support::cli::number<size_t>(
      arguments_.get(arguments_.has("--block") ? "--block" : "--entry"), "chunk index");

  return i;
}

sic::data::ReleaseOptions Invocation::release_options() const {
  arguments_.only({"--release-layout", "--multiplet", "--provenance-conductors",
                   "--provenance-conductor", "--assets-dir", "--release-repo", "--release-tag",
                   "--part-size"});
  sic::data::ReleaseOptions opt;
  opt.threads = 8;
  opt.multiplet = arguments_.numeric<uint64_t>("--multiplet", 0, 1);
  const std::string all = arguments_.get("--provenance-conductors"),
                    one = arguments_.get("--provenance-conductor");
  opt.assets_dir = arguments_.get("--assets-dir", kReleaseDefaults.assets_dir.string());
  opt.repository = arguments_.get("--release-repo", kReleaseDefaults.repository);
  opt.tag = arguments_.get("--release-tag", kReleaseDefaults.tag);
  opt.part_size = arguments_.numeric<uint64_t>("--part-size", kReleaseDefaults.part_size, 1);
  if (arguments_.has("--provenance-conductors") || arguments_.has("--provenance-conductor")) {
    if (opt.multiplet)
      throw sic::support::cli::UsageError("use --multiplet or the provenance options, not both");
    sic::support::cli::require(!all.empty() && all.back() != ',' && !one.empty(),
                               "both provenance conductor options are required");
    const uint64_t conductor =
        sic::support::cli::number<uint64_t>(one, "--provenance-conductor", 1);
    bool found = false;
    std::stringstream list(all);
    for (std::string value; std::getline(list, value, ',');)
      found =
          (sic::support::cli::number<uint64_t>(value, "--provenance-conductors", 1) == conductor) ||
          found;
    sic::support::cli::require(found, "the provenance conductor is not in the provenance list");
    opt.multiplet = sic::data::multiplet_from_provenance(all, one);
  }
  sic::support::cli::require(
      opt.multiplet >= 1, "release-layout needs --multiplet or the provenance conductor options");

  return opt;
}

std::filesystem::path Invocation::catalogue_root() const {
  arguments_.only({"--catalogue"});
  const std::filesystem::path root = arguments_.get("--catalogue");

  return root;
}

void Invocation::validate_info() const { arguments_.only({"--info"}); }

std::string Invocation::text_output() const { return arguments_.get("--text-json"); }

std::string Invocation::all_json_output() const { return arguments_.get("--json-all"); }

std::string Invocation::rays_directory() const { return arguments_.get("--json-rays"); }

std::string Invocation::release_root() const { return arguments_.get("--release-layout"); }

void Invocation::validate_chunk_index(size_t index, size_t count) const {
  if (index >= count) throw sic::support::cli::UsageError("chunk index out of range");
}
}  // namespace sic::apps::inspect
