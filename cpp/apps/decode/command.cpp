// SPDX-License-Identifier: AGPL-3.0-or-later
#include "access/source_arguments.hpp"
#include "data/decoded_output.hpp"
#include "decode/kernels/kernels.hpp"
#include "sicx/container/directory.hpp"
#include "support/resources/numa_policy.hpp"
#include "support/sha256.hpp"
// sic-decode FILE.sicx OUTDIR [--rays all|L1,L2,...] [--coordinates all|A-B]
//                 [--target-bits BITS] [--guard-bits BITS] [--initial-bits BITS] [--threads T]
//                 [--memory-mib M] [--no-pin] [--ray LABEL WORD ANCHOR]...
//                 [--tower auto|resident|stream] [--mem-policy bind:N|preferred:N|interleave]
//                 [--check-budget] [--profile]
// sic-decode FILE.sicx OUTDIR --finalize --ray LABEL WORD ANCHOR... [--working-bits B]
// [--resume]
//
// Decoder version: sic::sicx::kDecoderVersion (printed as "DECODER ..." and
// written to OUTDIR/decode.provenance.json).
// Certified decoding of the exact data of a SICX file (docs/sicx.md
// section 11; include/sic/decode/decoder.hpp). For every requested ray and
// coordinate it writes the raw decoded balls; when all d coordinates of a ray
// are requested it also writes the normalized, gauged balls, certified binary64
// values (correct rounding, refused if a ball straddles a rounding boundary)
// and compares them with the ray's certified balls: every coordinate must
// overlap, and the binary64 values must equal the certified rounding of the
// stored record. Any failure exits nonzero (fail-closed).
//
// Rays: a finalized file lists them in RAYS (compared with its BALL chunks). A file
// holding exact data only ("not finalized") is decoded for the rays given with --ray
// (no comparison). --finalize (two-stage completion, docs/sicx.md#58 section 5.4)
// decodes every coordinate of every given ray, constructs BALL by outward rounding
// its own enclosures, certifies containment/FP64 agreement, and appends RAYS, BALL and
// PREC in place; the exact chunks and the exact-part Merkle root are unchanged
// (checked). An interrupted finalization continues with --resume.
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "sic/access/remote.hpp"
#include "sic/decode/decoder.hpp"
#include "sic/sicx/finalize.hpp"
#include "sic/support/cli.hpp"
#include "sicx_decode/build_identity.hpp"

namespace {

// Memory policy for the whole process (inherited by the decoder's threads), without libnuma:
// "bind:N" (MPOL_BIND to node N), "interleave" (all nodes), "preferred:N".

// Resident pages per NUMA node of this process (from /proc/self/numa_maps).

}  // namespace

