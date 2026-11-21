// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/sicx/text.hpp"

#include "sic/support/flint_raii.hpp"
#include "sic/support/durable_file.hpp"
#include "sicx/codec/ball.hpp"
#include "sicx/codec/block.hpp"
#include "sicx/codec/integer.hpp"
#include "sicx/container/directory.hpp"
#include "sicx/container/leaf_index.hpp"
#include "sicx/container/verify.hpp"
#include "sicx/container/verify.tpp"
#include "sicx/io/io.hpp"
#include "support/sha256.hpp"

namespace sic::sicx {
sic::support::Json dec(uint64_t v) { return sic::support::Json::string_value(std::to_string(v)); }
}  // namespace sic::sicx

namespace sic::sicx {
sic::support::Json dec_fmpz(const fmpz_t v) {
  sic::support::flint::String text(fmpz_get_str(nullptr, 10, v));
  sic::support::Json j = sic::support::Json::string_value(text.get());
  return j;
}
}  // namespace sic::sicx

namespace sic::sicx {
sic::support::Json leaf_json(const Meta& meta, std::span<const uint8_t> payload) {
  IntCodec codec;
  PairTransform transform(meta.D0);
  FmpzPair value;
  sic::support::Json leaf = sic::support::Json::object_value(),
                     runs = sic::support::Json::array_value();
  sic::support::Json* pairs = nullptr;
  const auto h = decode_block(
      payload, meta,
      [&](uint64_t, const fmpz_t x, const fmpz_t y) {
        if (!pairs) {
          sic::support::Json run = sic::support::Json::array_value();
          run.array = {sic::support::Json::string_value("pairs"),
                       sic::support::Json::array_value()};
          runs.array.push_back(std::move(run));
          pairs = &runs.array.back().array[1];
        }
        sic::support::Json pair = sic::support::Json::array_value();
        pair.array = {dec_fmpz(x), dec_fmpz(y)};
        pairs->array.push_back(std::move(pair));
      },
      [&](uint64_t, uint64_t n) {
        pairs = nullptr;
        sic::support::Json run = sic::support::Json::array_value();
        run.array = {sic::support::Json::string_value("zeros"), dec(n)};
        runs.array.push_back(std::move(run));
      },
      codec, transform, value);
  leaf["element"] = dec(h.element);
  leaf["first"] = dec(h.first);
  leaf["count"] = dec(h.count);
  leaf["runs"] = std::move(runs);
  return leaf;
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string block_line(const Meta& meta, const IndexEntry& entry, const ByteBuffer& payload) {
  sic::support::Json line = sic::support::Json::object_value();
  line["chunk"] = sic::support::Json::string_value(entry.type_name());
  line["codec"] = dec(0);
  line["flags"] = dec(1);
  line["leaves"] = sic::support::Json::array_value();
  for (const auto& span : leaf_spans(payload))
    line["leaves"].array.push_back(
        leaf_json(meta, std::span(payload).subspan(span.offset, span.length)));
  return sic::support::dump_json(line);
}
}  // namespace sic::sicx

namespace sic::sicx {
sic::support::Json ball_part_json(const BallPart& part) {
  sic::support::flint::Fmpz v;
  fmpz_set_ui_array(v, reinterpret_cast<const ulong*>(part.mantissa.data()),
                    static_cast<slong>(part.mantissa.size()));
  if (part.negative) fmpz_neg(v, v);
  sic::support::Json j = sic::support::Json::array_value();
  j.array = {dec_fmpz(v), sic::support::Json::string_value(std::to_string(part.mid_exp)),
             dec(part.rad_man), sic::support::Json::string_value(std::to_string(part.rad_exp))};
  return j;
}
}  // namespace sic::sicx

namespace sic::sicx {
BallPart ball_part_from_json(const sic::support::Json& j, unsigned limbs) {
  const auto& a = j.as_array();
  if (a.size() != 4) fail(sic::ErrorKind::invalid_format, "BALL part must be [M, E, m, e]");
  sic::support::flint::Fmpz v;
  const std::string& m = a[0].as_str();
  const size_t body = !m.empty() && m[0] == '-' ? 1 : 0;
  if (body == m.size() || m.find_first_not_of("0123456789", body) != std::string::npos ||
      (m.size() - body > 1 && m[body] == '0') || m == "-0" || fmpz_set_str(v, m.c_str(), 10)) {
    fail(sic::ErrorKind::invalid_format, "invalid BALL mantissa");
  }
  std::string hex_m = fmpz_hex(v);
  const int64_t rad_man = a[2].as_int();
  if (rad_man < 0) fail(sic::ErrorKind::invalid_format, "negative BALL radius mantissa");
  const std::string text =
      hex_m + ' ' + hex_i64(a[1].as_int()) + ' ' + hex_i64(rad_man) + ' ' + hex_i64(a[3].as_int());
  return parse_ball_part(text, limbs);
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string chunk_line(const Reader& reader, size_t i) {
  const Meta& meta = reader.meta();
  const auto& index = reader.index();
  const std::string type = index.at(i).type_name();
  if (type == "TOWR" || type == "BODY") return block_line(meta, index[i], reader.chunk(i));
  const auto& e = index[i];
  sic::support::Json line = sic::support::Json::object_value();
  line["chunk"] = sic::support::Json::string_value(type);
  line["flags"] = dec(e.flags);
  if (type == "EXMT")
    line["meta"] = reader.json_chunk(i);
  else if (type == "PREC" || type == "PROV" || type == "RAYS")
    line["json"] = reader.json_chunk(i);
  else if (type == "BALL") {
    const auto parts = reader.ball_parts(e.object);
    line["ray"] = dec(e.object);
    line["label"] = sic::support::Json::string_value(meta.rays.at(e.object).label);
    sic::support::Json values = sic::support::Json::array_value();
    for (uint64_t j = 0; j < meta.d; ++j) {
      sic::support::Json entry = sic::support::Json::array_value();
      entry.array = {ball_part_json(parts[2 * j]), ball_part_json(parts[2 * j + 1])};
      values.array.push_back(std::move(entry));
    }
    line["values"] = std::move(values);
  } else {
    line["object"] = dec(e.object);
    line["first"] = dec(e.first);
    line["count"] = dec(e.count);
    line["hex"] = sic::support::Json::string_value(hex(reader.chunk(i)));
  }
  return sic::support::dump_json(line);
}
}  // namespace sic::sicx

namespace sic::sicx {
void dump_text_json(const Reader& reader, const std::string& sicx_sha256, LineSink& sink,
                    unsigned threads) {
  const Meta& meta = reader.meta();
  support::sha256::Hasher content;
  uint64_t lines = 0;
  auto emit = [&](const std::string& text) {
    content.update(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    content.update(reinterpret_cast<const uint8_t*>("\n"), 1);
    sink.line(text);
    ++lines;
  };
  sic::support::Json header = sic::support::Json::object_value();
  header["schema"] = sic::support::Json::string_value(kTextSchema);
  header["sicx_version"] = sic::support::Json::array_value();
  header["sicx_version"].array = {dec(kMajor), dec(reader.minor())};
  header["required_features"] = sic::support::Json::array_value();
  header["content_hash"] = sic::support::Json::string_value("sha256");
  emit(sic::support::dump_json(header));
  // EXMT is rendered first (readers of the text need it to interpret the blocks), then
  // every other chunk in file order.
  emit(chunk_line(reader, reader.meta_chunk()));
  const auto& index = reader.index();
  const size_t window = std::max(1u, threads) * 2;
  for (size_t i = 0; i < index.size();) {
    const std::string type = index[i].type_name();
    if (i == reader.meta_chunk()) {
      ++i;
      continue;
    }
    if (type == "TOWR" || type == "BODY") {
      size_t stop = i;
      while (stop < index.size() && stop - i < window &&
             (index[stop].type_name() == "TOWR" || index[stop].type_name() == "BODY"))
        ++stop;
      std::vector<std::string> out(stop - i);
      parallel_for(stop - i, threads,
                   [&](size_t q) { out[q] = block_line(meta, index[i + q], reader.chunk(i + q)); });
      for (const auto& text : out) emit(text);
      i = stop;
      continue;
    }
    emit(chunk_line(reader, i));
    ++i;
  }
  sic::support::Json final_line = sic::support::Json::object_value();
  final_line["content_sha256"] = sic::support::Json::string_value(hex(content.finish()));
  final_line["exact_root"] = sic::support::Json::string_value(hex(reader.exact_root()));
  final_line["lines"] = dec(lines);
  final_line["merkle_root"] = sic::support::Json::string_value(hex(reader.root()));
  final_line["sicx_bytes"] = dec(reader.size());
  final_line["sicx_sha256"] = sic::support::Json::string_value(sicx_sha256);
  sink.line(sic::support::dump_json(final_line));
}
}  // namespace sic::sicx

namespace sic::sicx {
StreamSink::StreamSink(std::ostream& out) : out_(out) {}
}  // namespace sic::sicx

namespace sic::sicx {
void StreamSink::line(const std::string& text) try {
  out_ << text << '\n';
  if (!out_) fail(sic::ErrorKind::io, "text write failed");
} catch (const std::ios_base::failure& error) {
  fail(sic::ErrorKind::io, error.what());
} catch (const sic::Error&) {
  throw;
} catch (const std::overflow_error&) {
  throw;
} catch (const std::runtime_error& error) {
  // A throwing stream buffer may rethrow its original I/O exception.
  if (out_.bad()) fail(sic::ErrorKind::io, error.what());
  throw;
}
}  // namespace sic::sicx

namespace sic::sicx {
void PartSink::close_part() {
  if (!out_.is_open()) return;
  out_.close();
  if (!out_) fail(sic::ErrorKind::io, "part write failed");
  sic::support::Json p = sic::support::Json::object_value();
  p["name"] = sic::support::Json::string_value(name_);
  p["bytes"] = dec(bytes_);
  p["lines"] = dec(lines_);
  p["sha256"] = sic::support::Json::string_value(hex(hasher_->finish()));
  parts_.array.push_back(p);
}
}  // namespace sic::sicx

namespace sic::sicx {
PartSink::PartSink(std::filesystem::path base, uint64_t limit)
    : base_(std::move(base)), limit_(limit) {}
}  // namespace sic::sicx

namespace sic::sicx {
void PartSink::line(const std::string& text) {
  if (!out_.is_open() || bytes_ >= limit_) {
    close_part();
    char number[16];
    std::snprintf(number, sizeof number, "%05zu", parts_.array.size());
    name_ = base_.filename().string() + ".part-" + number + ".jsonl";
    const auto path = base_.parent_path() / name_;
    if (sic::sicx::filesystem_io([&] { return std::filesystem::exists(path); }))
      fail(sic::ErrorKind::io, "part exists: " + path.string());
    out_.open(path, std::ios::binary);
    hasher_ = std::make_unique<support::sha256::Hasher>();
    bytes_ = lines_ = 0;
  }
  out_ << text << '\n';
  hasher_->update(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  hasher_->update(reinterpret_cast<const uint8_t*>("\n"), 1);
  bytes_ += text.size() + 1;
  ++lines_;
}
}  // namespace sic::sicx

namespace sic::sicx {
void PartSink::finish() {
  close_part();
  sic::support::Json index = sic::support::Json::object_value();
  index["schema"] = sic::support::Json::string_value("sicx-text-json-parts/1.0");
  index["parts"] = parts_;
  const auto path = base_.parent_path() / (base_.filename().string() + ".parts.json");
  if (sic::sicx::filesystem_io([&] { return std::filesystem::exists(path); }))
    fail(sic::ErrorKind::io, "parts index exists");
  try {
    support::atomic_write(path, support::dump_json(index) + '\n', false, 0666);
  } catch (const sic::Error& error) {
    if (error.kind() != ErrorKind::io) throw;
    fail(ErrorKind::io, "parts index write failed");
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
bool LineSource::open_next() {
  if (in_.is_open()) {
    if (!hashes_.empty() &&
        (hex(part_hash_->finish()) != hashes_[file_ - 1] ||
         part_bytes_ != expected_bytes_[file_ - 1] || part_lines_ != expected_lines_[file_ - 1]))
      fail(sic::ErrorKind::integrity, "text part digest/byte/line count mismatch");
    in_.close();
  }
  if (file_ == files_.size()) return false;
  in_.open(files_[file_++], std::ios::binary);
  if (!in_) fail(sic::ErrorKind::io, "cannot open text part");
  part_hash_ = std::make_unique<support::sha256::Hasher>();
  part_bytes_ = part_lines_ = 0;
  return true;
}
}  // namespace sic::sicx

namespace sic::sicx {
LineSource::LineSource(const std::filesystem::path& path) {
  if (path.string().ends_with(".parts.json")) {
    std::string text = read_bounded_text(path);
    if (!text.empty() && text.back() == '\n') text.pop_back();
    const sic::support::Json index = sic::support::JsonParser::parse(text);
    index.keys({"schema", "parts"});
    if (index.at("schema").as_str() != "sicx-text-json-parts/1.0")
      fail(sic::ErrorKind::unsupported, "unknown parts index schema");
    if (index.at("parts").as_array().empty())
      fail(ErrorKind::invalid_format, "empty text parts list");
    if (index.at("parts").as_array().size() > kMaxChunks)
      fail(sic::ErrorKind::resource, "text parts count resource refusal");
    for (const auto& p : index.at("parts").as_array()) {
      p.keys({"name", "bytes", "lines", "sha256"});
      const std::string& name = p.at("name").as_str();
      if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos ||
          name.find(char(0)) != std::string::npos)
        fail(sic::ErrorKind::invalid_format, "part names must be plain file names");
      files_.push_back(path.parent_path() / name);
      hashes_.push_back(p.at("sha256").as_str());
      expected_bytes_.push_back(p.at("bytes").as_uint());
      expected_lines_.push_back(p.at("lines").as_uint());
    }
  } else
    files_.push_back(path);
  open_next();
}
}  // namespace sic::sicx

namespace sic::sicx {
bool LineSource::next(std::string& text) {
  for (;;) {
    if (bounded_line(in_, text, uint64_t(1) << 30)) {
      part_bytes_ = checked_add(part_bytes_, checked_add(text.size(), 1));
      ++part_lines_;
      part_hash_->update(reinterpret_cast<const uint8_t*>(text.data()), text.size());
      part_hash_->update(reinterpret_cast<const uint8_t*>("\n"), 1);
      return true;
    }
    if (in_.bad()) fail(sic::ErrorKind::io, "text read failed");
    if (!open_next()) return false;
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
namespace {
void parse_header_record(const sic::support::Json& header) {
  header.keys({"schema", "sicx_version", "required_features", "content_hash"});
  if (header.at("schema").as_str() != kTextSchema)
    fail(sic::ErrorKind::unsupported, "not a sicx-text-json/1.0 stream");
  if (!header.at("required_features").as_array().empty())
    fail(sic::ErrorKind::unsupported, "text form requires an unsupported feature");
  if (header.at("content_hash").as_str() != "sha256")
    fail(sic::ErrorKind::unsupported, "unsupported content hash");
  const auto v = header.at("sicx_version").as_uints<uint64_t>();
  if (v.size() != 2) fail(ErrorKind::invalid_format, "invalid SICX version in text form");
  if (v[0] != kMajor || v[1] != kMinor)
    fail(sic::ErrorKind::unsupported, "unsupported SICX version in text form");
}

struct ChunkRecord {
  std::string type;
  uint32_t flags;
};

ChunkRecord parse_chunk_record(const sic::support::Json& line, bool has_meta) {
  std::string type = line.at("chunk").as_str();
  if (type.size() != 4) fail(sic::ErrorKind::invalid_format, "invalid chunk type");
  const uint64_t flag_value = line.at("flags").as_uint();
  if (flag_value > 1) fail(sic::ErrorKind::invalid_format, "chunk flags must be 0 or 1");
  const uint32_t flags = static_cast<uint32_t>(flag_value);
  if (!known_type(type)) fail(sic::ErrorKind::invalid_format, "undefined text chunk");
  if (!has_meta && type != "EXMT")
    fail(sic::ErrorKind::invalid_format, "EXMT must be the first chunk line");
  return {std::move(type), flags};
}

Meta parse_exmt_record(const sic::support::Json& line, bool has_meta, uint64_t lines) {
  line.keys({"chunk", "flags", "meta"});
  if (has_meta || lines != 2)
    fail(sic::ErrorKind::invalid_format, "EXMT must be the first chunk line (and unique)");
  return meta_from_json(line.at("meta"));
}

std::string parse_prec_record(const sic::support::Json& line) {
  line.keys({"chunk", "flags", "json"});
  return sic::support::dump_json(line.at("json"));
}

std::string parse_prov_record(const sic::support::Json& line) {
  line.keys({"chunk", "flags", "json"});
  return sic::support::dump_json(line.at("json"));
}

std::string parse_rays_record(const sic::support::Json& line, Meta& meta) {
  line.keys({"chunk", "flags", "json"});
  rays_from_json(line.at("json"), meta);
  return sic::support::dump_json(line.at("json"));
}

struct ElementRecord {
  ByteBuffer payload;
  uint64_t element = 0, first = 0, count = 0;
};

// TOWR and BODY share the leaf grammar and physical grouping rules.
ElementRecord parse_element_leaves(const std::vector<sic::support::Json>& leaves,
                                   const Meta& meta) {
  ElementRecord record;
  auto& [payload, element, first, count] = record;
  for (const auto& leaf : leaves) {
    leaf.keys({"element", "first", "count", "runs"});
    const auto id = leaf.at("element").as_uint(), start = leaf.at("first").as_uint(),
               n = leaf.at("count").as_uint();
    if (id >= meta.elements.size() || n != meta.elements[id].block || start % n ||
        start >= meta.elements[id].layout.coordinates())
      fail(sic::ErrorKind::invalid_format, "text logical boundary");
    if (payload.empty()) {
      element = id;
      first = start;
    }
    if (id != element || start != checked_add(first, count))
      fail(sic::ErrorKind::invalid_format, "text physical grouping");
    std::vector<uint64_t> subs;
    std::vector<std::unique_ptr<FmpzPair>> values;
    uint64_t at = start;
    const auto end = checked_add(start, n);
    int last = -1;
    for (const auto& run : leaf.at("runs").as_array()) {
      const auto& r = run.as_array();
      if (r.size() != 2) fail(sic::ErrorKind::invalid_format, "text run shape");
      const auto& kind = r[0].as_str();
      const int now = kind == "zeros" ? 0 : kind == "pairs" ? 1 : -1;
      if (now < 0 || now == last) fail(sic::ErrorKind::invalid_format, "text nonmaximal runs");
      last = now;
      if (!now) {
        const auto zeros = r[1].as_uint();
        if (!zeros || zeros > end - at)
          fail(sic::ErrorKind::invalid_format, "text zero run length");
        at += zeros;
      } else {
        const auto& pairs = r[1].as_array();
        if (pairs.empty() || pairs.size() > end - at)
          fail(sic::ErrorKind::invalid_format, "text data run length");
        for (const auto& xy : pairs) {
          if (xy.as_array().size() != 2) fail(sic::ErrorKind::invalid_format, "text pair shape");
          auto pair = std::make_unique<FmpzPair>();
          for (unsigned j = 0; j < 2; ++j) {
            const auto& str = xy.array[j].as_str();
            const size_t b = !str.empty() && str[0] == '-' ? 1 : 0;
            if (str.size() > 3 * kMaxIntegerBytes || b == str.size() ||
                str.find_first_not_of("0123456789", b) != std::string::npos ||
                (str.size() - b > 1 && str[b] == '0') || str == "-0" ||
                fmpz_set_str(j ? pair->y : pair->x, str.c_str(), 10))
              fail(sic::ErrorKind::invalid_format, "text decimal integer");
          }
          if (fmpz_is_zero(pair->x) && fmpz_is_zero(pair->y))
            fail(sic::ErrorKind::invalid_format, "text zero data pair");
          subs.push_back(at++);
          values.push_back(std::move(pair));
        }
      }
    }
    if (at != end) fail(sic::ErrorKind::invalid_format, "text run coverage");
    BlockEncoder encoder(meta.D0);
    const auto bytes = encoder.encode(id, start, n, subs.size(), [&](size_t i) {
      return std::tuple<uint64_t, const fmpz*, const fmpz*>(subs[i], values[i]->x, values[i]->y);
    });
    if (bytes.size() > kMaxBlockBytes - std::min<uint64_t>(payload.size(), kMaxBlockBytes))
      fail(sic::ErrorKind::resource, "text physical chunk resource limit");
    payload.insert(payload.end(), bytes.begin(), bytes.end());
    count = checked_add(count, n);
  }
  return record;
}

ElementRecord parse_towr_record(const sic::support::Json& line, uint32_t flags,
                                const std::optional<Meta>& meta) {
  if (!meta) fail(sic::ErrorKind::invalid_format, "element line before EXMT");
  line.keys({"chunk", "codec", "flags", "leaves"});
  if (line.at("codec").as_uint() || flags != 1 || line.at("leaves").as_array().empty())
    fail(sic::ErrorKind::invalid_format, "element line codec/flags/leaves");
  return parse_element_leaves(line.at("leaves").as_array(), *meta);
}

ElementRecord parse_body_record(const sic::support::Json& line, uint32_t flags,
                                const std::optional<Meta>& meta) {
  if (!meta) fail(sic::ErrorKind::invalid_format, "element line before EXMT");
  line.keys({"chunk", "codec", "flags", "leaves"});
  if (line.at("codec").as_uint() || flags != 1 || line.at("leaves").as_array().empty())
    fail(sic::ErrorKind::invalid_format, "element line codec/flags/leaves");
  return parse_element_leaves(line.at("leaves").as_array(), *meta);
}

struct BallRecord {
  ByteBuffer payload;
  uint64_t ray;
};

BallRecord parse_ball_record(const sic::support::Json& line, const std::optional<Meta>& meta) {
  if (!meta) fail(sic::ErrorKind::invalid_format, "BALL before EXMT");
  line.keys({"chunk", "flags", "ray", "label", "values"});
  const uint64_t ray = line.at("ray").as_uint();
  if (ray >= meta->rays.size() || line.at("label").as_str() != meta->rays[ray].label)
    fail(sic::ErrorKind::invalid_format, "BALL label disagrees with EXMT");
  const auto& values = line.at("values").as_array();
  if (values.size() != meta->d) fail(sic::ErrorKind::invalid_format, "BALL needs d entries");
  ByteBuffer payload;
  for (const auto& entry : values) {
    const auto& parts = entry.as_array();
    if (parts.size() != 2) fail(sic::ErrorKind::invalid_format, "BALL entry must be [re, im]");
    for (const auto& part : parts)
      put_ball_part(payload, ball_part_from_json(part, meta->ball_limbs));
  }
  return {std::move(payload), ray};
}

void parse_final_record(const sic::support::Json& final_line, support::sha256::Hasher& content,
                        uint64_t lines) {
  final_line.keys(
      {"content_sha256", "exact_root", "lines", "merkle_root", "sicx_bytes", "sicx_sha256"});
  if (final_line.at("content_sha256").as_str() != hex(content.finish()))
    fail(ErrorKind::integrity, "text content hash mismatch");
  if (final_line.at("lines").as_uint() != lines)
    fail(ErrorKind::invalid_format, "text line count mismatch");
}
}  // namespace

std::string text_json_to_binary(const std::filesystem::path& in_path,
                                const std::filesystem::path& final_path) {
  if (sic::sicx::filesystem_io([&] { return std::filesystem::exists(final_path); }))
    fail(sic::ErrorKind::io, "text output exists");
  const std::filesystem::path out_path = final_path.string() + ".rebuild";
  LineSource source(in_path);
  support::sha256::Hasher content;
  uint64_t lines = 0;
  std::string text;
  auto next = [&](bool hash) -> const std::string& {
    if (!source.next(text)) fail(sic::ErrorKind::invalid_format, "text form truncated");
    if (hash) {
      content.update(reinterpret_cast<const uint8_t*>(text.data()), text.size());
      content.update(reinterpret_cast<const uint8_t*>("\n"), 1);
      ++lines;
    }
    return text;
  };
  {
    const sic::support::Json header = sic::support::JsonParser::parse(next(true));
    parse_header_record(header);
  }
  std::optional<Meta> meta;
  sic::support::Json final_line;
  {
    StreamWriter writer(out_path, StreamOptions{false, false, {}, false});
    for (;;) {
      const std::string& raw = next(false);
      const sic::support::Json line = sic::support::JsonParser::parse(raw);
      if (!line.has("chunk")) {
        final_line = line;
        break;
      }
      content.update(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
      content.update(reinterpret_cast<const uint8_t*>("\n"), 1);
      ++lines;
      const auto [type, flags] = parse_chunk_record(line, meta.has_value());
      if (type == "EXMT") {
        meta = parse_exmt_record(line, meta.has_value(), lines);
        const std::string payload = sic::support::dump_json(line.at("meta"));
        writer.chunk(type, flags,
                     std::span(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
      } else if (type == "PREC" || type == "PROV" || type == "RAYS") {
        const std::string payload = type == "PREC"   ? parse_prec_record(line)
                                    : type == "PROV" ? parse_prov_record(line)
                                                     : parse_rays_record(line, *meta);
        writer.chunk(type, flags,
                     std::span(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
      } else if (type == "TOWR" || type == "BODY") {
        const auto record = type == "TOWR" ? parse_towr_record(line, flags, meta)
                                           : parse_body_record(line, flags, meta);
        writer.chunk(type, flags, record.payload, record.element, record.first, record.count);
      } else if (type == "BALL") {
        const auto record = parse_ball_record(line, meta);
        writer.chunk("BALL", flags, record.payload, record.ray, 0, meta->d);
      } else
        fail(sic::ErrorKind::invalid_format, "undefined text chunk");
    }
    parse_final_record(final_line, content, lines);
    std::string extra;
    if (source.next(extra))
      fail(sic::ErrorKind::invalid_format, "trailing lines after the final line");
    if (hex(writer.exact_root()) != final_line.at("exact_root").as_str())
      fail(sic::ErrorKind::integrity, "text exact root mismatch");
    const Digest root = writer.finish();
    if (hex(root) != final_line.at("merkle_root").as_str()) {
      std::error_code ec;
      std::filesystem::remove(out_path, ec);
      fail(sic::ErrorKind::integrity, "rebuilt Merkle root differs from the text form");
    }
  }
  // The output must pass the full reader verification and render back to the
  // identical text (same content hash and line count): byte identity both ways.
  try {
    const std::string sha = support::sha256::file_hex(out_path);
    if (sha != final_line.at("sicx_sha256").as_str() ||
        sic::sicx::filesystem_io([&] { return std::filesystem::file_size(out_path); }) !=
            final_line.at("sicx_bytes").as_uint())
      fail(sic::ErrorKind::integrity,
           "rebuilt .sicx is not byte-identical to the recorded SHA-256");
    Reader reader(out_path);
    (void)verify_all(reader, 1);
    struct HashSink : LineSink {
      support::sha256::Hasher h;
      uint64_t lines = 0;
      std::string last;
      void line(const std::string& text) override {
        if (!last.empty()) {
          h.update(reinterpret_cast<const uint8_t*>(last.data()), last.size());
          h.update(reinterpret_cast<const uint8_t*>("\n"), 1);
          ++lines;
        }
        last = text;
      }
    } sink;
    dump_text_json(reader, sha, sink, 1);
    if (hex(sink.h.finish()) != final_line.at("content_sha256").as_str() || sink.lines != lines ||
        sink.last != sic::support::dump_json(final_line))
      fail(sic::ErrorKind::invalid_format,
           "text form is not the canonical rendering of the rebuilt file");
    const int fd = ::open(out_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) fail(sic::ErrorKind::io, "cannot sync reconstructed file");
    try {
      sync_fd(fd, "reconstructed file");
    } catch (...) {
      ::close(fd);
      throw;
    }
    ::close(fd);
    if (sic::sicx::filesystem_io([&] { return std::filesystem::exists(final_path); }))
      fail(sic::ErrorKind::io, "text output appeared during reconstruction");
    sic::sicx::rename_file(out_path, final_path, "text_reconstruction");
    sync_dir(final_path);
    return sha;
  } catch (...) {
    std::error_code ec;
    std::filesystem::remove(out_path, ec);
    throw;
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
PartSink::~PartSink() = default;
LineSource::~LineSource() = default;
}  // namespace sic::sicx
