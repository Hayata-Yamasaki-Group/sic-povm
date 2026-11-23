// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <utility>

#include "access/source_arguments.hpp"
#include "data/decoded_output.hpp"
#include "decode/decoder.hpp"
#include "decode/kernels/anchor.hpp"
#include "decode/kernels/kernels.hpp"
#include "decode/model.hpp"
#include "sicx/codec/ball.hpp"
#include "sicx/codec/ray_list.hpp"
#include "sicx/container/directory.hpp"
#include "sicx/container/finalization_builder.hpp"
#include "sicx/io/io.hpp"
#include "sicx/model/metadata.hpp"
#include "support/resources/numa_policy.hpp"
#include "support/resources/platform_snapshot.hpp"
#include "support/sha256.hpp"
// sic-decode FILE.sicx OUTDIR [--rays all|L1,L2,...] [--coordinates all|A-B]
//                 [--target-bits BITS] [--guard-bits BITS] [--initial-bits BITS] [--threads T]
//                 [--memory-mib M] [--no-pin] [--ray LABEL WORD ANCHOR]...
//                 [--tower auto|resident|stream] [--mem-policy bind:N|preferred:N|interleave]
//                 [--check-budget] [--profile]
// sic-decode FILE.sicx OUTDIR --finalize (--ray-list FILE | --ray LABEL WORD ANCHOR...)
// [--working-bits B]
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
#include <unistd.h>

#include "sic/access/remote.hpp"
#include "sic/decode/decoder.hpp"
#include "sic/sicx/finalize.hpp"
#include "sic/support/cli.hpp"
#include "sicx_decode/build_identity.hpp"

#include "command.hpp"
#include "command_line.hpp"
namespace sic::apps::decode {
namespace {
namespace resources = sic::support::resources;
using resources::add;
using resources::mul;

struct FinalizationWorkspace {
  uint64_t ray_parts, encoded, metadata;
  unsigned limbs;

  static FinalizationWorkspace plan(const sic::sicx::Meta& meta, size_t rays,
                                    uint64_t working_bits) {
    const auto limbs = static_cast<unsigned>(
        std::min<uint64_t>(sic::sicx::kMaximumBallLimbs, (working_bits + 63) / 64));
    const auto ray_bytes = mul(meta.d, sic::sicx::ball_record_bytes(limbs));
    sic::sicx::admit_chunk("BALL", ray_bytes);
    return {mul(2 * meta.d, sizeof(sic::sicx::BallPart) + 8 * limbs + 16), ray_bytes,
            mul(rays, mul(add(meta.smith.size(), 8), 8 * sizeof(sic::support::Json))), limbs};
  }

  uint64_t output_bytes(uint64_t dimension, slong bits, slong normalization_bits,
                        uint64_t working_bits) const {
    const auto scalar = sic::decode::acb_bytes(
        std::max({bits, normalization_bits, static_cast<slong>(working_bits)}));
    const auto normalized = mul(dimension, sic::decode::acb_bytes(normalization_bits));
    return add(encoded, add(ray_parts, add(normalized, mul(128, scalar))));
  }
};

struct RayOutput {
  std::string log;
  bool pass = true;
  std::optional<sic::sicx::FinalizationBuilder::PackedRay> packed;
};

class BallSpool {
 public:
  BallSpool(const std::filesystem::path& dir, size_t bytes) : bytes_(bytes) {
    auto name = (dir / ".ball-spool-XXXXXX").string();
    fd_.fd = ::mkostemp(name.data(), O_CLOEXEC);
    if (fd_.fd < 0 || ::unlink(name.c_str()))
      throw sic::Error(sic::ErrorKind::io, "cannot create private BALL spool");
  }

  void append(std::span<const uint8_t> ball) {
    if (ball.size() != bytes_)
      throw sic::Error(sic::ErrorKind::integrity, "wrong BALL spool record size");
    sic::sicx::pwrite_all(fd_.fd, ball.data(), ball.size(), mul(count_, bytes_));
    ++count_;
  }