#include "command.hpp"
#include "command_line.hpp"
namespace sic::apps::decode {
int command(Invocation invocation) try {
  auto& input = invocation.input;
  auto& start = invocation.start;
  auto& file = invocation.file;
  auto& outdir = invocation.outdir;
  auto& options = invocation.options;
  auto& rays_arg = invocation.rays_arg;
  auto& coords_arg = invocation.coords_arg;
  auto& finalize = invocation.finalize;
  auto& resume = invocation.resume;
  auto& check_budget = invocation.check_budget;
  auto& working_bits = invocation.working_bits;
  auto& intervals = invocation.intervals;
  auto& given = invocation.given;
  // A resumed finalization re-decodes a verified reconstruction of the immutable
  // exact prefix. The regenerated payload must match the journal's plan digest.
  struct RecoveredExact {
    std::filesystem::path dir;
    ~RecoveredExact() {
      if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
      }
    }
  } recovered;
  std::filesystem::path decode_file = file;
  if (finalize && sic::sicx::finalization_in_progress(file)) {
    std::string name = file.string() + ".resume-XXXXXX";
    if (!mkdtemp(name.data()))
      throw sic::Error(sic::ErrorKind::io, "cannot create resume reconstruction");
    recovered.dir = name;
    decode_file = recovered.dir / "exact.sicx";
    const auto entries = sic::sicx::StreamWriter::peek_journal(file.string() + ".partial.journal");
    const auto prefix = std::filesystem::exists(file.string() + ".partial")
                            ? std::filesystem::path(file.string() + ".partial")
                            : file;
    sic::sicx::StreamWriter exact(decode_file, sic::sicx::StreamOptions{false, false, {}, false});
    for (size_t i = 0; i < sic::sicx::exact_count_of(entries); ++i) {
      const auto& e = entries[i];
      exact.chunk(e.type_name(), e.flags, sic::sicx::StreamWriter::read_chunk_at(prefix, e),
                  e.object, e.first, e.count);
    }
    exact.finish();
    sic::sicx::Reader check(decode_file);
    (void)sic::sicx::verify_all(check, 1, {{options.maximum_precision}, {}});
    if (check.exact_root() != sic::sicx::interrupted_exact_root(file))
      throw sic::Error(sic::ErrorKind::integrity, "resume reconstruction exact-root mismatch");
  }
  const auto source = input.open(decode_file, options.memory_budget);
  source->bind_cancellation(options.cancellation);
  std::optional<sic::sicx::Finalization> finalization;
  sic::sicx::Meta meta;
  sic::sicx::Digest decoded_exact_root;
  bool finalized = false;
  {
    sic::sicx::Reader probe(source);
    meta = probe.meta();
    decoded_exact_root = probe.exact_root();
    finalized = probe.finalized();
  }
  std::vector<std::vector<sic::sicx::BallPart>> constructed;
  if (finalize) {
    if (finalized) throw sic::Error(sic::ErrorKind::invalid_argument, "file is already finalized");
    meta.rays.clear();
    for (const auto& r : given) {
      if (r.word.size() != meta.smith.size() || r.anchor < 0 ||
          static_cast<uint64_t>(r.anchor) >= meta.d)
        throw sic::support::cli::UsageError("invalid finalization word or anchor");
      meta.rays.push_back({r.label, r.word, static_cast<uint64_t>(r.anchor), 0});
    }
    meta.ball_limbs = static_cast<unsigned>(
        std::min<uint64_t>(sic::sicx::kMaximumBallLimbs, (working_bits + 63) / 64));
    meta.ball_working_bits = working_bits;
    auto labels = meta;
    labels.rays.clear();
    sic::sicx::rays_from_json(sic::sicx::rays_to_json(meta), labels);
    constructed.resize(given.size());
  } else if (!given.empty()) {
    if (finalized)
      throw sic::Error(sic::ErrorKind::invalid_argument,
                       "--ray is for files without RAYS; this file is finalized");
    meta.rays.clear();
    for (const auto& r : given) {
      if (r.word.size() != meta.smith.size())
        throw sic::support::cli::UsageError("ray word width differs from the Smith rank");
      if (r.anchor < 0 || static_cast<uint64_t>(r.anchor) >= meta.d)
        throw sic::support::cli::UsageError(
            "--ray on a non-finalized file needs ANCHOR within the input dimension");
      meta.rays.push_back({r.label, r.word, static_cast<uint64_t>(r.anchor), 0});
    }
  } else if (!finalized) {
    std::cout << "NOT FINALIZED: exact data only (no RAYS/BALL); pass --ray LABEL WORD ANCHOR to "
                 "decode, or --finalize"
              << std::endl;
    return 0;
  }
  const bool compare = finalized || finalize;
  int status = 0;
  {
    sic::sicx::Reader reader(source);
    if (reader.exact_root() != decoded_exact_root)
      throw sic::Error(sic::ErrorKind::integrity, "input exact root changed before decoding");
    std::vector<size_t> rays;
    if (rays_arg == "all")
      for (size_t r = 0; r < meta.rays.size(); ++r) rays.push_back(r);
    else {
      std::stringstream in(rays_arg);
      for (std::string label; std::getline(in, label, ',');) {
        size_t r = 0;
        while (r < meta.rays.size() && meta.rays[r].label != label) ++r;
        if (r == meta.rays.size()) throw sic::support::cli::UsageError("unknown ray " + label);
        rays.push_back(r);
      }
    }
    std::vector<bool> selected(meta.d, coords_arg == "all");
    for (auto [a, b] : intervals) {
      sic::support::cli::require(b < meta.d, "invalid --coordinates for this dimension");
      for (auto m = a; m <= b; ++m) selected[m] = true;
    }
    std::vector<uint64_t> coordinates;
    for (uint64_t m = 0; m < meta.d; ++m)
      if (selected[m]) coordinates.push_back(m);
    sic::support::cli::require(!coordinates.empty(), "empty coordinate selection");
    const uint64_t first = coordinates.front(), last = coordinates.back() + 1;
    const bool contiguous = coordinates.size() == last - first;
    const bool full = coordinates.size() == meta.d;
    if (std::filesystem::exists(outdir) &&
        (!std::filesystem::is_directory(outdir) || !std::filesystem::is_empty(outdir)))
      throw sic::Error(sic::ErrorKind::io,
                       "output directory must be absent or empty: " + outdir.string());
    std::filesystem::create_directories(outdir);
    std::cout << "DECODE PREMISES: field, group, commuting action identities and representative "
                 "kernel fixity are assumed; container verification does not certify them.\n";
    std::vector<sic::decode::Decoder::Request> requests;
    for (auto r : rays)
      for (uint64_t m : coordinates) {
        const auto e = meta.entry(m);
        requests.push_back(
            {e.representative, sic::sicx::words_add(meta.rays[r].word, e.word, meta.smith)});
      }
    // Preflight the decoder's actual payload set before its constructor loads towers.
    auto chunks = sic::decode::Decoder::input_chunks(reader, requests);
    if (full && finalized)
      for (auto r : rays) chunks.push_back(reader.ball_chunk(r));
    reader.plan_chunks(chunks);
    sic::decode::Decoder decoder(reader, options, requests);
    const auto values = decoder.decode(requests);
    const double decode_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    bool ok = true;
    size_t q = 0;
    for (auto r : rays) {
      const auto& ray = meta.rays[r];
      sic::support::ComplexBallVector raw(coordinates.size());
      for (size_t m = 0; m < coordinates.size(); ++m, ++q) acb_set(raw[m], values[q]);
      if (!full) {
        sic::data::write_balls(
            outdir / (ray.label +
                      (contiguous ? ".raw-" + std::to_string(first) + "-" + std::to_string(last - 1)
                                  : ".raw-selected") +
                      ".balls"),
            raw);
        std::cout << "RAY " << ray.label << " raw coordinates "
                  << (contiguous ? std::to_string(first) + ".." + std::to_string(last - 1)
                                 : coords_arg)
                  << " written\n";
        continue;
      }
      sic::data::write_balls(outdir / (ray.label + ".raw.balls"), raw);
      const sic::support::ComplexBallVector w =
          sic::decode::normalize(raw, ray.anchor, options.target + 64);
      sic::data::write_balls(outdir / (ray.label + ".normalized.balls"), w);
      if (!compare) {
        std::cout << "RAY " << ray.label << " decoded (not finalized: no BALL comparison)\n";
        continue;
      }
      std::vector<sic::sicx::BallPart> parts;
      if (finalized)
        parts = reader.ball_parts(r);
      else {
        const unsigned limbs = static_cast<unsigned>(
            std::min<uint64_t>(sic::sicx::kMaximumBallLimbs, (working_bits + 63) / 64));
        for (uint64_t j = 0; j < meta.d; ++j) {
          parts.push_back(sic::sicx::outward_ball_part(acb_realref(w[j]), limbs));
          parts.push_back(sic::sicx::outward_ball_part(acb_imagref(w[j]), limbs));
        }
        constructed[r] = parts;
      }
      const auto comparison =
          sic::data::write_decoded_fp64(outdir / (ray.label + ".fp64.txt"), w, parts, meta.d);
      const auto overlap = comparison.overlap, same64 = comparison.same64;
      const auto worst = comparison.worst;
      const bool pass = overlap == meta.d && same64 == meta.d;
      ok = ok && pass;
      std::cout << "RAY " << ray.label << " overlap_with_"
                << (finalized ? "BALL" : "constructed_BALL") << "=" << overlap << "/" << meta.d
                << " fp64_identical=" << same64 << "/" << meta.d << " max_normalized_radius<=2^"
                << worst << (pass ? " PASS" : " FAIL") << '\n';
    }
    struct rusage usage {};
    getrusage(RUSAGE_SELF, &usage);
    const auto& s = decoder.stats();
    const std::string numa = sic::support::resources::numa_pages();
    {
      // Provenance of the decoded values (not compared across runs: it records the worker count).
      std::ostringstream prov;
      prov << "{\"decoder\":\"" << sic::sicx::kDecoderVersion << "\",\"input\":\""
           << sic::data::json_escape(file.filename().string())
           << "\",\"input_bytes\":" << reader.size() << ",\"exact_root\":\""
           << sic::sicx::hex(reader.exact_root())
           << "\",\"finalized\":" << (finalized ? "true" : "false") << ",\"d\":" << meta.d
           << ",\"rays\":" << rays.size() << ",\"coordinates\":[" << first << "," << last - 1
           << "],\"target_bits\":" << options.target
           << ",\"base_bits\":" << decoder.base_precision() << ",\"passes\":" << s.passes
           << ",\"replays\":" << s.replays << ",\"workers\":" << options.threads;
      if (!contiguous) {
        prov << ",\"coordinate_indices\":[";
        for (size_t j = 0; j < coordinates.size(); ++j) prov << (j ? "," : "") << coordinates[j];
        prov << "]";
      }
      prov << "}\n";
      sic::data::write_decode_provenance(outdir / "decode.provenance.json", prov.str());
    }
    std::cout << "DECODER " << sic::sicx::kDecoderVersion << '\n';
    std::cout << "DECODE d=" << meta.d << " rays=" << rays.size() << " outputs=" << requests.size()
              << " target=" << options.target << " base_bits=" << decoder.base_precision()
              << " threads=" << options.threads << " numa_nodes=" << decoder.placement().nodes
              << " passes=" << s.passes << " replays=" << s.replays << "\n"
              << "TIME wall=" << decode_seconds << " cpu="
              << usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
                     1e-6 * (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec)
              << " tower_load=" << s.seconds_tower_load << " base_path=" << s.seconds_base
              << " tower_eval=" << s.seconds_tower_eval << " roots=" << s.seconds_roots
              << " edges=" << s.seconds_edges << " body=" << s.seconds_body << "\n"
              << "IO tower_bytes=" << s.tower_bytes_read << " body_bytes=" << s.body_bytes_read
              << " file_bytes=" << reader.size() << " peak_rss_kb=" << usage.ru_maxrss << '\n'
              << "STAGES edge_targets=" << s.edge_targets
              << " edge_targets_reused=" << s.edge_targets_reused
              << " generations=" << s.generations << " targets_s=" << s.seconds_edges_targets
              << " labels_s=" << s.seconds_edges_labels << " duals_s=" << s.seconds_edges_duals
              << " plan_s=" << s.seconds_plan << " prune_s=" << s.seconds_prune
              << " rebase_s=" << s.seconds_rebase << '\n'
              << "BUDGET budget_mib=" << (options.memory_budget >> 20)
              << " accounted_peak_mib=" << (s.budget_peak >> 20)
              << " per_worker_task_peak_mib=" << (s.budget_peak_per_worker >> 20)
              << " waits=" << s.budget_waits << " rss_waits=" << s.budget_rss_waits
              << " fallbacks=" << s.budget_fallbacks << " tower_resident=" << s.tower_resident
              << " arenas_capped=" << s.arenas_capped << " peak_rss_mib=" << (usage.ru_maxrss >> 10)
              << (static_cast<uint64_t>(usage.ru_maxrss) * 1024 <= options.memory_budget
                      ? " within_budget"
                      : " OVER_BUDGET")
              << '\n'
              << "NUMA pool_groups=" << decoder.pool_groups()
              << " bind_failures=" << s.bind_failures << " pages" << numa << '\n'
              << (!full      ? "RAW-ONLY (not compared; normalization needs all d coordinates)"
                  : !compare ? "DECODED (exact data only, not finalized)"
                  : ok       ? "PASS"
                             : "FAIL")
              << std::endl;
    status = ok ? 0 : 1;
    if (const auto remote = std::dynamic_pointer_cast<sic::access::RemoteByteSource>(source)) {
      const auto io = remote->stats();
      std::cout << "REMOTE requests=" << io.requests << " transferred=" << io.transferred
                << " cache_bytes=" << io.cache_bytes << " planned_bytes=" << io.planned_bytes
                << " scheduled_bytes=" << io.scheduled_bytes
                << " prefetched_bytes=" << io.prefetched_bytes
                << " coalesced_ranges=" << io.coalesced_ranges << " peak_active=" << io.peak_active
                << " buffer_bound=" << io.buffer_bound << " prefetch_window=" << io.prefetch_window
                << " full_download=" << io.full_download << '\n';
      std::set<int64_t> used;
      for (const auto& request : requests) used.insert(request.representative);
      std::cout << "PARTIAL INPUT: consumed chunks checked against PDIR and LDIR; unread payloads "
                   "are unchecked; global exact canonicality "
                << (used.size() == meta.representative.size() ? "checked" : "deferred")
                << ". Root authentication requires trusted identities.\n";
      std::cout << "ROOT TRUST exact="
                << (input.trusted_exact()  ? "matched supplied pin"
                    : input.trusted_file() ? "transitive via supplied file-root pin"
                                           : "recomputed (manifest trust required)")
                << " file="
                << (input.trusted_file() ? "matched supplied pin"
                                         : "recomputed (manifest trust required)")
                << " whole_file="
                << (io.full_download ? "matched manifest SHA-256"
                                     : "not established by partial decoding")
                << '\n';
    }
    if (check_budget && static_cast<uint64_t>(usage.ru_maxrss) * 1024 > options.memory_budget) {
      std::cout << "BUDGET EXCEEDED: peak RSS above --memory-mib" << std::endl;
      status = 1;
    }
  }
  if (finalize) {
    finalization = sic::sicx::build_finalization(meta, given, constructed, working_bits,
                                                 sic::sicx::kDecoderVersion,
                                                 sic::decode::kDecoderSourceCommit);
    if (status) {
      std::cout << "FINALIZE REFUSED: the decoded rays do not certify BALL" << std::endl;
      return 1;
    }
    const auto [exact_root, root] = sic::sicx::finalize_in_place(
        file, meta, *finalization, resume, decoded_exact_root, {{options.maximum_precision}, {}});
    std::cout << "FINALIZED " << file.string() << " exact_root=" << sic::sicx::hex(exact_root)
              << " merkle=" << sic::sicx::hex(root)
              << " sha256=" << sic::support::sha256::file_hex(file) << std::endl;
  }
  return status;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::apps::decode
