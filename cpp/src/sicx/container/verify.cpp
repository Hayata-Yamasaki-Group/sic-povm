// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/sicx/verify.hpp"

#include <tuple>

#include "sic/support/flint_raii.hpp"
#include "sicx/codec/ball.hpp"
#include "sicx/codec/integer.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/codec/rescaling_rule.hpp"
#include "sicx/container/verify.hpp"
#include "sicx/container/verify.tpp"
#include "sicx/model/branches.hpp"

namespace sic::sicx {
BlockResult check_block(const Reader& reader, size_t chunk_index, bool want_stream) {
  const Meta& meta = reader.meta();
  const ByteBuffer payload = reader.leaf(chunk_index);
  const auto& entry = reader.leaf_entry(chunk_index);
  const auto& element = meta.elements.at(entry.object);
  IntCodec codec, out_codec;
  PairTransform transform(meta.D0);
  FmpzPair value;
  fmpz_t scale, full_x, full_y, g;
  fmpz_init(scale);
  fmpz_init(full_x);
  fmpz_init(full_y);
  fmpz_init(g);
  fmpz_set_str(scale, element.scale.c_str(), 10);
  std::vector<std::unique_ptr<FmpzPair>> xs;
  std::vector<uint64_t> subs;
  BlockResult result;
  result.payload_bytes = payload.size();
  ByteBuffer buffer;
  try {
    const auto header = decode_block(
        payload, meta,
        [&](uint64_t s, const fmpz_t x, const fmpz_t y) {
          auto pair = std::make_unique<FmpzPair>();
          fmpz_set(pair->x, x);
          fmpz_set(pair->y, y);
          fmpz_gcd(g, g, x);
          fmpz_gcd(g, g, y);
          fmpz_mul(full_x, x, scale);
          fmpz_mul(full_y, y, scale);
          const uint64_t bits = std::max(fmpz_bits(full_x), fmpz_bits(full_y));
          if (bits > element.max_bits)
            fail(sic::ErrorKind::invalid_format, "leaf exceeds the element max_bits");
          result.max_bits = std::max(result.max_bits, bits);
          uint64_t rest = s;
          for (size_t j = 0; j < element.layout.levels.size(); ++j) {
            const uint64_t p = element.layout.sub_width[j + 1] / element.layout.sub_width[j];
            if (rest % p) result.level_mask |= uint64_t(1) << j;
            rest /= p;
          }
          if (want_stream) {
            put_varint(result.stream, element.layout.position(s));
            out_codec.put(result.stream, full_x);
            out_codec.put(result.stream, full_y);
          }
          ++result.nonzero;
          subs.push_back(s);
          xs.push_back(std::move(pair));
        },
        [](uint64_t, uint64_t) {}, codec, transform, value);
    if (header.element != entry.object || header.first != entry.first ||
        header.count != entry.count)
      fail(sic::ErrorKind::invalid_format, "block header disagrees with LDIR");
    BlockEncoder encoder(meta.D0);
    const ByteBuffer again =
        encoder.encode(header.element, header.first, header.count, subs.size(), [&](size_t i) {
          return std::tuple<uint64_t, const fmpz*, const fmpz*>(subs[i], xs[i]->x, xs[i]->y);
        });
    if (again != payload)
      fail(sic::ErrorKind::invalid_format, "block is not in canonical encoding");
    result.gcd = fmpz_hex(g);
  } catch (...) {
    fmpz_clear(scale);
    fmpz_clear(full_x);
    fmpz_clear(full_y);
    fmpz_clear(g);
    throw;
  }
  fmpz_clear(scale);
  fmpz_clear(full_x);
  fmpz_clear(full_y);
  fmpz_clear(g);
  return result;
}
}  // namespace sic::sicx