  std::span<const uint8_t> read(size_t index) {
    if (index >= count_) throw sic::Error(sic::ErrorKind::integrity, "missing BALL spool record");
    buffer_.resize(bytes_);
    sic::sicx::pread_exact(fd_.fd, buffer_.data(), buffer_.size(), mul(index, bytes_));
    return buffer_;
  }

 private:
  sic::support::sha256::Descriptor fd_{-1};
  size_t bytes_, count_ = 0;
  sic::sicx::ByteBuffer buffer_;
};

}  // namespace

int command(Invocation invocation) try {
  auto& input = invocation.input;
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
  sic::support::StopScope stop(options.cancellation.get());
  sic::support::check_stop();
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
  resources::Budget process_memory;
  process_memory.user_cap = options.memory_budget;
  const sic::sicx::WorkspaceAdmission admit = [&](const char* stage, uint64_t bytes) {
    process_memory.require_additional(stage, bytes);
  };
  const auto reader_limits = [&] { return sic::sicx::finalization_reader_limits(process_memory); };
  std::filesystem::path decode_file = file;
  if (finalize && sic::sicx::finalization_in_progress(file)) {
    const auto entries =
        sic::sicx::StreamWriter::peek_journal(file.string() + ".partial.journal", nullptr, admit);
    const auto exact_count = sic::sicx::exact_count_of(entries);
    std::string name = file.string() + ".resume-XXXXXX";
    if (!mkdtemp(name.data()))
      throw sic::Error(sic::ErrorKind::io, "cannot create resume reconstruction");
    recovered.dir = name;
    decode_file = recovered.dir / "exact.sicx";
    const auto prefix = std::filesystem::exists(file.string() + ".partial")
                            ? std::filesystem::path(file.string() + ".partial")
                            : file;
    sic::sicx::StreamOptions recovery{false, false, {}, false};
    recovery.admit_workspace = admit;
    recovery.limits = reader_limits();
    sic::sicx::StreamWriter exact(decode_file, recovery);
    for (size_t i = 0; i < exact_count; ++i) {
      const auto& e = entries[i];
      sic::support::check_stop();
      admit("resume exact chunk", e.length);
      exact.chunk(e.type_name(), e.flags, sic::sicx::StreamWriter::read_chunk_at(prefix, e),
                  e.object, e.first, e.count);
    }
    exact.finish();
    sic::sicx::Reader check(decode_file, reader_limits());
    admit("resume exact verification", sic::sicx::bounds_memory(check, true));
    (void)sic::sicx::verify_all(check, 1, {{options.maximum_precision}, {}});
    if (check.exact_root() != sic::sicx::interrupted_exact_root(file, entries, admit))
      throw sic::Error(sic::ErrorKind::integrity, "resume reconstruction exact-root mismatch");
  }
  auto source = input.open(decode_file, options.memory_budget);
  source->bind_cancellation(options.cancellation);
  std::optional<sic::sicx::Reader> reader_owner(std::in_place, source, reader_limits());
  const auto& reader = *reader_owner;
  if (finalize && reader.finalized())
    throw sic::Error(sic::ErrorKind::invalid_argument, "file is already finalized");
  const auto metadata_bytes = reader.index()[reader.meta_chunk()].length;
  const auto ray_bytes = reader.rays_chunk() ? reader.index()[*reader.rays_chunk()].length : 0;
  admit("decoder metadata copy", mul(64, add(metadata_bytes, ray_bytes)));
  sic::sicx::Meta meta = reader.meta();
  const auto decoded_exact_root = reader.exact_root();
  const bool finalized = reader.finalized();
  std::optional<FinalizationWorkspace> workspace;
  uint64_t publication_workspace = 0;
  slong initial_bits = 0;
  uint64_t ray_decode_bytes = 0, worker_decode_bytes = 0;
  if (finalize) {
    std::optional<sic::sicx::RayList> list;
    if (!invocation.ray_list.empty()) {
      std::string digest;
      list = sic::sicx::read_ray_list(invocation.ray_list, process_memory, false, &digest);
      if (!invocation.ray_list_sha256.empty() && digest != invocation.ray_list_sha256)
        throw sic::Error(sic::ErrorKind::integrity,
                         "ray list SHA-256 differs from the supplied pin");
      if (list->dimension != meta.d || list->exact_root != sic::sicx::hex(decoded_exact_root) ||
          !std::ranges::equal(list->smith, meta.smith))
        throw sic::Error(sic::ErrorKind::integrity,
                         "ray list dimension, exact root or Smith basis differs from the SICX");
    }
    const size_t ray_count = list ? list->words.size() : given.size();
    workspace = FinalizationWorkspace::plan(meta, ray_count, working_bits);
    options = sic::decode::Decoder::validate_options(options, reader);
    initial_bits = sic::checked_precision(options.target, options.delta, options.maximum_precision);
    const auto chunk_planning = mul(add(reader.index().size(), meta.elements.size()), 128);
    const auto coordinate_selection = mul(meta.d, 2 * sizeof(uint64_t));
    const auto metadata = add(workspace->metadata, add(chunk_planning, coordinate_selection));
    ray_decode_bytes = add(sic::decode::request_metadata_bytes(meta.d, meta.smith.size()),
                           sic::decode::request_numeric_bytes(meta.d, initial_bits));
    const auto degree = std::ranges::max_element(meta.degrees);
    const auto worker_ball_count = degree == meta.degrees.end() ? 3 : *degree + 3;
    worker_decode_bytes =
        mul(options.threads, add(4096, sic::decode::av_bytes(worker_ball_count, initial_bits)));
    const auto decode_stage = add(ray_decode_bytes, worker_decode_bytes);
    const auto output_stage =
        add(mul(meta.d, sic::decode::acb_bytes(initial_bits)),
            workspace->output_bytes(meta.d, initial_bits, options.target + 64, working_bits));
    admit("finalization request materialization",
          add(metadata, std::max(decode_stage, output_stage)));
    const auto ball_checks = add(mul(4, workspace->encoded), mul(2, workspace->ray_parts));
    const auto added_framing =
        mul(add(ray_count, 2), sic::sicx::kIndexEntryBytes + sic::sicx::kChunkHeaderBytes);
    const auto added_text = add(workspace->metadata, mul(add(meta.levels(), 1), 128));
    const auto completed_size =
        add(reader.size(), add(mul(ray_count, workspace->encoded), add(added_framing, added_text)));
    publication_workspace =
        add(metadata, std::max(add(sic::sicx::bounds_memory(reader, true), ball_checks),
                               sic::sicx::finalization_hash_buffer(completed_size)));
    admit("finalization publication plan", add(workspace->encoded, publication_workspace));
    std::cout << "FINALIZE-MEMORY requests_per_ray=" << meta.d << " initial_bits=" << initial_bits
              << " decode_workspace_per_ray=" << decode_stage
              << " output_workspace_per_ray=" << output_stage
              << " publication_workspace=" << publication_workspace << '\n';
    meta.rays.reserve(ray_count);
    if (list) {
      for (size_t i = 0; i < ray_count; ++i) {
        const auto& word = list->words[i];
        meta.rays.push_back({sic::sicx::ray_label(i), {word.begin(), word.end()}, 0, 0});
      }
    } else {
      for (auto& r : given) {
        if (r.word.size() != meta.smith.size() ||
            (r.anchor >= 0 && static_cast<uint64_t>(r.anchor) >= meta.d))
          throw sic::support::cli::UsageError("invalid finalization word or anchor");
        meta.rays.push_back({std::move(r.label), std::move(r.word),
                             static_cast<uint64_t>(std::max<int64_t>(0, r.anchor)), 0});
      }
      meta.ball_limbs = workspace->limbs;
      meta.ball_working_bits = working_bits;
      sic::sicx::Meta labels;
      labels.d = meta.d;
      labels.smith = meta.smith;
      sic::sicx::rays_from_json(sic::sicx::rays_to_json(meta), labels);
    }
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
  std::optional<BallSpool> spool;
  std::optional<sic::sicx::FinalizationBuilder> builder;
  {
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
    if (workspace) {
      spool.emplace(outdir, workspace->encoded);
      builder.emplace(meta, rays.size(), workspace->limbs, working_bits, sic::sicx::kDecoderVersion,
                      sic::decode::kDecoderSourceCommit,
                      [&](std::span<const uint8_t> bytes) { spool->append(bytes); });
    }
    std::vector<sic::decode::Decoder::Request> requests;
    const auto append_requests = [&](size_t ray) {
      for (uint64_t m : coordinates) {
        const auto e = meta.entry(m);
        requests.push_back(
            {e.representative, sic::sicx::words_add(meta.rays[ray].word, e.word, meta.smith)});
      }
    };
    sic::decode::Stats s;
    unsigned nodes = 0, groups = 0;
    slong base_precision = 0;
    const size_t count = mul(rays.size(), coordinates.size());
    std::optional<sic::decode::Decoder> decoder;
    size_t batch_limit = rays.size();
    if (workspace) {
      // Admit one ray while choosing tower residency, then size the full batch
      // from the decoder's live state and its reserved source headroom.
      admit("finalization decoder setup", add(ray_decode_bytes, worker_decode_bytes));
      append_requests(rays.front());
      decoder.emplace(reader, options, requests);
      requests.clear();
      const auto room = sic::decode::DecoderAccess::request_headroom(*decoder);
      const auto ray_room = room > worker_decode_bytes ? room - worker_decode_bytes : 0;
      batch_limit = std::clamp<uint64_t>(ray_room / ray_decode_bytes, 1, rays.size());
    }
    bool ok = true;
    size_t batches = 0, packing_batches = 0, packing_peak_rays = 0;
    double decode_seconds = 0;
    for (size_t begin = 0; begin < rays.size();) {
      const size_t size = std::min(batch_limit, rays.size() - begin);
      const auto batch = std::span(rays).subspan(begin, size);
      sic::support::ComplexBallVector values;
      std::unique_ptr<sic::decode::Pool> last_workers;
      slong output_bits = initial_bits;
      const auto decode_start = std::chrono::steady_clock::now();
      try {
        if (workspace)
          admit("finalization decode batch", add(mul(size, ray_decode_bytes), worker_decode_bytes));
        requests.reserve(mul(size, coordinates.size()));
        for (auto r : batch) append_requests(r);
        {
          auto chunks = sic::decode::Decoder::input_chunks(reader, requests);
          if (full && finalized)
            for (auto r : batch) chunks.push_back(reader.ball_chunk(r));
          reader.plan_chunks(chunks);
        }
        if (!decoder) decoder.emplace(reader, options, requests);
        bool test_replay_first = false;
#ifdef SIC_ENABLE_TEST_HOOKS
        if (workspace) {
          const auto limit = sic::sicx::env_u64("SIC_TEST_FINALIZATION_BATCH_RAYS");
          if (limit && size > limit)
            throw sic::Error(sic::ErrorKind::resource, "test finalization batch limit");
          test_replay_first =
              begin == 0 && sic::sicx::env_u64("SIC_TEST_FINALIZATION_REPLAY_FIRST_OUTPUT");
        }
#endif
        if (workspace) {
          values = sic::decode::DecoderAccess::decode_rays(*decoder, requests, coordinates.size(),
                                                           output_bits, test_replay_first);
        } else {
          values = decoder->decode(requests);
        }
        s = decoder->stats();
        nodes = decoder->placement().nodes;
        groups = decoder->pool_groups();
        base_precision = std::max(base_precision, decoder->base_precision());
        if (!workspace) {
          decoder.reset();
        } else if (begin + size == rays.size()) {
          last_workers = sic::decode::DecoderAccess::take_workers(std::move(*decoder));
          decoder.reset();
        }
      } catch (const sic::Error& error) {
        if (!workspace || size == 1 || error.kind() != sic::ErrorKind::resource) throw;
        std::vector<sic::decode::Decoder::Request>().swap(requests);
        batch_limit = size / 2;
        std::cout << "FINALIZE-MEMORY retry with at most " << batch_limit << " rays\n";
        continue;
      }
      decode_seconds +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - decode_start).count();
      if (!reader.remote()) std::vector<sic::decode::Decoder::Request>().swap(requests);
      ++batches;
      const auto write_ray = [&](size_t i) -> RayOutput {
        sic::support::check_stop();
        const auto r = batch[i];
        const auto& ray = meta.rays[r];
        const auto raw = values.span().subspan(i * coordinates.size(), coordinates.size());
        std::ostringstream log;
        if (!full) {
          sic::data::write_balls(outdir / (ray.label +
                                           (contiguous ? ".raw-" + std::to_string(first) + "-" +
                                                             std::to_string(last - 1)
                                                       : ".raw-selected") +
                                           ".balls"),
                                 raw);
          log << "RAY " << ray.label << " raw coordinates "
              << (contiguous ? std::to_string(first) + ".." + std::to_string(last - 1) : coords_arg)
              << " written\n";
          return {log.str(), true, {}};
        }
        auto anchor = ray.anchor;
        if (finalize && (!invocation.ray_list.empty() || given[r].anchor == kAutomaticAnchor)) {
          anchor = sic::decode::select_anchor(raw, options.target + 64);
          log << "RAY " << ray.label << " automatic anchor=" << anchor << '\n';
        }
        sic::data::write_balls(outdir / (ray.label + ".raw.balls"), raw);
        const sic::support::ComplexBallVector w =
            sic::decode::normalize(raw, anchor, options.target + 64);
        sic::data::write_balls(outdir / (ray.label + ".normalized.balls"), w.span());
        if (!compare) {
          log << "RAY " << ray.label << " decoded (not finalized: no BALL comparison)\n";
          return {log.str(), true, {}};
        }
        std::vector<sic::sicx::BallPart> parts;
        if (finalized)
          parts = reader.ball_parts(r);
        else {
          const unsigned limbs = workspace->limbs;
          parts.reserve(2 * meta.d);
          for (uint64_t j = 0; j < meta.d; ++j) {
            parts.push_back(sic::sicx::outward_ball_part(acb_realref(w[j]), limbs));
            parts.push_back(sic::sicx::outward_ball_part(acb_imagref(w[j]), limbs));
          }
        }
        const auto comparison =
            sic::data::write_decoded_fp64(outdir / (ray.label + ".fp64.txt"), w, parts, meta.d);
        const auto overlap = comparison.overlap, same64 = comparison.same64;
        const auto worst = comparison.worst;
        const bool pass = overlap == meta.d && same64 == meta.d;
        log << "RAY " << ray.label << " overlap_with_" << (finalized ? "BALL" : "constructed_BALL")
            << "=" << overlap << "/" << meta.d << " fp64_identical=" << same64 << "/" << meta.d
            << " max_normalized_radius<=2^" << worst << (pass ? " PASS" : " FAIL") << '\n';
        RayOutput result{log.str(), pass, {}};
        if (builder && pass) result.packed = builder->pack({ray.label, ray.word, anchor, 0}, parts);
        return result;
      };
      const auto publish_ray = [&](RayOutput result) {
        ok &= result.pass;
        std::cout << result.log;
        if (result.packed) builder->append(std::move(*result.packed));
      };
      if (workspace) {
        auto& output_workers =
            last_workers ? *last_workers : sic::decode::DecoderAccess::workers(*decoder);
        const auto per_ray =
            workspace->output_bytes(meta.d, output_bits, options.target + 64, working_bits);
        size_t limit = output_workers.size();
#ifdef SIC_ENABLE_TEST_HOOKS
        if (const auto test_limit = sic::sicx::env_u64("SIC_TEST_FINALIZATION_PACK_RAYS"))
          limit = std::min<uint64_t>(limit, test_limit);
#endif
        for (size_t packed = 0; packed < size;) {
          // Live RSS includes the decoded batch. Reserve for every simultaneously
          // packed ray, including its encoded payload until the owner spools it.
          const auto max_rays = std::min(limit, size - packed);
          const auto width =
              std::clamp<uint64_t>(process_memory.remaining() / per_ray, 1, max_rays);
          admit("finalization output packing", mul(width, per_ray));
          std::vector<RayOutput> outputs(width);
          output_workers.run(width, [&](size_t i) { outputs[i] = write_ray(packed + i); });
          for (auto& output : outputs) publish_ray(std::move(output));
          ++packing_batches;
          packing_peak_rays = std::max<uint64_t>(packing_peak_rays, width);
          packed += width;
        }
      } else {
        for (size_t i = 0; i < size; ++i) publish_ray(write_ray(i));
      }
      begin += size;
    }
    if (workspace)
      std::cout << "FINALIZE-MEMORY batches=" << batches << " packing_batches=" << packing_batches
                << " packing_peak_rays=" << packing_peak_rays << " passes=" << s.passes << '\n';
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage))
      throw sic::Error(sic::ErrorKind::io, "cannot read process resource usage");
    const auto peak_rss = resources::peak_rss_bytes(usage);
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
           << "],\"target_bits\":" << options.target << ",\"base_bits\":" << base_precision
           << ",\"passes\":" << s.passes << ",\"replays\":" << s.replays
           << ",\"workers\":" << options.threads;
      if (!contiguous) {
        prov << ",\"coordinate_indices\":[";
        for (size_t j = 0; j < coordinates.size(); ++j) prov << (j ? "," : "") << coordinates[j];
        prov << "]";
      }
      prov << "}\n";
      sic::data::write_decode_provenance(outdir / "decode.provenance.json", prov.str());
    }
    std::cout << "DECODER " << sic::sicx::kDecoderVersion << '\n';
    std::cout << "DECODE d=" << meta.d << " rays=" << rays.size() << " outputs=" << count
              << " target=" << options.target << " base_bits=" << base_precision
              << " threads=" << options.threads << " numa_nodes=" << nodes << " passes=" << s.passes
              << " replays=" << s.replays << '\n'
              << "TIME wall=" << decode_seconds << " cpu="
              << usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
                     1e-6 * (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec)
              << " tower_load=" << s.seconds_tower_load << " base_path=" << s.seconds_base
              << " tower_eval=" << s.seconds_tower_eval << " roots=" << s.seconds_roots
              << " edges=" << s.seconds_edges << " body=" << s.seconds_body << "\n"
              << "IO tower_bytes=" << s.tower_bytes_read << " body_bytes=" << s.body_bytes_read
              << " file_bytes=" << reader.size() << " peak_rss_kb=" << (peak_rss >> 10) << '\n'
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
              << " arenas_capped=" << s.arenas_capped << " peak_rss_mib=" << (peak_rss >> 20)
              << (peak_rss <= options.memory_budget ? " within_budget" : " OVER_BUDGET") << '\n'
              << "NUMA pool_groups=" << groups << " bind_failures=" << s.bind_failures << " pages"
              << numa << '\n'
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
    if (check_budget && peak_rss > options.memory_budget) {
      std::cout << "BUDGET EXCEEDED: peak RSS above --memory-mib" << std::endl;
      status = 1;
    }
  }
  if (finalize) {
    if (status) {
      std::cout << "FINALIZE REFUSED: the decoded rays do not certify BALL" << std::endl;
      return 1;
    }
    reader_owner.reset();
    source.reset();
    admit("finalization publication", add(workspace->encoded, publication_workspace));
    std::cout << "FINALIZE-MEMORY publication_rss=" << resources::inspect().rss << '\n';
    const auto finalization = std::move(*builder).finish();
    const auto check_peak = [&] {
      sic::support::check_stop();
      if (check_budget && resources::process_peak_rss_bytes() > options.memory_budget)
        throw sic::Error(sic::ErrorKind::resource,
                         "peak RSS above --memory-mib before publication");
    };
    const auto artifact = sic::sicx::finalize_and_hash(
        file, meta, finalization, [&](size_t ray) { return spool->read(ray); }, resume,
        decoded_exact_root, {{options.maximum_precision}, {}}, process_memory, check_peak);
    const auto final_peak = resources::process_peak_rss_bytes();
    std::cout << "FINALIZE-MEMORY peak_rss_bytes=" << final_peak << '\n';
    if (check_budget && final_peak > options.memory_budget)
      throw sic::Error(sic::ErrorKind::resource, "peak RSS above --memory-mib during publication");
    std::cout << "FINALIZED " << file.string()
              << " exact_root=" << sic::sicx::hex(artifact.roots.exact_root)
              << " merkle=" << sic::sicx::hex(artifact.roots.file_root)
              << " sha256=" << artifact.sha256 << std::endl;
  }
  return status;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::apps::decode
