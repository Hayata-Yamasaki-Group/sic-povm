// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/container/reader.hpp"

#include <algorithm>
#include <cstring>

#include "sicx/codec/ball.hpp"
#include "sicx/codec/integer.hpp"
#include "sicx/container/directory.hpp"
#include "sicx/container/leaf_index.hpp"

namespace sic::sicx {
void Reader::State::read(uint8_t* p, size_t n, uint64_t offset) const {
  if (offset > size_ || n > size_ - offset)
    fail(sic::ErrorKind::invalid_format, "read outside source");
  source_->read(offset, {p, n});
  std::lock_guard lock(stats_mutex_);
  bytes_read_ += n;
}
}  // namespace sic::sicx

namespace sic::sicx {
ByteBuffer Reader::State::read_directory(std::string_view type, uint64_t offset, uint64_t length,
                                         size_t width) {
  if (length < 8 || (length - 8) % width || (length - 8) / width == 0)
    fail(sic::ErrorKind::invalid_format, "invalid " + std::string(type) + " length");
  if ((length - 8) / width > limits_.directory_entries)
    fail(ErrorKind::resource, "directory entry resource limit");
  uint8_t head[16];
  read(head, 16, offset);
  if (std::memcmp(head, type.data(), 4) || get_le(head + 4, 4) != 1 ||
      get_le(head + 8, 8) != length)
    fail(sic::ErrorKind::invalid_format, "directory header mismatch");
  ByteBuffer bytes(length);
  read(bytes.data(), bytes.size(), checked_add(offset, 16));
  if (get_le(bytes.data(), 8) != (length - 8) / width)
    fail(sic::ErrorKind::invalid_format, "directory count mismatch");
  return bytes;
}
}  // namespace sic::sicx

namespace sic::sicx {
void Reader::State::validate_coverage() {
  if (logical_.empty() || logical_[0].type_name() != "EXMT" || logical_[0].element ||
      logical_[0].first || logical_[0].count != 1)
    fail(sic::ErrorKind::invalid_format, "EXMT LDIR entry");
  element_blocks_.resize(meta_.elements.size());
  size_t li = 1;
  for (size_t id = 0; id < meta_.elements.size(); ++id) {
    const auto& e = meta_.elements[id];
    for (uint64_t first = 0; first < e.layout.coordinates(); first += e.block) {
      if (li >= logical_.size()) fail(sic::ErrorKind::invalid_format, "missing logical leaf");
      const auto& leaf = logical_[li];
      if (leaf.type_name() != (e.kind == "rep" ? "BODY" : "TOWR") || leaf.element != id ||
          leaf.first != first || leaf.count != e.block)
        fail(sic::ErrorKind::invalid_format, "logical boundary/order mismatch");
      element_blocks_[id].push_back(li++);
    }
  }
  if (li != logical_.size()) fail(sic::ErrorKind::invalid_format, "extra logical leaves");
  owners_.assign(logical_.size(), SIZE_MAX);
  size_t next_leaf = 0;
  for (size_t i = 0; i < index_.size(); ++i) {
    const auto& p = index_[i];
    const auto type = p.type_name();
    if (type == "EXMT") {
      if (p.first_leaf || p.leaf_count != 1 || p.object || p.first || p.count)
        fail(sic::ErrorKind::invalid_format, "EXMT PDIR mapping");
    } else if (type == "TOWR" || type == "BODY") {
      if (!p.leaf_count || p.first_leaf != next_leaf || p.first_leaf >= logical_.size() ||
          p.leaf_count > logical_.size() - p.first_leaf)
        fail(sic::ErrorKind::invalid_format, "PDIR leaf coverage");
      uint64_t end = p.first;
      for (uint64_t j = 0; j < p.leaf_count; ++j) {
        const auto& leaf = logical_[p.first_leaf + j];
        if (leaf.type != p.type || leaf.element != p.object || leaf.first != end)
          fail(sic::ErrorKind::invalid_format, "PDIR leaf mapping");
        end = checked_add(end, leaf.count);
      }
      if (end - p.first != p.count) fail(sic::ErrorKind::invalid_format, "PDIR coordinate sum");
    } else {
      if (p.first_leaf || p.leaf_count)
        fail(sic::ErrorKind::invalid_format, "non-leaf chunk claims leaves");
      continue;
    }
    if (p.first_leaf != next_leaf || p.leaf_count > owners_.size() - next_leaf)
      fail(sic::ErrorKind::invalid_format, "duplicate leaf coverage");
    for (uint64_t j = 0; j < p.leaf_count; ++j) owners_[next_leaf++] = i;
  }
  if (next_leaf != logical_.size())
    fail(sic::ErrorKind::invalid_format, "uncovered logical leaves");
}
}  // namespace sic::sicx