namespace sic::sicx {
ElementCheck check_element(const Reader& reader, size_t id, unsigned threads) {
  const auto& element = reader.meta().elements.at(id);
  const auto& blocks = reader.element_blocks(id);
  ElementCheck check;
  LeafDigest digest;
  uint64_t mask = 0;
  sic::support::flint::Fmpz g, part;
  for (size_t start = 0; start < blocks.size(); start += std::max(1u, threads)) {
    const size_t stop = std::min(blocks.size(), start + std::max(1u, threads));
    std::vector<BlockResult> results(stop - start);
    parallel_for(stop - start, threads,
                 [&](size_t i) { results[i] = check_block(reader, blocks[start + i], true); });
    for (auto& r : results) {
      digest.add_bytes(r.stream, r.nonzero);
      check.max_bits = std::max(check.max_bits, r.max_bits);
      check.payload_bytes += r.payload_bytes;
      mask |= r.level_mask;
      fmpz_set_str(part, r.gcd.c_str(), 16);
      fmpz_gcd(g, g, part);
    }
  }
  check.nonzero = digest.nonzero();
  check.digest = digest.finish();
  const bool zero = check.nonzero == 0;
  const bool gcd_ok = zero ? element.scale == "1" : fmpz_is_one(g);
  if (!gcd_ok)
    fail(sic::ErrorKind::invalid_format,
         "element " + std::to_string(id) +
             ": stored coordinates are not primitive (gcd folded into scale)");
  if (check.max_bits != element.max_bits)
    fail(sic::ErrorKind::invalid_format,
         "element " + std::to_string(id) + ": max_bits is not attained");
  const uint64_t all = element.layout.levels.size() == 64
                           ? ~uint64_t(0)
                           : (uint64_t(1) << element.layout.levels.size()) - 1;
  if (mask != all)
    fail(sic::ErrorKind::invalid_format,
         "element " + std::to_string(id) + ": level set is not minimal");
  return check;
}
}  // namespace sic::sicx

namespace sic::sicx {
BallCheck check_balls(const Reader& reader, size_t ray) {
  const Meta& meta = reader.meta();
  const auto parts = reader.ball_parts(ray);
  const slong prec = static_cast<slong>(std::max<uint64_t>(meta.ball_working_bits, 64)) + 64;
  arb_t re, im, norm, t;
  arb_init(re);
  arb_init(im);
  arb_init(norm);
  arb_init(t);
  int64_t worst = INT64_MIN / 2;
  std::string norm_text;
  try {
    for (uint64_t j = 0; j < meta.d; ++j) {
      const auto& a = parts[2 * j];
      const auto& b = parts[2 * j + 1];
      ball_part_arb(re, a);
      ball_part_arb(im, b);
      sic::support::flint::String text(arb_dump_str(re));
      const bool same = ball_part_text(a) == text.get();
      text.reset();
      text.reset(arb_dump_str(im));
      const bool same_im = ball_part_text(b) == text.get();
      text.reset();
      if (!same || !same_im)
        fail(sic::ErrorKind::invalid_format,
             "BALL record does not round-trip through the Arb dump form");
      if (!arb_is_finite(re) || !arb_is_finite(im))
        fail(sic::ErrorKind::invalid_format, "nonfinite BALL entry");
      worst = std::max({worst, radius_log2_bound(a), radius_log2_bound(b)});
      if (j == meta.rays[ray].anchor && (!arb_is_zero(im) || !arb_is_positive(re)))
        fail(sic::ErrorKind::invalid_format,
             "anchor entry is not real positive with exact zero imaginary part");
      arb_sqr(t, re, prec);
      arb_add(norm, norm, t, prec);
      arb_sqr(t, im, prec);
      arb_add(norm, norm, t, prec);
    }
    arb_sub_ui(t, norm, 1, prec);
    if (!arb_contains_zero(t))
      fail(sic::ErrorKind::invalid_format, "BALL vector norm does not contain 1");
    sic::support::flint::String text(arb_get_str(norm, 20, 0));
    norm_text = text.get();
    text.reset();
  } catch (...) {
    arb_clear(re);
    arb_clear(im);
    arb_clear(norm);
    arb_clear(t);
    throw;
  }
  arb_clear(re);
  arb_clear(im);
  arb_clear(norm);
  arb_clear(t);
  if (-worst != meta.rays[ray].accuracy_bits)
    fail(sic::ErrorKind::invalid_format, "ray accuracy_bits disagrees with the BALL radii");
  return {-worst, norm_text};
}
}  // namespace sic::sicx

namespace sic::sicx {
void check_branches(const Meta& meta) {
  arb_t x;
  arb_init(x);
  for (const auto& pair : meta.branches)
    for (const auto& part : pair) {
      if (arb_load_str(x, part.c_str()) || !arb_is_finite(x)) {
        arb_clear(x);
        fail(sic::ErrorKind::invalid_format, "invalid branch rectangle");
      }
      char* text = arb_dump_str(x);
      const bool same = part == text;
      flint_free(text);
      if (!same) {
        arb_clear(x);
        fail(sic::ErrorKind::invalid_format, "branch rectangle is not in canonical Arb dump form");
      }
    }
  arb_clear(x);
}
}  // namespace sic::sicx

