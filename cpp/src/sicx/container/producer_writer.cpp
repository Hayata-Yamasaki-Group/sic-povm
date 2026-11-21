// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/container/producer_writer.hpp"

#include <atomic>

#include "sicx/codec/block.hpp"
#include "sicx/container/rescaling_input.hpp"
#include "sicx/container/verify.hpp"
#include "sicx/model/branches.hpp"
#include "support/admitted_worker_team.hpp"
#include "support/resources.hpp"
#include "support/runtime/reservations.hpp"

namespace sic::sicx {
sic::support::Json element_info_json(const ElementInfo& e) {
  sic::support::Json x = sic::support::Json::object_value();
  x["kind"] = sic::support::Json::string_value(e.kind);
  x["level"] = sic::support::Json::integer_value(e.level);
  x["axis"] = sic::support::Json::integer_value(e.axis);
  x["k"] = sic::support::Json::integer_value(e.k);
  x["position"] = sic::support::Json::integer_value(e.position);
  x["levels"] = sic::support::Json::integer_array(e.levels);
  x["scale"] = sic::support::Json::string_value(e.scale);
  x["max_bits"] = sic::support::Json::integer_value(static_cast<int64_t>(e.max_bits));
  return x;
}
}  // namespace sic::sicx

namespace sic::sicx {
ElementStats element_stats(const Meta& header, const ElementId& id, const LeafSource& source) {
  ElementStats st;
  ElementInfo& e = st.info;
  e.kind = id.kind;
  e.level = id.level;
  e.axis = id.axis;
  e.k = id.k;
  e.position = id.position;
  uint64_t mask = 0, last = 0;
  bool any = false;
  sic::support::flint::Fmpz g;
  LeafDigest digest;
  try {
    source([&](uint64_t pos, const fmpz* x, const fmpz* y) {
      if (fmpz_is_zero(x) && fmpz_is_zero(y)) return;
      if (any && pos <= last)
        fail(ErrorKind::invalid_format, "producer leaves are not in increasing position");
      if (pos >= header.width.at(id.level))
        fail(ErrorKind::invalid_format, "producer leaf outside its field");
      any = true;
      last = pos;
      digest.add(pos, x, y);
      fmpz_gcd(g, g, x);
      fmpz_gcd(g, g, y);
      uint64_t rest = pos;
      for (unsigned i = 0; i < id.level; ++i) {
        if (rest % header.degrees[i]) mask |= uint64_t(1) << i;
        rest /= header.degrees[i];
      }
      e.max_bits = std::max<uint64_t>(e.max_bits, std::max(fmpz_bits(x), fmpz_bits(y)));
      st.bits_sum += static_cast<double>(fmpz_bits(x) + fmpz_bits(y));
      ++st.nonzero;
    });
  } catch (...) {
    throw;
  }
  for (unsigned i = 0; i < id.level; ++i)
    if (mask >> i & 1) e.levels.push_back(i);
  if (fmpz_is_zero(g)) fmpz_one(g);
  std::unique_ptr<char, decltype(&flint_free)> text(fmpz_get_str(nullptr, 10, g), flint_free);
  e.scale = text.get();
  e.layout = SubLayout(header.degrees, header.width, e.levels);
  st.digest = digest.finish();
  return st;
}
}  // namespace sic::sicx

namespace sic::sicx {
ProducerWriter::ProducerWriter(const std::filesystem::path& path, const Meta& header,
                               const StreamOptions& options, uint64_t physical_target,
                               sic::PrecisionPolicy branch_policy, unsigned workers,
                               uint64_t buffer_limit)
    : header_(header),
      writer_(options.part ? path : std::filesystem::path(path.string() + ".spool"),
              spool_options(options)),
      path_(path),
      options_(options),
      physical_target_(physical_target),
      branch_policy_(branch_policy),
      workers_(workers),
      buffer_limit_(buffer_limit) {
  if (!workers_ || !buffer_limit_)
    throw std::invalid_argument("writer requires workers and buffers");
  if (!physical_target_ || physical_target_ > kMaxBlockBytes)
    fail(ErrorKind::resource, "physical chunk target resource limit");
  header_.block_coordinates = uint64_t(1) << kLogicalLeafLog2Limit;
  if (header_.rescaling_shifts.empty()) header_.rescaling_shifts.assign(header_.levels(), 0);
  header_.elements.clear();
  header_.rays.clear();
}
}  // namespace sic::sicx

