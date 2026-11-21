// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/sicx/finalize.hpp"

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
  if (!working_bits || working_bits > kMaximumWorkingBits)
    throw Error(ErrorKind::invalid_argument, "working precision outside SICX informative range");
  // Storage width comes from the outward-rounded input, independently of computation.
  const unsigned limbs = parts.empty() || parts.front().empty()
                             ? 0
                             : static_cast<unsigned>(parts.front().front().mantissa.size());
  if (!limbs || limbs > kMaximumBallLimbs)
    fail(sic::ErrorKind::invalid_argument, "BALL width out of range");
  meta.ball_limbs = limbs;
  meta.ball_working_bits = working_bits;
  meta.rays_decoder = decoder;
  meta.rays_decoder_source_commit = decoder.empty() ? "" : decoder_source_commit;
  meta.rays.clear();
  Finalization out;
  for (size_t ri = 0; ri < rays.size(); ++ri) {
    Ray ray{rays[ri].label, rays[ri].word, 0, 0};
    ByteBuffer payload;
    if (rays[ri].word.size() != meta.smith.size() || rays[ri].anchor < 0 ||
        static_cast<uint64_t>(rays[ri].anchor) >= meta.d || parts[ri].size() != 2 * meta.d)
      fail(sic::ErrorKind::invalid_argument, "invalid finalized ray");
    int64_t worst = INT64_MIN / 2;
    for (const auto& part : parts[ri]) {
      if (part.mantissa.size() != limbs)
        fail(sic::ErrorKind::invalid_argument, "BALL width mismatch");
      worst = std::max(worst, radius_log2_bound(part));
      put_ball_part(payload, part);
    }
    ray.anchor = static_cast<uint64_t>(rays[ri].anchor);
    ray.accuracy_bits = -worst;
    meta.rays.push_back(ray);
    out.balls.push_back(std::move(payload));
  }
  out.rays_text = sic::support::dump_json(rays_to_json(meta));
  {
    Meta check = meta;
    check.rays.clear();
    rays_from_json(sic::support::JsonParser::parse(out.rays_text),
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
  out.prec_text = sic::support::dump_json(prec);
  return out;
}
}  // namespace sic::sicx

namespace sic::sicx {
std::span<const uint8_t> text_span(const std::string& s) {
  return std::span(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}
}  // namespace sic::sicx

namespace sic::sicx {
void write_finalization(StreamWriter& writer, const Meta& meta, const Finalization& f) {
  writer.chunk("RAYS", kRequired, text_span(f.rays_text));
  for (size_t ri = 0; ri < f.balls.size(); ++ri)
    writer.chunk("BALL", kRequired, f.balls[ri], ri, 0, meta.d);
  writer.chunk("PREC", 0, text_span(f.prec_text));
}
}  // namespace sic::sicx

namespace sic::sicx {
Digest finalization_digest(const Finalization& f, const Digest& exact_root) {
  support::sha256::Hasher h;
  const std::string tag = "sicx finalize 2\n";
  h.update(reinterpret_cast<const uint8_t*>(tag.data()), tag.size());
  h.update(exact_root.data(), exact_root.size());
  auto add = [&](const uint8_t* p, size_t n) {
    ByteBuffer len;
    put_le(len, n, 8);
    h.update(len.data(), 8);
    h.update(p, n);
  };
  add(reinterpret_cast<const uint8_t*>(f.rays_text.data()), f.rays_text.size());
  for (const auto& b : f.balls) add(b.data(), b.size());
  add(reinterpret_cast<const uint8_t*>(f.prec_text.data()), f.prec_text.size());
  return h.finish();
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
  const auto file =
      sic::sicx::filesystem_io([&] { return std::filesystem::exists(path.string() + ".partial"); })
          ? std::filesystem::path(path.string() + ".partial")
          : path;
  std::vector<LogicalEntry> leaves;
  for (size_t i = 0; i < exact_count_of(entries); ++i) {
    const auto payload = StreamWriter::read_chunk_at(file, entries[i]);
    const auto part = chunk_leaves(entries[i], payload);
    if (!part.empty() &&
        (entries[i].first_leaf != leaves.size() || entries[i].leaf_count != part.size()))
      fail(sic::ErrorKind::invalid_format, "interrupted leaf mapping");
    leaves.insert(leaves.end(), part.begin(), part.end());
  }
  return merkle_root(leaves);
}
}  // namespace sic::sicx

namespace sic::sicx {
FinalizedRoots finalize_in_place(const std::filesystem::path& path, const Meta& meta,
                                 const Finalization& f, bool resume,
                                 const Digest& decoded_exact_root, VerificationPolicy policy) {
  const auto validate = [decoded_exact_root, policy](const std::filesystem::path& private_path) {
#ifdef SIC_ENABLE_TEST_HOOKS
    if (io_fault_hook) io_fault_hook("finalization_validation", private_path);
#endif
    Reader check(private_path);
    if (check.exact_root() != decoded_exact_root || !check.finalized())
      throw std::logic_error(std::string("SICX: ") + "finalization invariant");
    (void)verify_all(check, 1, policy);
  };
  std::unique_ptr<StreamWriter> writer;
  Digest exact_root;
  size_t exact = 0;
  if (resume && sic::sicx::filesystem_io(
                    [&] { return std::filesystem::exists(path.string() + ".partial.journal"); })) {
    const auto journaled = StreamWriter::peek_journal(path.string() + ".partial.journal");
    exact = exact_count_of(journaled);
    exact_root = interrupted_exact_root(path);
    if (exact_root != decoded_exact_root)
      fail(sic::ErrorKind::integrity, "finalization exact root differs from the certified decode");
    StreamOptions options;
    options.plan = finalization_digest(f, exact_root);
    options.before_publish = validate;
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
      Reader reader(path);
      (void)verify_all(reader, 1, policy);
      index.assign(reader.index().begin(), reader.index().begin() + reader.exact_count());
      indx_offset =
          reader.finalized() ? reader.index()[reader.exact_count()].offset : reader.ldir_offset();
      exact_root = reader.exact_root();
      exact = reader.exact_count();
    }
    if (exact_root != decoded_exact_root)
      fail(sic::ErrorKind::integrity, "finalization exact root differs from the certified decode");
    StreamOptions options;
    options.plan = finalization_digest(f, exact_root);
    options.before_publish = validate;
    writer = StreamWriter::reopen(path, options, index, indx_offset, exact_root);
  }
  writer->adopt(exact);
  write_finalization(*writer, meta, f);
  const Digest root = writer->finish();
  Reader check(path);
  if (check.exact_root() != exact_root || check.exact_count() != exact)
    fail(sic::ErrorKind::integrity, "finalization changed the exact part");
  if (!check.finalized()) fail(sic::ErrorKind::invalid_format, "finalized file has no RAYS");
  return {exact_root, root};
}
}  // namespace sic::sicx