namespace sic::sicx {
void check_rescaling(const Reader& reader, unsigned threads, ValuationPolicy policy) {
  const auto& m = reader.meta();
  RescalingCollector residual(m, policy), original(m, policy);
  for (size_t id = 0; id < m.elements.size(); ++id) {
    const auto& e = m.elements[id];
    const ElementId key{e.kind, e.level, e.axis, e.k, e.position};
    auto source = [&](const LeafVisitor& visit, bool undo) {
      IntCodec codec;
      PairTransform transform(m.D0);
      FmpzPair pair, full;
      fmpz_t scale;
      fmpz_init(scale);
      fmpz_set_str(scale, e.scale.c_str(), 10);
      try {
        for (auto li : reader.element_blocks(id)) {
          const auto bytes = reader.leaf(li);
          decode_block(
              bytes, m,
              [&](uint64_t sub, const fmpz_t x, const fmpz_t y) {
                fmpz_mul(full.x, x, scale);
                fmpz_mul(full.y, y, scale);
                const uint64_t pos = e.layout.position(sub);
                if (undo) {
                  uint64_t shift =
                      checked_mul(own_weight(key, m.degrees),
                                  e.kind == "rep" ? 0 : m.rescaling_shifts.at(e.level));
                  uint64_t rest = pos;
                  for (unsigned i = 0; i < e.level; ++i) {
                    shift =
                        checked_add(shift, checked_mul(rest % m.degrees[i], m.rescaling_shifts[i]));
                    rest /= m.degrees[i];
                  }
                  if (shift > 8 * kMaxIntegerBytes)
                    fail(sic::ErrorKind::resource, "rescaling reconstruction resource refusal");
                  fmpz_mul_2exp(full.x, full.x, shift);
                  fmpz_mul_2exp(full.y, full.y, shift);
                }
                visit(pos, full.x, full.y);
              },
              [](uint64_t, uint64_t) {}, codec, transform, pair);
        }
      } catch (...) {
        fmpz_clear(scale);
        throw;
      }
      fmpz_clear(scale);
    };
    residual.add(key, [&](const LeafVisitor& visit) { source(visit, false); });
    original.add(key, [&](const LeafVisitor& visit) { source(visit, true); });
  }
  if (!residual.compute(threads).identity())
    fail(sic::ErrorKind::invalid_format, "residual generator rescaling is not canonical");
  if (original.compute(threads).shifts != m.rescaling_shifts)
    fail(sic::ErrorKind::invalid_format,
         "recorded generator shifts do not reproduce the greedy rule");
}
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t bounds_memory(const Reader& reader, bool complete, ValuationPolicy policy) {
  const auto& meta = reader.meta();
  uint64_t scratch = 8ull << 20, owner_scratch = 0, cache_growth = 0;
  for (size_t id = 0; id < meta.elements.size(); ++id) {
    const uint64_t integer_bytes = (meta.elements[id].max_bits + 39) / 8;
    for (auto li : reader.element_blocks(id)) {
      const auto leaf = reader.leaf_entry(li);
      // Full scaled integer streams, stored pairs/vectors, re-encoding and
      // vector growth: tiny encoded integers can expand after scaling.
      scratch = std::max(
          scratch,
          checked_add(8ull << 20,
                      checked_add(checked_mul(4, leaf.length),
                                  checked_mul(leaf.count,
                                              checked_add(512, checked_mul(12, integer_bytes))))));
    }
  }
  for (const auto& owner : reader.index()) {
    if (owner.type_name() != "TOWR" && owner.type_name() != "BODY") continue;
    const auto framing =
        checked_mul(owner.leaf_count, 2 * sizeof(LeafSpan) + 2 * sizeof(LogicalEntry));
    owner_scratch =
        std::max(owner_scratch, checked_add(owner.length, checked_add(4ull << 20, framing)));
    cache_growth = std::min<uint64_t>(
        256ull << 20,
        checked_add(cache_growth, checked_add(owner.length, checked_add(256, framing))));
  }
  // Two simultaneous dense valuation collectors; their storage is governed
  // by field widths, not by compressed leaf sizes or sparsity.
  std::set<std::pair<unsigned, uint64_t>> weights;
  uint64_t table_bytes = policy.admit(meta.D, 0);
  for (const auto& e : meta.elements) {
    const auto weight = own_weight({e.kind, e.level, e.axis, e.k, e.position}, meta.degrees);
    if (weight && weights.emplace(e.level, weight).second) {
      const auto bytes = policy.admit(meta.width[e.level], table_bytes);
      table_bytes += bytes;
    }
  }
  scratch = checked_add(
      scratch, checked_add(checked_mul(2, table_bytes), checked_add(owner_scratch, cache_growth)));
  if (complete) {
    uint64_t ball_work = 0;
    for (const auto& e : reader.index()) {
      if (e.type_name() == "BALL") {
        const auto work =
            checked_add(checked_mul(4, e.length), checked_mul(4 * meta.d, sizeof(BallPart) + 16));
        ball_work = std::max(ball_work, work);
      } else if (e.type_name() == "PROV" || e.type_name() == "PREC") {
        scratch = checked_add(scratch, checked_mul(64, e.length));
      }
    }
    const auto report_bytes = checked_mul(
        meta.rays.size(), checked_add(sizeof(BallCheck) + 128, meta.ball_working_bits / 2));
    scratch = checked_add(scratch, checked_add(ball_work, report_bytes));
  }
  return scratch;
}
}  // namespace sic::sicx