namespace sic::sicx {
ProducerWriter::~ProducerWriter() {
  try {
    std::error_code ec;
    if (!preserve_spool_ && !options_.part && std::filesystem::exists(path_, ec)) {
      std::filesystem::remove(path_.string() + ".spool.partial", ec);
      std::filesystem::remove(path_.string() + ".spool.partial.journal", ec);
    }
  } catch (...) {
    // Best-effort scratch cleanup must never terminate exception unwinding.
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
void ProducerWriter::set_rescaling_shifts(std::vector<uint64_t> shifts) {
  header_.rescaling_shifts = std::move(shifts);
}
}  // namespace sic::sicx

namespace sic::sicx {
void ProducerWriter::set_branches(std::vector<std::array<std::string, 2>> branches) {
  header_.branches = std::move(branches);
}
}  // namespace sic::sicx

namespace sic::sicx {
bool ProducerWriter::done(const ElementId& id) {
  const ByteBuffer* note = writer_.note(note_key(offset_ + header_.elements.size()));
  if (!note) return false;
  const sic::support::Json j = sic::support::JsonParser::parse(
      std::string_view(reinterpret_cast<const char*>(note->data()), note->size()));
  ElementInfo e;
  e.kind = j.at("kind").as_str();
  e.level = static_cast<unsigned>(j.at("level").as_uint());
  e.axis = j.at("axis").as_int();
  e.k = j.at("k").as_int();
  e.position = j.at("position").as_int();
  e.levels = j.at("levels").as_uints<unsigned>();
  e.scale = j.at("scale").as_str();
  e.max_bits = j.at("max_bits").as_uint();
  if (e.kind != id.kind || e.level != id.level || e.axis != id.axis || e.k != id.k ||
      e.position != id.position)
    fail(ErrorKind::integrity, "resume: element sequence differs from the interrupted run");
  e.layout = SubLayout(header_.degrees, header_.width, e.levels);
  derive_blocks(e, header_.block_coordinates);
  if (writer_.pending_replay() < e.blocks()) return false;
  const auto& rec = writer_.resumed();
  const size_t base = writer_.index().size();
  for (uint64_t b = 0; b < e.blocks(); ++b)
    if (rec[base + b].object != offset_ + header_.elements.size() ||
        rec[base + b].first != b * e.block || rec[base + b].count != e.block ||
        rec[base + b].flags != kRequired ||
        rec[base + b].type_name() != (e.kind == "rep" ? "BODY" : "TOWR"))
      fail(ErrorKind::invalid_format, "resume: recovered blocks do not match the element note");
  check_order(e);
  writer_.adopt(e.blocks());
  for (uint64_t b = 0; b < e.blocks(); ++b)
    (e.kind == "rep" ? stats_.body_bytes : stats_.tower_bytes) +=
        rec[base + b].length + kChunkHeaderBytes;
  header_.elements.push_back(std::move(e));
  digests_.push_back(Digest{});  // not recomputed for adopted elements
  return true;
}
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t ProducerWriter::writer_workspace(const ElementInfo& e) {
  namespace admission = sic::support::admission;
  const auto count = std::min<uint64_t>(e.block, e.layout.coordinates());
  const auto pair = admission::add(128, admission::mul(2, admission::add(e.max_bits, 63) / 8));
  const auto block = admission::add(4096, admission::mul(count, admission::add(pair, 32)));
  return admission::add(8ull << 20, admission::mul(12, block));
}
}  // namespace sic::sicx

namespace sic::sicx {
ElementInfo ProducerWriter::add(const ElementId& id, const LeafSource& source,
                                const std::function<void(const ElementInfo&)>& admit) {
  const auto t0 = std::chrono::steady_clock::now();
  sic::support::check_stop();
  ElementStats st = element_stats(header_, id, source);
  stats_.pass1_seconds +=
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  ElementInfo e = std::move(st.info);
  derive_blocks(e, header_.block_coordinates);
  // Admit before the second source pass; retain the grant through every block commit.
  sic::support::resources::Budget memory;
  const auto workspace = writer_workspace(e);
  if (workspace > memory.remaining()) sic::support::resources::release_free_heap();
  const auto buffers = std::min<uint64_t>(
      {workers_, e.blocks(), buffer_limit_, std::max<uint64_t>(1, memory.remaining() / workspace)});
  const auto retained = sic::support::admission::mul(buffers, workspace);
  sic::support::Reservations grants(std::max<uint64_t>(retained, 1), [&](uint64_t bytes) {
    memory.require_additional("SICX writer retained buffers", bytes);
    return true;
  });
  auto grant = grants.try_acquire(std::max<uint64_t>(retained, 1));
  if (!grant) fail(ErrorKind::resource, "writer retained-buffer admission refused");
  stats_.peak_buffer_count = std::max(stats_.peak_buffer_count, buffers);
  stats_.peak_workspace_bytes = std::max(stats_.peak_workspace_bytes, retained);
  if (admit) admit(e);
  check_order(e);
  const uint64_t element = offset_ + header_.elements.size();
  const std::string type = e.kind == "rep" ? "BODY" : "TOWR";
  // Reader rules for this element alone (checked again in EXMT at the end).
  Meta probe = header_;
  probe.elements.assign(element, e);  // decode_block reads only elements[element]
  probe.elements.push_back(e);
  sic::support::flint::Fmpz g;
  fmpz_set_str(g, e.scale.c_str(), 10);
  uint64_t bytes = 0, block = 0;
  struct BlockBuffer {
    std::vector<uint64_t> subs;
    std::vector<std::unique_ptr<FmpzPair>> values;
    ByteBuffer encoded;
  };
  std::vector<BlockBuffer> batch(buffers);
  uint64_t batch_first = 0;
  double batch_seconds = 0;
  auto flush = [&] {
    const auto count = block - batch_first;
    if (!count) return;
    const auto batch_started = std::chrono::steady_clock::now();
    std::atomic<bool> cancelled{false};
    std::vector<std::exception_ptr> failures(count);
    struct BatchCancelled {};
    auto encode = [&](unsigned slot) {
      // This batch already owns its worker slots, including the caller's slot.
      const int saved_workers = flint_set_num_workers(0);
      try {
        uint64_t polls = 0;
        auto check = [&] {
          if ((polls++ & 1023) == 0) sic::support::check_stop();
          return !cancelled.load(std::memory_order_relaxed);
        };
        if (check()) {
          auto& buffer = batch[slot];
          BlockEncoder encoder(header_.D0);
          IntCodec codec;
          PairTransform transform(header_.D0);
          FmpzPair value;
          buffer.encoded = encoder.encode(
              element, (batch_first + slot) * e.block, e.block, buffer.subs.size(), [&](size_t i) {
                if (!check()) throw BatchCancelled{};
                return std::tuple<uint64_t, const fmpz*, const fmpz*>(
                    buffer.subs[i], buffer.values[i]->x, buffer.values[i]->y);
              });
          size_t i = 0;
          decode_block(
              buffer.encoded, probe,
              [&](uint64_t sub, const fmpz_t x, const fmpz_t y) {
                if (!check()) throw BatchCancelled{};
                if (i >= buffer.subs.size() || buffer.subs[i] != sub ||
                    !fmpz_equal(x, buffer.values[i]->x) || !fmpz_equal(y, buffer.values[i]->y))
                  throw std::logic_error("SICX: in-memory block round trip failed");
                ++i;
              },
              [](uint64_t, uint64_t) {}, codec, transform, value);
          if (i != buffer.subs.size())
            throw std::logic_error("SICX: in-memory block round trip lost pairs");
        }
      } catch (const BatchCancelled&) {
        // Peer cancellation is not a new failure and must not replace its cause.
      } catch (...) {
        failures[slot] = std::current_exception();
        cancelled.store(true, std::memory_order_relaxed);
      }
      flint_reset_num_workers(saved_workers);
    };
    sic::support::local_worker_team().run(static_cast<unsigned>(count), encode);
    stats_.parallel_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - batch_started).count();
    // Join makes every slot visible; preserve the lowest failed block's original exception.
    for (const auto& failure : failures)
      if (failure) std::rethrow_exception(failure);
    // Workers never touch the stream. A failed batch commits nothing, so its durable
    // prefix can be shorter than a serial writer's; resume accepts any durable prefix.
    for (uint64_t slot = 0; slot < count; ++slot) {
      sic::support::check_stop();
      auto& buffer = batch[slot];
      const auto committed = std::chrono::steady_clock::now();
      writer_.chunk(type, kRequired, buffer.encoded, element, (batch_first + slot) * e.block,
                    e.block);
      stats_.commit_seconds +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - committed).count();
      bytes += buffer.encoded.size() + kChunkHeaderBytes;
      ++stats_.blocks;
      buffer = BlockBuffer{};
    }
    batch_first = block;
    batch_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - batch_started).count();
  };
  auto complete_block = [&] {
    sic::support::check_stop();
    ++block;
    if (block - batch_first == buffers) flush();
  };
  const auto collection_started = std::chrono::steady_clock::now();
  source([&](uint64_t pos, const fmpz* x, const fmpz* y) {
    if (fmpz_is_zero(x) && fmpz_is_zero(y)) return;
    uint64_t rest = pos, sub = 0;
    size_t j = 0;
    for (unsigned i = 0; i < e.level; ++i) {
      const uint64_t digit = rest % header_.degrees[i];
      rest /= header_.degrees[i];
      if (j < e.levels.size() && e.levels[j] == i) {
        sub += digit * e.layout.sub_width[j];
        ++j;
      } else if (digit)
        throw std::logic_error(
            "SICX: producer leaf outside the element level set (second pass differs)");
    }
    const uint64_t b = sub / e.block;
    if (b < block)
      throw std::logic_error("SICX: producer leaves out of order (second pass differs)");
    while (block < b) complete_block();
    auto pair = std::make_unique<FmpzPair>();
    if (!fmpz_divisible(x, g) || !fmpz_divisible(y, g))
      throw std::logic_error("SICX: scale does not divide a leaf (second pass differs)");
    fmpz_divexact(pair->x, x, g);
    fmpz_divexact(pair->y, y, g);
    auto& buffer = batch[block - batch_first];
    buffer.subs.push_back(sub);
    buffer.values.push_back(std::move(pair));
  });
  while (block < e.blocks()) complete_block();
  flush();
  stats_.collection_seconds +=
      std::chrono::duration<double>(std::chrono::steady_clock::now() - collection_started).count() -
      batch_seconds;
  (e.kind == "rep" ? stats_.body_bytes : stats_.tower_bytes) += bytes;
  const std::string note = sic::support::dump_json(element_info_json(e));
  writer_.put_note(note_key(element), ByteBuffer(note.begin(), note.end()));
  header_.elements.push_back(std::move(e));
  digests_.push_back(st.digest);
  stats_.encode_seconds +=
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  return header_.elements.back();
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string ProducerWriter::write_meta() {
  const auto canonical = canonical_branches(
      header_,
      [&](size_t id, auto visit) {
        const auto& element = header_.elements.at(id);
        fmpz_t scale;
        fmpz_init(scale);
        fmpz_set_str(scale, element.scale.c_str(), 10);
        IntCodec codec;
        PairTransform transform(header_.D0);
        FmpzPair value, full;
        try {
          for (const auto& entry : writer_.index())
            if (entry.object == id) {
              const auto bytes = StreamWriter::read_chunk_at(
                  options_.part ? path_.string() + ".partial" : path_.string() + ".spool.partial",
                  entry, options_.limits);
              decode_block(
                  bytes, header_,
                  [&](uint64_t sub, const fmpz_t x, const fmpz_t y) {
                    fmpz_mul(full.x, x, scale);
                    fmpz_mul(full.y, y, scale);
                    visit(element.layout.position(sub), full.x, full.y);
                  },
                  [](uint64_t, uint64_t) {}, codec, transform, value);
            }
        } catch (...) {
          fmpz_clear(scale);
          throw;
        }
        fmpz_clear(scale);
      },
      branch_policy_.maximum_bits);
  header_.branch_bits = canonical.bits;
  header_.branches = canonical.rectangles;
  const std::string text = sic::support::dump_json(meta_to_json(header_));
  const Meta checked =
      meta_from_json(sic::support::JsonParser::parse(text, options_.limits.json_depth));
  for (size_t i = 0; i < header_.elements.size(); ++i)
    if (checked.elements[i].block != header_.elements[i].block)
      fail(ErrorKind::invalid_format, "block rule mismatch");
  StreamOptions options = options_;
  options.part = false;
  ByteBuffer plan(options.plan.begin(), options.plan.end());
  plan.insert(plan.end(), text.begin(), text.end());
  put_le(plan, physical_target_, 8);
  options.plan = sha256(plan.data(), plan.size());
  const auto parent = path_.has_parent_path() ? path_.parent_path() : std::filesystem::path(".");
  const uint64_t extra = checked_add(
      writer_.offset(),
      checked_add(text.size(),
                  checked_add(checked_mul(writer_.index().size() + 4, 192), kMaxTextChunk)));
  if (std::filesystem::space(parent).available < checked_add(extra, uint64_t(1) << 30))
    fail(ErrorKind::resource, "insufficient space for the final stream beside the private spool");
  uint64_t largest = 0;
  for (const auto& entry : writer_.index()) largest = std::max(largest, entry.length);
  sic::support::resources::Budget memory;
  memory.require_additional("SICX physical grouping buffers",
                            checked_mul(4, checked_add(physical_target_, largest)));
  published_ = std::make_unique<StreamWriter>(path_, options);
  published_->chunk("EXMT", kRequired,
                    std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
  ByteBuffer group;
  uint64_t object = 0, first = 0, count = 0;
  std::string type;
  auto flush = [&] {
    if (group.empty()) return;
    published_->chunk(type, kRequired, group, object, first, count);
    group.clear();
    count = 0;
  };
  for (const auto& e : writer_.index()) {
    const auto bytes = StreamWriter::read_chunk_at(
        options_.part ? path_.string() + ".partial" : path_.string() + ".spool.partial", e,
        options_.limits);
    if (!group.empty() &&
        (e.object != object ||
         bytes.size() > physical_target_ - std::min<uint64_t>(group.size(), physical_target_)))
      flush();
    if (group.empty()) {
      object = e.object;
      first = e.first;
      type = e.type_name();
    }
    group.insert(group.end(), bytes.begin(), bytes.end());
    count += e.count;
  }
  flush();
  return text;
}
}  // namespace sic::sicx

namespace sic::sicx {
void ProducerWriter::set_first_element(uint64_t first) {
  if (!header_.elements.empty() || writer_.index().size())
    throw std::logic_error("SICX: set_first_element before the first element only");
  offset_ = first;
}
}  // namespace sic::sicx

namespace sic::sicx {
void ProducerWriter::finish_part(const std::filesystem::path& path, const std::string& source) {
  const auto side_path = path.string() + ".elements.json";
  if (std::filesystem::exists(side_path)) fail(ErrorKind::io, "output exists: " + side_path);
  sic::support::Json side = sic::support::Json::object_value();
  side["source"] = sic::support::Json::string_value(source);
  Meta header = header_;
  header.elements.clear();
  side["header"] = meta_to_json(header);
  side["first"] = sic::support::Json::integer_value(static_cast<int64_t>(offset_));
  sic::support::Json list = sic::support::Json::array_value();
  for (const auto& e : header_.elements) list.array.push_back(element_info_json(e));
  side["elements"] = list;
  const std::string text = sic::support::dump_json(side);
  std::ofstream out(side_path, std::ios::binary);
  out << text;
  if (!out) fail(ErrorKind::io, "cannot write " + side_path);
  out.close();
  writer_.finish();
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string ProducerWriter::note_key(uint64_t element) {
  return "element " + std::to_string(element);
}
}  // namespace sic::sicx

namespace sic::sicx {
void ProducerWriter::check_order(const ElementInfo& e) const {
  if (!header_.elements.empty() && !element_less(header_.elements.back(), e))
    fail(ErrorKind::invalid_format, "producer elements are not in canonical order");
}
}  // namespace sic::sicx

namespace sic::sicx {
StreamOptions ProducerWriter::spool_options(StreamOptions o) {
  o.part = true;
  return o;
}
}  // namespace sic::sicx

namespace sic::sicx {
Digest merge_element_parts(const std::filesystem::path& out,
                           const std::vector<std::filesystem::path>& parts,
                           const StreamOptions& options) {
  std::string header_text, source;
  sic::support::Json header;
  sic::support::Json elements = sic::support::Json::array_value();
  uint64_t expect = 0;
  for (const auto& part : parts) {
    std::ifstream in(part.string() + ".elements.json", std::ios::binary);
    if (!in) fail(ErrorKind::io, "cannot read " + part.string() + ".elements.json");
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    const sic::support::Json side = sic::support::JsonParser::parse(text);
    side.keys({"source", "header", "first", "elements"});
    if (side.at("source").as_str().empty())
      fail(ErrorKind::invalid_format, "private element parts require a nonempty source identity");
    if (source.empty())
      source = side.at("source").as_str();
    else if (side.at("source").as_str() != source)
      fail(ErrorKind::integrity, "parts come from different sources: " + part.string());
    const std::string h = sic::support::dump_json(side.at("header"));
    if (header_text.empty()) {
      header_text = h;
      header = side.at("header");
    } else if (h != header_text)
      fail(ErrorKind::integrity, "part headers differ: " + part.string());
    if (side.at("first").as_uint() != expect)
      fail(ErrorKind::invalid_format, "part element ranges are not contiguous: " + part.string());
    for (const auto& j : side.at("elements").as_array()) {
      sic::support::Json x = sic::support::Json::object_value();
      const std::string kind = j.at("kind").as_str();
      x["kind"] = sic::support::Json::string_value(kind);
      x["levels"] = j.at("levels");
      x["max_bits"] = j.at("max_bits");
      x["scale"] = j.at("scale");
      if (kind == "rep")
        x["position"] = j.at("position");
      else
        x["level"] = j.at("level");
      if (j.at("axis").as_int() >= 0) x["axis"] = j.at("axis");
      if (j.at("k").as_int() >= 0) x["k"] = j.at("k");
      elements.array.push_back(x);
      ++expect;
    }
  }
  header["elements"] = elements;
  const Meta meta = meta_from_json(header);
  struct PartBlock {
    std::filesystem::path path;
    IndexEntry entry;
  };
  std::vector<std::vector<PartBlock>> blocks(meta.elements.size());
  for (const auto& part : parts) {
    std::ifstream in(part.string() + ".idx", std::ios::binary);
    const ByteBuffer idx((std::istreambuf_iterator<char>(in)), {});
    if (idx.size() < 48 || std::memcmp(idx.data(), "SICXPRT1", 8))
      fail(ErrorKind::invalid_format, "invalid private part index");
    const auto n = get_le(idx.data() + 8, 8);
    if (n > kMaxChunks || idx.size() != checked_add(48, checked_mul(n, kIndexEntryBytes)))
      fail(ErrorKind::invalid_format, "private part index length");
    std::vector<IndexEntry> entries;
    uint64_t end = 0;
    for (uint64_t i = 0; i < n; ++i) {
      const auto e = IndexEntry::get(idx.data() + 16 + i * kIndexEntryBytes);
      if (e.object >= blocks.size() || e.offset != end ||
          (e.type_name() != "TOWR" && e.type_name() != "BODY"))
        fail(ErrorKind::invalid_format, "private part mapping");
      end = checked_add(checked_add(end, 16), e.length);
      blocks[e.object].push_back({part, e});
      entries.push_back(e);
    }
    if (end != std::filesystem::file_size(part))
      fail(ErrorKind::invalid_format, "private part trailing bytes");
    const auto root = merkle_root(entries);
    if (!std::equal(root.begin(), root.end(), idx.end() - 32))
      fail(ErrorKind::integrity, "private part root");
  }
  StreamOptions validated = options;
  validated.part = false;
  validated.before_publish = [hook = options.before_publish](const auto& private_path) {
    Reader r(private_path);
    (void)verify_all(r, 1);
    if (hook) hook(private_path);
  };
  ProducerWriter producer(out, meta, validated);
  for (size_t id = 0; id < meta.elements.size(); ++id) {
    const auto& element = meta.elements[id];
    producer.add({element.kind, element.level, element.axis, element.k, element.position},
                 [&](const LeafVisitor& visit) {
                   IntCodec codec;
                   PairTransform transform(meta.D0);
                   FmpzPair value, full;
                   fmpz_t scale;
                   fmpz_init(scale);
                   fmpz_set_str(scale, element.scale.c_str(), 10);
                   uint64_t first = 0;
                   try {
                     for (const auto& b : blocks[id]) {
                       if (b.entry.first != first || b.entry.count != element.block)
                         fail(ErrorKind::invalid_format, "private part logical coverage");
                       const auto payload = StreamWriter::read_chunk_at(b.path, b.entry);
                       const auto h = decode_block(
                           payload, meta,
                           [&](uint64_t sub, const fmpz_t x, const fmpz_t y) {
                             fmpz_mul(full.x, x, scale);
                             fmpz_mul(full.y, y, scale);
                             visit(element.layout.position(sub), full.x, full.y);
                           },
                           [](uint64_t, uint64_t) {}, codec, transform, value);
                       if (h.element != id || h.first != first)
                         fail(ErrorKind::invalid_format, "private part leaf header");
                       first += element.block;
                     }
                     if (first != element.layout.coordinates())
                       fail(ErrorKind::invalid_format, "missing private part leaves");
                   } catch (...) {
                     fmpz_clear(scale);
                     throw;
                   }
                   fmpz_clear(scale);
                 });
  }
  producer.write_meta();
  const Digest root = producer.writer().finish();
  return root;
}
}  // namespace sic::sicx