namespace sic::sicx {
std::shared_ptr<const ByteBuffer> Reader::State::verified_chunk(size_t i,
                                                                std::vector<ParsedLeaf>* parsed,
                                                                uint64_t* parsed_bytes) const {
  const auto& e = index_.at(i);
  const auto type = e.type_name();
  const sic::support::ByteRange range{e.offset, checked_add(16, e.length)};
  source_->plan({&range, 1});
  try {
    uint8_t head[16];
    read(head, 16, e.offset);
    if (std::memcmp(head, e.type.data(), 4) || get_le(head + 4, 4) != e.flags ||
        get_le(head + 8, 8) != e.length) {
      fail(sic::ErrorKind::invalid_format, "chunk header differs from PDIR");
    }
    if (checked_mul(e.length, 4) > limits_.reader_memory_bytes)
      throw Error(ErrorKind::resource, "SICX: payload memory policy exceeded");
    auto bytes = std::make_shared<ByteBuffer>(e.length);
    read(bytes->data(), bytes->size(), e.offset + 16);
    if (sha256(bytes->data(), bytes->size()) != e.sha256) {
      throw Error(ErrorKind::integrity, "SICX: physical payload digest mismatch");
    }
    const auto leaves = chunk_leaves(e, *bytes, limits_);
    if (leaves.size() != e.leaf_count)
      fail(sic::ErrorKind::invalid_format, "physical/logical leaf count mismatch");
    for (size_t j = 0; j < leaves.size(); ++j) {
      ByteBuffer a, b;
      leaves[j].put(a);
      logical_.at(e.first_leaf + j).put(b);
      if (a != b) fail(sic::ErrorKind::integrity, "logical leaf digest/header mismatch");
    }
    if (type == "TOWR" || type == "BODY") {
      const auto spans = leaf_spans(*bytes, limits_);
      // Optional integer retention is an accelerator, never a validity rule.
      // Its complete allocation is capped at three times the encoded payload;
      // sparse or expensive representations fall back to decoding stable bytes.
      const uint64_t quota = checked_mul(bytes->size(), 3);
      uint64_t retained = checked_add(256, checked_mul(spans.size(), sizeof(ParsedLeaf)));
      if (parsed) {
        for (const auto& span : spans)
          if (span.nonzero)
            retained = checked_add(retained, checked_add(32, checked_mul(span.length, 2)));
        if (retained > quota) parsed = nullptr;
      }
      if (parsed) {
        parsed->resize(spans.size());
        for (size_t j = 0; j < spans.size(); ++j) {
          (*parsed)[j].count = spans[j].nonzero;
          if (spans[j].nonzero) (*parsed)[j].values.reserve(checked_mul(spans[j].length, 2));
        }
      }
      IntCodec codec;
      PairTransform transform(meta_.D0);
      FmpzPair v, full;
      fmpz_t scale;
      fmpz_init(scale);
      try {
        const auto& element = meta_.elements.at(e.object);
        fmpz_set_str(scale, element.scale.c_str(), 10);
        for (size_t j = 0; j < spans.size(); ++j) {
          const auto& span = spans[j];
          decode_block(
              std::span(*bytes).subspan(span.offset, span.length), meta_,
              [&](uint64_t subindex, const fmpz_t x, const fmpz_t y) {
                fmpz_mul(full.x, x, scale);
                fmpz_mul(full.y, y, scale);
                if (std::max(fmpz_bits(full.x), fmpz_bits(full.y)) > element.max_bits)
                  fail(sic::ErrorKind::invalid_format, "leaf exceeds max_bits");
                if (parsed) {
                  auto& buffer = (*parsed)[j].values;
                  // Thirty bytes cover the three varints. Check before any
                  // append can expand beyond the pre-admitted allocation.
                  const uint64_t extra =
                      checked_add(30, (fmpz_bits(full.x) + 7) / 8 + (fmpz_bits(full.y) + 7) / 8);
                  if (extra > buffer.capacity() - buffer.size()) {
                    std::vector<ParsedLeaf>().swap(*parsed);
                    parsed = nullptr;
                  } else {
                    put_varint(buffer, subindex);
                    codec.put(buffer, full.x);
                    codec.put(buffer, full.y);
                  }
                }
              },
              [](uint64_t, uint64_t) {}, codec, transform, v);
        }
      } catch (...) {
        fmpz_clear(scale);
        throw;
      }
      fmpz_clear(scale);
      if (parsed && parsed_bytes) *parsed_bytes = retained;
    }
    return bytes;
  } catch (const Error& error) {
    // Only corruption retires fragments. Cleanup must preserve the read or
    // validation failure that caused retirement.
    if (error.kind() == ErrorKind::integrity || error.kind() == ErrorKind::invalid_format) {
      try {
        source_->invalidate(range);
      } catch (...) {
      }
    }
    throw;
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
const ResourceLimits& Reader::State::resource_limits() const noexcept { return limits_; }
}  // namespace sic::sicx

namespace sic::sicx {
Reader::State::State(const std::filesystem::path& p, ResourceLimits limits)
    : State(sic::support::local_byte_source(p), limits) {}
}  // namespace sic::sicx

namespace sic::sicx {
Reader::State::State(std::shared_ptr<sic::support::ByteSource> source, ResourceLimits limits)
    : source_(std::move(source)), limits_(limits) {
  if (!source_) fail(sic::ErrorKind::invalid_argument, "null source");
  size_ = source_->size();
  if (size_ < 16 + 112 + 2 * 24) fail(sic::ErrorKind::invalid_format, "file too short");
  std::vector<sic::support::ByteRange> bootstrap{{0, 16}, {size_ - 112, 112}};
  try {
    uint8_t header[16], footer[112];
    read(header, 16, 0);
    read(footer, 112, size_ - 112);
    if (!std::equal(kMagic.begin(), kMagic.end(), header))
      fail(ErrorKind::invalid_format, "invalid header magic");
    if (get_le(header + 8, 2) != 1 || get_le(header + 10, 2) != 0 || get_le(header + 12, 4))
      fail(sic::ErrorKind::unsupported, "unsupported header/version/features");
    if (!std::equal(kEndMagic.begin(), kEndMagic.end(), footer + 104) ||
        get_le(footer + 96, 8) != size_)
      fail(sic::ErrorKind::invalid_format, "invalid footer");
    ldir_offset_ = get_le(footer, 8);
    const auto ll = get_le(footer + 8, 8), po = get_le(footer + 16, 8), pl = get_le(footer + 24, 8);
    if (ldir_offset_ < 16 || checked_add(checked_add(ldir_offset_, 16), ll) != po ||
        checked_add(checked_add(checked_add(po, 16), pl), 112) != size_)
      fail(sic::ErrorKind::invalid_format, "directory geometry mismatch");
    // Admit raw directories, parsed entries, coverage maps and Merkle copies
    // together before reading either directory. JSON/EXMT has a separate bound.
    const auto directory_bytes = checked_add(ll, pl);
    const auto bootstrap_bytes = checked_mul(8, directory_bytes);
    if (bootstrap_bytes > limits_.reader_memory_bytes)
      throw Error(ErrorKind::resource, "SICX: memory budget too small for reader metadata");
    bootstrap.push_back({ldir_offset_, checked_add(16, ll)});
    bootstrap.push_back({po, checked_add(16, pl)});
    std::memcpy(exact_root_.data(), footer + 32, 32);
    std::memcpy(root_.data(), footer + 64, 32);
    const auto lt = read_directory("LDIR", ldir_offset_, ll, 64),
               pt = read_directory("PDIR", po, pl, 112);
    for (uint64_t i = 0; i < get_le(lt.data(), 8); ++i)
      logical_.push_back(LogicalEntry::get(lt.data() + 8 + i * 64));
    uint64_t next = 16;
    int rank = 0;
    bool seen_exmt = false, seen_prov = false, seen_rays = false, seen_prec = false;
    for (uint64_t i = 0; i < get_le(pt.data(), 8); ++i) {
      auto e = IndexEntry::get(pt.data() + 8 + i * 112);
      const auto type = e.type_name();
      if (!known_type(type)) fail(sic::ErrorKind::invalid_format, "undefined chunk type " + type);
      if (e.flags != ((type == "PROV" || type == "PREC") ? 0u : 1u))
        fail(sic::ErrorKind::invalid_format, "chunk flags mismatch");
      if (e.codec) fail(ErrorKind::unsupported, "unsupported codec");
      if (e.decoded_length != e.length) fail(ErrorKind::invalid_format, "raw length mismatch");
      admit_chunk(type, e.length, limits_);
      if (e.offset != next) fail(sic::ErrorKind::invalid_format, "physical chunks not contiguous");
      next = checked_add(checked_add(next, 16), e.length);
      if (next > ldir_offset_)
        fail(sic::ErrorKind::invalid_format, "chunk exceeds exact data range");
      const int r = type_rank(type);
      if (r < rank) fail(sic::ErrorKind::invalid_format, "chunk order");
      rank = r;
      if (type == "EXMT") {
        if (i || seen_exmt) fail(sic::ErrorKind::invalid_format, "EXMT must be first and unique");
        seen_exmt = true;
      }
      if (type == "PROV") {
        if (seen_prov) fail(sic::ErrorKind::invalid_format, "duplicate PROV");
        seen_prov = true;
        prov_ = i;
      }
      if (type == "RAYS") {
        if (seen_rays) fail(sic::ErrorKind::invalid_format, "duplicate RAYS");
        seen_rays = true;
        rays_chunk_ = i;
      }
      if (type == "PREC") {
        if (!seen_rays || seen_prec) fail(sic::ErrorKind::invalid_format, "PREC placement");
        seen_prec = true;
        prec_ = i;
      }
      if (type == "BALL") {
        if (!seen_rays) fail(sic::ErrorKind::invalid_format, "BALL without RAYS");
        ball_chunks_.push_back(i);
      }
      if ((type == "PROV" || type == "RAYS" || type == "PREC") && (e.object || e.first || e.count))
        fail(sic::ErrorKind::invalid_format, "nonzero PDIR fields");
      index_.push_back(e);
    }
    if (!seen_exmt || next != ldir_offset_)
      fail(sic::ErrorKind::invalid_format, "missing EXMT or gap before LDIR");
    if (merkle_root(logical_) != exact_root_ ||
        file_tree_root(index_, directory_chunk("LDIR", lt)) != root_)
      throw Error(ErrorKind::integrity, "SICX: directory root mismatch");
  } catch (const Error& error) {
    // A failed bootstrap has no authenticated attribution. Retire all fetched
    // bootstrap units and preserve the original validation diagnostic.
    if (error.kind() == ErrorKind::integrity || error.kind() == ErrorKind::invalid_format ||
        error.kind() == ErrorKind::unsupported)
      for (const auto range : bootstrap) {
        try {
          source_->invalidate(range);
        } catch (...) {
        }
      }
    throw;
  }
  // Validate EXMT's singleton mapping before using its routing information.
  const auto& e = index_[0];
  if (e.first_leaf || e.leaf_count != 1 || logical_[0].type_name() != "EXMT" ||
      logical_[0].element || logical_[0].first || logical_[0].count != 1)
    fail(sic::ErrorKind::invalid_format, "EXMT directory mapping");
  const auto directory_memory =
      checked_mul(8, checked_add(8 + logical_.size() * 64, 8 + index_.size() * 112));
  auto exmt_memory = checked_add(directory_memory, checked_mul(64, e.length));
  if (exmt_memory > limits_.reader_memory_bytes)
    throw Error(ErrorKind::resource, "SICX: EXMT memory policy exceeded");
  const auto bytes = chunk(0);
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  meta_json_ = sic::support::JsonParser::parse(text, limits_.json_depth);
  const auto dimension = meta_json_.at("d").as_uint();
  if (dimension <= kMaximumDimension)
    exmt_memory = checked_add(exmt_memory, checked_mul(dimension, sizeof(uint32_t)));
  if (exmt_memory > limits_.reader_memory_bytes)
    fail(ErrorKind::resource, "SICX: EXMT unit-group memory policy exceeded");
  meta_ = meta_from_json(meta_json_);
  if (sic::support::dump_json(meta_to_json(meta_)) != text)
    fail(sic::ErrorKind::invalid_format, "noncanonical EXMT");
  validate_coverage();
  source_->bind_identity(hex(exact_root_), hex(root_));
  std::vector<uint64_t> boundaries;
  for (const auto& entry : index_) boundaries.push_back(entry.offset + 16 + entry.length);
  boundaries.push_back(ldir_offset_);
  source_->bind_layout(boundaries, rays_chunk_ ? index_[*rays_chunk_].offset : ldir_offset_,
                       meta_.d);
  if (rays_chunk_) {
    if (checked_add(exmt_memory, checked_mul(64, index_[*rays_chunk_].length)) >
        limits_.reader_memory_bytes)
      fail(ErrorKind::resource, "RAYS memory policy exceeded");
    const auto rb = chunk(*rays_chunk_);
    const std::string_view rt(reinterpret_cast<const char*>(rb.data()), rb.size());
    rays_from_json(sic::support::JsonParser::parse(rt, limits_.json_depth), meta_);
    if (sic::support::dump_json(rays_to_json(meta_)) != rt)
      fail(sic::ErrorKind::invalid_format, "noncanonical RAYS");
    if (ball_chunks_.size() != meta_.rays.size())
      fail(sic::ErrorKind::invalid_format, "BALL count mismatch");
  }
  for (size_t ray = 0; ray < ball_chunks_.size(); ++ray) {
    const auto& b = index_[ball_chunks_[ray]];
    if (b.object != ray || b.first || b.count != meta_.d ||
        b.length != checked_mul(meta_.d, ball_record_bytes(meta_.ball_limbs)))
      fail(sic::ErrorKind::invalid_format, "BALL directory mapping/length");
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
const Meta& Reader::State::meta() const { return meta_; }
}  // namespace sic::sicx

namespace sic::sicx {
const sic::support::Json& Reader::State::meta_json() const { return meta_json_; }
}  // namespace sic::sicx

namespace sic::sicx {
const std::vector<IndexEntry>& Reader::State::index() const { return index_; }
}  // namespace sic::sicx

namespace sic::sicx {
const std::vector<LogicalEntry>& Reader::State::logical() const { return logical_; }
}  // namespace sic::sicx

namespace sic::sicx {
const Digest& Reader::State::root() const { return root_; }
}  // namespace sic::sicx

namespace sic::sicx {
const Digest& Reader::State::exact_root() const { return exact_root_; }
}  // namespace sic::sicx

namespace sic::sicx {
size_t Reader::State::exact_count() const { return exact_count_of(index_); }
}  // namespace sic::sicx

namespace sic::sicx {
size_t Reader::State::meta_chunk() const { return 0; }
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t Reader::State::ldir_offset() const { return ldir_offset_; }
}  // namespace sic::sicx

namespace sic::sicx {
bool Reader::State::finalized() const { return rays_chunk_.has_value(); }
}  // namespace sic::sicx

namespace sic::sicx {
std::optional<size_t> Reader::State::rays_chunk() const { return rays_chunk_; }
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t Reader::State::size() const { return size_; }
}  // namespace sic::sicx

namespace sic::sicx {
uint16_t Reader::State::minor() const { return 0; }
}  // namespace sic::sicx

namespace sic::sicx {
const std::vector<size_t>& Reader::State::element_blocks(size_t id) const {
  return element_blocks_.at(id);
}
}  // namespace sic::sicx

namespace sic::sicx {
size_t Reader::State::leaf_owner(size_t leaf) const { return owners_.at(leaf); }
}  // namespace sic::sicx

namespace sic::sicx {
IndexEntry Reader::State::leaf_entry(size_t leaf) const {
  const auto& l = logical_.at(leaf);
  IndexEntry e = index_.at(owners_.at(leaf));
  e.object = l.element;
  e.first = l.first;
  e.count = l.count;
  // Bound one logical leaf independently of how many share its physical
  // owner. max_bits bounds unscaled coordinates; the pair transform adds
  // at most 22 bits for every supported d. Include worst-case run/int tags.
  const uint64_t bits = meta_.elements.at(l.element).max_bits;
  const uint64_t per_coordinate = checked_add(checked_mul(2, (bits + 29) / 8 + 10), 10);
  e.length = std::min(e.length, checked_add(30, checked_mul(l.count, per_coordinate)));
  return e;
}
}  // namespace sic::sicx

namespace sic::sicx {
size_t Reader::State::ball_chunk(size_t ray) const { return ball_chunks_.at(ray); }
}  // namespace sic::sicx

namespace sic::sicx {
std::optional<size_t> Reader::State::prec() const { return prec_; }
}  // namespace sic::sicx

namespace sic::sicx {
std::optional<size_t> Reader::State::prov() const { return prov_; }
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t Reader::State::bytes_read() const {
  std::lock_guard lock(stats_mutex_);
  return bytes_read_;
}
}  // namespace sic::sicx

namespace sic::sicx {
void Reader::State::plan_chunks(std::span<const size_t> chunks) const {
  std::vector<sic::support::ByteRange> ranges;
  for (auto i : chunks) {
    const auto& e = index_.at(i);
    ranges.push_back({e.offset, checked_add(16, e.length)});
  }
  source_->plan(ranges);
}
}  // namespace sic::sicx

namespace sic::sicx {
void Reader::State::bind_cancellation(std::shared_ptr<sic::support::StopToken> token) const {
  source_->bind_cancellation(std::move(token));
}
}  // namespace sic::sicx

namespace sic::sicx {
bool Reader::State::remote() const { return source_->remote(); }
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t Reader::State::source_memory_reservation() const { return source_->memory_reservation(); }
}  // namespace sic::sicx

namespace sic::sicx {
void Reader::State::quiesce() const { source_->quiesce(); }
}  // namespace sic::sicx

namespace sic::sicx {
void Reader::State::schedule_chunks(std::span<const size_t> chunks) const {
  if (!remote()) return;
  std::vector<sic::support::ByteRange> ranges;
  for (auto i : chunks) {
    const auto& e = index_.at(i);
    ranges.push_back({e.offset, checked_add(16, e.length)});
  }
  source_->schedule(ranges);
}
}  // namespace sic::sicx

namespace sic::sicx {
ByteBuffer Reader::State::chunk(size_t i) const { return *verified_chunk(i); }
}  // namespace sic::sicx

namespace sic::sicx {
std::shared_ptr<const Reader::State::Cached> Reader::State::load_element_chunk(
    size_t owner, bool retain_integers) const {
  std::shared_ptr<const Cached> cached;
  std::shared_future<std::shared_ptr<const Cached>> pending;
  std::shared_ptr<std::promise<std::shared_ptr<const Cached>>> producer;
  // Verification temporarily holds logical descriptors and framing spans in
  // addition to the immutable retained unit. Bound before fetching/expansion.
  const auto bytes_needed = checked_add(
      checked_mul(index_[owner].length, retain_integers ? 4 : 1),
      checked_add(4ull << 20, checked_mul(index_[owner].leaf_count,
                                          2 * sizeof(LeafSpan) + 2 * sizeof(LogicalEntry))));
  const auto inflight_limit =
      std::min(limits_.inflight_verification_bytes, limits_.reader_memory_bytes);
  if (bytes_needed > limits_.chunk_verification_bytes || bytes_needed > inflight_limit)
    throw Error(ErrorKind::resource, "SICX: chunk verification resource refusal");
  {
    std::unique_lock lock(cache_mutex_);
    for (;;) {
      if (auto it = cache_.find(owner); it != cache_.end()) {
        cached = it->second;
        break;
      }
      if (auto it = loading_.find(owner); it != loading_.end()) {
        pending = it->second;
        break;
      }
      if (loading_bytes_ <= inflight_limit && bytes_needed <= inflight_limit - loading_bytes_) {
        producer = std::make_shared<std::promise<std::shared_ptr<const Cached>>>();
        pending = producer->get_future().share();
        loading_[owner] = pending;
        loading_bytes_ += bytes_needed;
        break;
      }
      cache_space_.wait(lock);
    }
  }
  if (producer) {
    bool reserved = true;
    try {
      std::vector<ParsedLeaf> parsed;
      uint64_t parsed_bytes = 0;
      auto bytes = verified_chunk(owner, retain_integers ? &parsed : nullptr, &parsed_bytes);
      auto spans = leaf_spans(*bytes, limits_);
      const auto retained =
          checked_add(checked_add(bytes->size(), parsed_bytes),
                      checked_add(256, checked_mul(spans.capacity(), sizeof(LeafSpan))));
      auto complete = std::make_shared<const Cached>(
          Cached{bytes, std::move(spans), std::move(parsed), retained});
      {
        std::lock_guard lock(cache_mutex_);
        if (retained <= limits_.cache_bytes) {
          while (!cache_.empty() && checked_add(cache_bytes_, retained) > limits_.cache_bytes) {
            cache_bytes_ -= cache_.begin()->second->retained;
            cache_.erase(cache_.begin());
          }
          cache_.emplace(owner, complete);
          cache_bytes_ += retained;
        }
        loading_bytes_ -= bytes_needed;
        loading_.erase(owner);
        reserved = false;
      }
      producer->set_value(std::move(complete));
      cache_space_.notify_all();
    } catch (...) {
      {
        std::lock_guard lock(cache_mutex_);
        if (reserved) {
          loading_bytes_ -= bytes_needed;
          loading_.erase(owner);
        }
      }
      producer->set_exception(std::current_exception());
      cache_space_.notify_all();
      throw;
    }
  }
  if (pending.valid()) cached = pending.get();
  return cached;
}
}  // namespace sic::sicx

namespace sic::sicx {
Reader::ElementChunk Reader::State::element_chunk(size_t owner, bool retain_integers) const {
  const auto& entry = index_.at(owner);
  if (entry.type_name() != "TOWR" && entry.type_name() != "BODY")
    fail(sic::ErrorKind::invalid_format, "non-element physical unit");
  auto cached = load_element_chunk(owner, retain_integers);
  return {cached->bytes, std::shared_ptr<const std::vector<LeafSpan>>(cached, &cached->spans),
          std::shared_ptr<const std::vector<ParsedLeaf>>(cached, &cached->parsed),
          entry.first_leaf};
}
}  // namespace sic::sicx

namespace sic::sicx {
ByteBuffer Reader::State::leaf(size_t li) const {
  if (li == 0) return chunk(owners_.at(li));
  const auto unit = element_chunk(owners_.at(li));
  const auto bytes = unit.leaf(li);
  return ByteBuffer(bytes.begin(), bytes.end());
}
}  // namespace sic::sicx

namespace sic::sicx {
sic::support::Json Reader::State::json_chunk(size_t i) const {
  const auto bytes = chunk(i);
  auto j = sic::support::JsonParser::parse(
      std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  const auto type = index_.at(i).type_name();
  if (type == "PROV" &&
      (j.kind != sic::support::Json::Kind::object || j.has("generator_rescaling")))
    fail(sic::ErrorKind::invalid_format, "invalid PROV");
  if (type == "PREC") {
    if (j.kind != sic::support::Json::Kind::object)
      fail(sic::ErrorKind::invalid_format, "PREC must be an object");
    for (const auto& [key, value] : j.object) {
      if (key != "branch_bits" && key != "base_path_bits" && key != "delta_bits" &&
          key != "max_element_bits")
        fail(sic::ErrorKind::invalid_format, "unknown PREC key");
      (void)value.as_uint();
    }
  }
  return j;
}
}  // namespace sic::sicx

namespace sic::sicx {
std::vector<BallPart> Reader::State::ball_parts(size_t ray) const {
  const auto bytes = chunk(ball_chunk(ray));
  std::vector<BallPart> parts;
  const auto half = ball_record_bytes(meta_.ball_limbs) / 2;
  for (uint64_t q = 0; q < 2 * meta_.d; ++q)
    parts.push_back(get_ball_part(bytes.data() + q * half, meta_.ball_limbs));
  arb_t norm, value, square;
  arb_init(norm);
  arb_init(value);
  arb_init(square);
  int64_t worst = INT64_MIN / 2;
  bool nonzero_radius = false;
  const slong precision = static_cast<slong>(meta_.ball_working_bits) + 64;
  try {
    for (size_t q = 0; q < parts.size(); ++q) {
      ball_part_arb(value, parts[q]);
      worst = std::max(worst, radius_log2_bound(parts[q]));
      nonzero_radius |= parts[q].rad_man != 0;
      if (q / 2 == meta_.rays[ray].anchor &&
          ((q & 1) ? !arb_is_zero(value) : !arb_is_positive(value)))
        fail(sic::ErrorKind::invalid_format, "BALL anchor gauge");
      arb_sqr(square, value, precision);
      arb_add(norm, norm, square, precision);
    }
    if (!nonzero_radius || -worst != meta_.rays[ray].accuracy_bits || !arb_contains_si(norm, 1))
      fail(sic::ErrorKind::invalid_format, "BALL accuracy or norm enclosure");
  } catch (...) {
    arb_clear(norm);
    arb_clear(value);
    arb_clear(square);
    throw;
  }
  arb_clear(norm);
  arb_clear(value);
  arb_clear(square);
  return parts;
}
}  // namespace sic::sicx