namespace sic::sicx {
VerifyReport verify_exact(const Reader& reader, unsigned threads, VerificationPolicy policy) {
  VerifyReport report;
  const Meta& meta = reader.meta();
  check_branches(meta);
  check_rescaling(reader, threads, policy.valuation);
  for (size_t id = 0; id < meta.elements.size(); ++id)
    report.elements.push_back(check_element(reader, id, threads));
  const auto branches = canonical_branches(
      meta,
      [&](size_t id, auto visit) {
        const auto& e = meta.elements.at(id);
        fmpz_t scale;
        fmpz_init(scale);
        fmpz_set_str(scale, e.scale.c_str(), 10);
        IntCodec codec;
        PairTransform transform(meta.D0);
        FmpzPair value, full;
        try {
          for (auto li : reader.element_blocks(id)) {
            const auto bytes = reader.leaf(li);
            decode_block(
                bytes, meta,
                [&](uint64_t sub, const fmpz_t x, const fmpz_t y) {
                  fmpz_mul(full.x, x, scale);
                  fmpz_mul(full.y, y, scale);
                  visit(e.layout.position(sub), full.x, full.y);
                },
                [](uint64_t, uint64_t) {}, codec, transform, value);
          }
        } catch (...) {
          fmpz_clear(scale);
          throw;
        }
        fmpz_clear(scale);
      },
      policy.precision.maximum_bits);
  if (branches.bits != meta.branch_bits || branches.rectangles != meta.branches)
    fail(sic::ErrorKind::invalid_format, "noncanonical branch precision/rectangle");
  return report;
}
}  // namespace sic::sicx

namespace sic::sicx {
VerifyReport verify_all(const Reader& reader, unsigned threads, VerificationPolicy policy) {
  auto report = verify_exact(reader, threads, policy);
  const Meta& meta = reader.meta();
  for (size_t ray = 0; ray < meta.rays.size(); ++ray)
    report.balls.push_back(check_balls(reader, ray));
  if (reader.prec()) {
    const auto j = reader.json_chunk(*reader.prec());
    for (const auto& [key, value] : j.object) {
      if (key != "branch_bits" && key != "base_path_bits" && key != "delta_bits" &&
          key != "max_element_bits")
        fail(sic::ErrorKind::invalid_format, "unknown PREC key");
      (void)value.as_uint();
    }
    if (j.kind != sic::support::Json::Kind::object)
      fail(sic::ErrorKind::invalid_format, "PREC must be an object");
  }
  if (reader.prov()) {
    const auto j = reader.json_chunk(*reader.prov());
    if (j.kind != sic::support::Json::Kind::object || j.has("generator_rescaling"))
      fail(sic::ErrorKind::invalid_format, "invalid PROV");
  }
  // The closed grammar and the checks above cover every ordinary chunk.
  report.chunks = reader.index().size();
  report.bytes = reader.size();
  return report;
}
}  // namespace sic::sicx

namespace sic::sicx {
void LeafDigest::add(uint64_t position, const fmpz_t x, const fmpz_t y) {
  buffer_.clear();
  put_varint(buffer_, position);
  codec_.put(buffer_, x);
  codec_.put(buffer_, y);
  hasher_.update(buffer_.data(), buffer_.size());
  ++nonzero_;
}

void LeafDigest::add_bytes(const ByteBuffer& bytes, uint64_t nonzero) {
  hasher_.update(bytes.data(), bytes.size());
  nonzero_ += nonzero;
}

uint64_t LeafDigest::nonzero() const { return nonzero_; }

Digest LeafDigest::finish() { return hasher_.finish(); }
}  // namespace sic::sicx
