// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/container/finalization_builder.hpp"

#include "sic/support/byte_source.hpp"
#include "sicx/codec/ball.hpp"
#include "sicx/codec/block.hpp"
#include "sicx/container/directory.hpp"
#include "sicx/container/leaf_index.hpp"
#include "sicx/container/verify.hpp"
#include "sicx/container/verify.tpp"
#include "sicx/io/io.hpp"
#include "support/sha256.hpp"

namespace sic::sicx {
BallPart outward_ball_part(arb_srcptr source, unsigned limbs) {
  if (!limbs || limbs > kMaximumBallLimbs || !arb_is_finite(source))
    fail(sic::ErrorKind::invalid_argument, "invalid BALL source or width");
  sic::support::flint::Arb rounded, stored;

  try {
    arb_set_round(rounded, source, static_cast<slong>(64 * limbs));
    std::unique_ptr<char, decltype(&flint_free)> dump(arb_dump_str(rounded), flint_free);
    const std::string text(dump.get());
    const BallPart part = parse_ball_part(text, limbs);
    ball_part_arb(stored, part);
    if (!arb_contains(stored, source))
      fail(sic::ErrorKind::invalid_format, "BALL does not contain the decoded enclosure");
    if (certified_round(source, "decoded BALL source") != certified_round(stored, "stored BALL"))
      fail(sic::ErrorKind::invalid_format, "BALL rounding differs from its decoded source");

    return part;
  } catch (...) {
    throw;
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
Finalization build_finalization(Meta& meta, const std::vector<FinalizeRay>& rays,
                                const std::vector<std::vector<BallPart>>& parts,
                                uint64_t working_bits, const std::string& decoder,
                                const std::string& decoder_source_commit) {
  if (rays.empty()) fail(sic::ErrorKind::invalid_argument, "at least one ray is required");
  if (parts.size() != rays.size()) fail(sic::ErrorKind::invalid_argument, "BALL ray count");
  const unsigned limbs = parts.front().empty() ? 0 : parts.front().front().mantissa.size();
  FinalizationBuilder builder(meta, rays.size(), limbs, working_bits, decoder,
                              decoder_source_commit);
  for (size_t ri = 0; ri < rays.size(); ++ri) {
    const auto& ray = rays[ri];
    if (ray.anchor < 0) fail(sic::ErrorKind::invalid_argument, "invalid finalized ray");
    builder.append({ray.label, ray.word, static_cast<uint64_t>(ray.anchor), 0}, parts[ri]);
  }
  return std::move(builder).finish();
}

FinalizationBuilder::FinalizationBuilder(Meta& meta, size_t count, unsigned limbs,
                                         uint64_t working_bits, const std::string& decoder,
                                         const std::string& decoder_source_commit, BallSink sink)
    : meta_(meta), count_(count), sink_(std::move(sink)) {
  if (!working_bits || working_bits > kMaximumWorkingBits)
    throw Error(ErrorKind::invalid_argument, "working precision outside SICX informative range");
  if (!limbs || limbs > kMaximumBallLimbs)
    fail(sic::ErrorKind::invalid_argument, "BALL width out of range");
  meta_.ball_limbs = limbs;
  meta_.ball_working_bits = working_bits;
  meta_.rays_decoder = decoder;
  meta_.rays_decoder_source_commit = decoder.empty() ? "" : decoder_source_commit;
  rays_.reserve(count_);
  if (!sink_) out_.balls.reserve(count_);
}

void FinalizationBuilder::append(Ray ray, std::span<const BallPart> parts) {
  append(pack(std::move(ray), parts));
}

FinalizationBuilder::PackedRay FinalizationBuilder::pack(Ray ray,
                                                         std::span<const BallPart> parts) const {
  const unsigned limbs = meta_.ball_limbs;
  if (ray.word.size() != meta_.smith.size() || ray.anchor >= meta_.d ||
      parts.size() != checked_mul(2, meta_.d))
    fail(sic::ErrorKind::invalid_argument, "invalid finalized ray");
  ByteBuffer payload;
  const auto payload_bytes = checked_mul(meta_.d, ball_record_bytes(limbs));
  admit_chunk("BALL", payload_bytes);
  payload.reserve(payload_bytes);
  int64_t worst = INT64_MIN / 2;
  for (const auto& part : parts) {
    if (part.mantissa.size() != limbs)
      fail(sic::ErrorKind::invalid_argument, "BALL width mismatch");
    worst = std::max(worst, radius_log2_bound(part));
    put_ball_part(payload, part);
  }
  ray.accuracy_bits = -worst;
  return {std::move(ray), std::move(payload)};
}

void FinalizationBuilder::append(PackedRay packed) {
  if (rays_.size() == count_) fail(sic::ErrorKind::invalid_argument, "BALL ray count");
  rays_.push_back(std::move(packed.ray));
  if (sink_)
    sink_(packed.payload);
  else
    out_.balls.push_back(std::move(packed.payload));
}

Finalization FinalizationBuilder::finish() && {
  if (rays_.size() != count_) fail(sic::ErrorKind::invalid_argument, "BALL ray count");
  meta_.rays = std::move(rays_);
  const auto& meta = meta_;
  out_.rays_text = sic::support::dump_json(rays_to_json(meta));
  {
    Meta check;
    check.d = meta.d;
    check.smith = meta.smith;
    rays_from_json(sic::support::JsonParser::parse(out_.rays_text),
                   check);  // reader rules before writing
  }
  // PREC: decoding hints (never trusted for rigor), from the branch rectangles and element bounds.
  sic::support::Json prec = sic::support::Json::object_value();
  sic::support::Json branch_bits = sic::support::Json::array_value();
  int64_t worst_branch = 0;
  sic::support::flint::Arb x, y;

  for (const auto& b : meta.branches) {
    if (arb_load_str(x, b[0].c_str()) || arb_load_str(y, b[1].c_str())) {
      fail(sic::ErrorKind::invalid_format, "branch rectangle does not load");
    }
    sic::support::flint::Mag r;

    mag_max(r, arb_radref(x), arb_radref(y));
    const int64_t bits = mag_is_zero(r) ? 0 : -static_cast<int64_t>(mag_get_d_log2_approx(r));

    branch_bits.array.push_back(sic::support::Json::integer_value(bits));
    worst_branch = std::max(worst_branch, bits);
  }

  prec["branch_bits"] = sic::support::Json::integer_value(meta.branch_bits);
  prec["base_path_bits"] = sic::support::Json::integer_value(worst_branch + 128);
  prec["delta_bits"] = sic::support::Json::integer_value(64);
  uint64_t max_bits = 0;
  for (const auto& e : meta.elements) max_bits = std::max(max_bits, e.max_bits);
  prec["max_element_bits"] = sic::support::Json::integer_value(static_cast<int64_t>(max_bits));
  out_.prec_text = sic::support::dump_json(prec);
  return std::move(out_);
}
}  // namespace sic::sicx

namespace sic::sicx {
std::span<const uint8_t> text_span(const std::string& s) {
  return std::span(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}
}  // namespace sic::sicx

namespace sic::sicx {
namespace {
void write_finalization(StreamWriter& writer, const Meta& meta, const Finalization& f, size_t count,
                        const BallSource& balls) {
  writer.chunk("RAYS", kRequired, text_span(f.rays_text));
  for (size_t ri = 0; ri < count; ++ri) writer.chunk("BALL", kRequired, balls(ri), ri, 0, meta.d);
  writer.chunk("PREC", 0, text_span(f.prec_text));
}

Digest finalization_digest(const Finalization& f, const Digest& exact_root, size_t count,
                           const BallSource& balls) {
  support::sha256::Hasher h;
  constexpr std::string_view tag = "sicx finalize 2\n";
  h.update(tag.data(), tag.size());
  h.update(exact_root.data(), exact_root.size());
  const auto add = [&](std::span<const uint8_t> bytes) {
    ByteBuffer len;
    put_le(len, bytes.size(), 8);
    h.update(len.data(), len.size());
    h.update(bytes.data(), bytes.size());
  };
  add(text_span(f.rays_text));
  for (size_t ri = 0; ri < count; ++ri) add(balls(ri));
  add(text_span(f.prec_text));
  return h.finish();
}

BallSource retained_balls(const Finalization& f) {
  return [&f](size_t index) -> std::span<const uint8_t> { return f.balls.at(index); };
}
}  // namespace

void write_finalization(StreamWriter& writer, const Meta& meta, const Finalization& f) {
  write_finalization(writer, meta, f, f.balls.size(), retained_balls(f));
}

Digest finalization_digest(const Finalization& f, const Digest& exact_root) {
  return finalization_digest(f, exact_root, f.balls.size(), retained_balls(f));
}
}  // namespace sic::sicx

namespace sic::sicx {
bool finalization_in_progress(const std::filesystem::path& path) {
  return sic::sicx::filesystem_io(
      [&] { return std::filesystem::exists(path.string() + ".partial.journal"); });
}
}  // namespace sic::sicx

namespace sic::sicx {
Meta meta_of_interrupted(const std::filesystem::path& path) {
  const auto entries = StreamWriter::peek_journal(path.string() + ".partial.journal");
  const std::filesystem::path partial = path.string() + ".partial";
  const std::filesystem::path file =
      sic::sicx::filesystem_io([&] { return std::filesystem::exists(partial); }) ? partial : path;
  for (const auto& e : entries)
    if (e.type_name() == "EXMT") {
      const ByteBuffer payload = StreamWriter::read_chunk_at(file, e);
      return meta_from_json(sic::support::JsonParser::parse(
          std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size())));
    }
  fail(sic::ErrorKind::invalid_format, "interrupted finalization: EXMT not found in the journal");
}
}  // namespace sic::sicx

namespace sic::sicx {
Digest interrupted_exact_root(const std::filesystem::path& path) {
  const auto entries = StreamWriter::peek_journal(path.string() + ".partial.journal");
  return interrupted_exact_root(path, entries, {});
}

Digest interrupted_exact_root(const std::filesystem::path& path,
                              const std::vector<IndexEntry>& entries,
                              const WorkspaceAdmission& admit) {
  const auto file =
      sic::sicx::filesystem_io([&] { return std::filesystem::exists(path.string() + ".partial"); })
          ? std::filesystem::path(path.string() + ".partial")
          : path;
  std::vector<LogicalEntry> leaves;
  const auto exact = exact_count_of(entries);
  for (size_t i = 0; i < exact; ++i) {
    const auto& entry = entries[i];
    ResourceLimits limits;
    limits.directory_entries -= leaves.size();
    if (entry.leaf_count > limits.directory_entries)
      fail(ErrorKind::resource, "interrupted logical directory limit");
    if (admit) admit("interrupted exact chunk payload", entry.length);
    const auto payload = StreamWriter::read_chunk_at(file, entry);
    const auto part = chunk_leaves(entry, payload, limits, admit);
    if (!part.empty() &&
        (entries[i].first_leaf != leaves.size() || entries[i].leaf_count != part.size()))
      fail(sic::ErrorKind::invalid_format, "interrupted leaf mapping");
    const auto capacity = directory_append_capacity(leaves, part.size(), ResourceLimits{},
                                                    "interrupted logical directory");
    if (admit)
      admit("interrupted logical directory growth",
            directory_append_bytes(leaves, capacity, part.size()));
    leaves.reserve(capacity);
    leaves.insert(leaves.end(), part.begin(), part.end());
  }
  if (admit)
    admit("interrupted logical Merkle root", checked_mul(leaves.size(), 4 * sizeof(LogicalEntry)));
  return merkle_root(leaves);
}
}  // namespace sic::sicx

namespace sic::sicx {
ResourceLimits finalization_reader_limits(const sic::support::resources::Budget& budget) {
  ResourceLimits limits;
  limits.reader_memory_bytes = std::min(limits.reader_memory_bytes, budget.remaining());
  return limits;
}

uint64_t finalization_hash_buffer(uint64_t file_bytes) {
  return std::min<uint64_t>(file_bytes, support::sha256::kFileChunkBytes);
}

namespace {
FinalizedRoots finalize(const std::filesystem::path& path, const Meta& meta, const Finalization& f,
                        size_t ball_count, const BallSource& balls, bool resume,
                        const Digest& decoded_exact_root, VerificationPolicy policy,
                        const sic::support::resources::Budget* budget, std::string* sha256,
                        const std::function<void()>& check_peak) {
  WorkspaceAdmission admit;
  if (budget)
    admit = [budget](const char* stage, uint64_t bytes) {
      budget->require_additional(stage, bytes);
    };
  const auto reader_limits = [&] {
    return budget ? finalization_reader_limits(*budget) : ResourceLimits{};
  };
  size_t exact = 0;
  const auto validate = [&](const std::filesystem::path& private_path, int fd, const Digest& root) {
#ifdef SIC_ENABLE_TEST_HOOKS
    if (io_fault_hook) io_fault_hook("finalization_validation", private_path);
#endif
    uint64_t file_bytes = 0;
    {
      Reader check(std::make_shared<sic::support::LocalByteSource>(fd), reader_limits());
      if (check.exact_root() != decoded_exact_root || check.root() != root ||
          check.exact_count() != exact || !check.finalized())
        throw std::logic_error(std::string("SICX: ") + "finalization invariant");
      if (admit) admit("completed SICX verification", bounds_memory(check, true, policy.valuation));
      (void)verify_all(check, 1, policy);
      file_bytes = check.size();
    }
    if (sha256) {
      const auto bytes = finalization_hash_buffer(file_bytes);
      if (admit) admit("finalized SICX SHA-256", bytes);
      std::vector<unsigned char> scratch(bytes);
      *sha256 = support::sha256::range_hash(fd, 0, file_bytes, scratch, true);
    }
    if (check_peak) check_peak();
  };
  std::unique_ptr<StreamWriter> writer;
  Digest exact_root;
  StreamOptions options;
  options.admit_workspace = admit;
  options.verify_output = validate;
  if (resume && sic::sicx::filesystem_io(
                    [&] { return std::filesystem::exists(path.string() + ".partial.journal"); })) {
    {
      const auto journaled =
          StreamWriter::peek_journal(path.string() + ".partial.journal", nullptr, admit);
      exact = exact_count_of(journaled);
      exact_root = interrupted_exact_root(path, journaled, admit);
    }
    if (exact_root != decoded_exact_root)
      fail(sic::ErrorKind::integrity, "finalization exact root differs from the certified decode");
    options.plan = finalization_digest(f, exact_root, ball_count, balls);
    writer = StreamWriter::resume_reopen(path, options);
    // The recovered prefix must be exactly the exact part recorded when the finalization began.
    const ByteBuffer* note = writer->note("exact part");
    ByteBuffer expected;
    put_le(expected, exact, 8);
    expected.insert(expected.end(), exact_root.begin(), exact_root.end());
    if (!note || *note != expected || exact_count_of(writer->resumed()) < exact) {
      fail(sic::ErrorKind::invalid_format,
           "resume: the journal does not recover the complete exact part (files kept for "
           "inspection)");
    }
  } else {
    std::vector<IndexEntry> index;
    uint64_t indx_offset = 0;
    {
      Reader reader(path, reader_limits());
      if (admit) admit("exact SICX verification", bounds_memory(reader, true, policy.valuation));
      (void)verify_all(reader, 1, policy);
      if (admit)
        admit("finalization exact index", checked_mul(reader.exact_count(), sizeof(IndexEntry)));
      index.assign(reader.index().begin(), reader.index().begin() + reader.exact_count());
      indx_offset =
          reader.finalized() ? reader.index()[reader.exact_count()].offset : reader.ldir_offset();
      exact_root = reader.exact_root();
      exact = reader.exact_count();
    }
    if (exact_root != decoded_exact_root)
      fail(sic::ErrorKind::integrity, "finalization exact root differs from the certified decode");
    options.plan = finalization_digest(f, exact_root, ball_count, balls);
    writer = StreamWriter::reopen(path, options, index, indx_offset, exact_root);
  }
  writer->adopt(exact);
  write_finalization(*writer, meta, f, ball_count, balls);
  const Digest root = writer->finish();
  return {exact_root, root};
}
}  // namespace

FinalizedRoots finalize_in_place(const std::filesystem::path& path, const Meta& meta,
                                 const Finalization& f, bool resume,
                                 const Digest& decoded_exact_root, VerificationPolicy policy) {
  return finalize(path, meta, f, f.balls.size(), retained_balls(f), resume, decoded_exact_root,
                  policy, nullptr, nullptr, {});
}

FinalizedArtifact finalize_and_hash(const std::filesystem::path& path, const Meta& meta,
                                    const Finalization& f, const BallSource& balls, bool resume,
                                    const Digest& decoded_exact_root, VerificationPolicy policy,
                                    const sic::support::resources::Budget& budget,
                                    const std::function<void()>& check_peak) {
  FinalizedArtifact result;
  result.roots = finalize(path, meta, f, meta.rays.size(), balls, resume, decoded_exact_root,
                          policy, &budget, &result.sha256, check_peak);
  return result;
}
}  // namespace sic::sicx
