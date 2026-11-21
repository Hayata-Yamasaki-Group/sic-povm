// SPDX-License-Identifier: AGPL-3.0-or-later
// SICX 1.0: public exact Weyl-Heisenberg SIC fiducial container (docs/sicx.md).
//
// This header implements the primitives of the specification: little-endian
// integers, LEB128 varints, the canonical signed integer code, canonical JSON,
// chunks, the PDIR table, the RFC 6962 Merkle root, the footer, the
// stabilizer-adapted element block code, fixed-width BALL records, a
// deterministic writer and a random-access reader that checks every chunk hash
// on read. Every reader check is fail-closed: a violation throws sic::Error.
#pragma once

#include <fcntl.h>
#include <flint/arb.h>
#include <flint/fmpz.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sic/limits.hpp"
#include "sic/support/json.hpp"

namespace sic::sicx {
inline constexpr const char* kDecoderVersion = "sic-decode/1.0";
inline constexpr const char* kRescalingRule = "sicx-2adic-greedy/1.0";
inline constexpr const char* kEntryRule = "sicx-divisors/1.0";

[[noreturn]] void fail(ErrorKind kind, const std::string& message);

using ByteBuffer = std::vector<uint8_t>;
using Digest = std::array<uint8_t, 32>;

inline constexpr uint16_t kMajor = 1, kMinor = 0;
inline constexpr std::array<uint8_t, 8> kMagic = {'S', 'I', 'C', 'X', 0x0d, 0x0a, 0x1a, 0x0a};
inline constexpr std::array<uint8_t, 8> kEndMagic = {'S', 'I', 'C', 'X', 'E', 'N', 'D', 0x1a};
inline constexpr size_t kHeaderBytes = 16, kChunkHeaderBytes = 16, kFooterBytes = 112,
                        kIndexEntryBytes = 112, kLogicalEntryBytes = 64;
inline constexpr uint32_t kRequired = 1;
// Reader bounds (specification section "Reader limits").
inline constexpr uint64_t kMaxIntegerBytes = 1u << 20;    // 8 Mbit per integer
inline constexpr uint64_t kMaxTextChunk = 64ull << 20;    // EXMT/PREC/PROV
inline constexpr uint64_t kMaxBlockBytes = 256ull << 20;  // one TOWR/BODY block
inline constexpr uint64_t kMaxChunks = 1ull << 24;
inline constexpr int64_t kMaxBallExponent = int64_t(1) << 40;

// Normative SICX 1.0 limits, independent of resource defaults.
inline constexpr uint64_t kMaximumDimension = uint64_t{1} << 20;
inline constexpr uint64_t kMaximumFieldDegree = uint64_t{1} << 40;
inline constexpr unsigned kMaximumTowerDepth = 62, kMaximumPrimeDegree = 31;
inline constexpr unsigned kMaximumBallLimbs = 1024;
inline constexpr uint64_t kMaximumWorkingBits = uint64_t{1} << 20;
inline constexpr uint64_t kMaximumBranchBits = uint64_t{1} << 20;
inline constexpr uint64_t kMaximumBranchTextBytes = uint64_t{1} << 20;
struct ResourceLimits {
  unsigned json_depth = support::kDefaultJsonDepth;
  uint64_t text_chunk_bytes = kMaxTextChunk;
  uint64_t block_bytes = kMaxBlockBytes;
  uint64_t directory_entries = kMaxChunks;
  uint64_t reader_memory_bytes = uint64_t{4} << 30;
  uint64_t chunk_verification_bytes = uint64_t{2} << 30;
  uint64_t inflight_verification_bytes = uint64_t{8} << 30;
  uint64_t cache_bytes = uint64_t{256} << 20;
};
struct ValuationPolicy {
  uint64_t table_bytes = uint64_t{4} << 30;
  uint64_t total_bytes = uint64_t{8} << 30;
  uint64_t admit(uint64_t count, uint64_t used) const;
};
void admit_chunk(std::string_view type, uint64_t bytes, const ResourceLimits& limits = {});
// ------------------------------------------------------------------ hashing
Digest sha256(const uint8_t* data, size_t size);
std::string hex(std::span<const uint8_t> bytes);

// ------------------------------------------------------------ little endian

// --------------------------------------------------------------- arithmetic
// Squarefree part of n (sign kept); trial division, n small (about d^2).

// D0 of K = Q(sqrt(D0)), derived from d (specification section "Fixed conventions").
int64_t field_radicand(uint64_t d);

// Derived unit group: direct-product enumeration gives O(d) storage/build work
// after bounded factorization and primitive-root search. One indexed lookup yields
// the mixed-radix logarithm; extracting its h digits takes O(h).
struct UnitGroup {
  uint64_t d;
  std::vector<uint64_t> generators, orders;
  std::vector<uint32_t> logarithm;
  static constexpr uint32_t kNonunit = UINT32_MAX;

  static std::vector<uint64_t> prime_divisors(uint64_t n);
  static uint64_t power(uint64_t a, uint64_t e, uint64_t n);
  explicit UnitGroup(uint64_t dimension);
};

// Mixed-radix sub-layout of an element: the levels J (increasing) of the tower
// whose digits may be nonzero. Coordinates are indexed by the sub-index s in
// [0, prod_{j in J} p_j), little-endian over J; full position pos(s) inserts
// zero digits at the other levels.
struct SubLayout {
  std::vector<unsigned> levels;        // J
  std::vector<uint64_t> sub_width{1};  // W_0..W_|J|
  std::vector<uint64_t> full_width;    // width(J[j]) in the full tower
  SubLayout() = default;
  SubLayout(const std::vector<unsigned>& degrees, const std::vector<uint64_t>& tower_width,
            std::vector<unsigned> J);
  uint64_t coordinates() const { return sub_width.back(); }
  uint64_t position(uint64_t s) const;
};

inline constexpr unsigned kLogicalLeafLog2Limit = 14;
inline constexpr uint64_t kPhysicalChunkTargetBytes = 16ull << 20;

// ------------------------------------------------------------------- EXMT
struct ElementInfo {
  std::string kind;    // wsq, fcoef, actdelta, act, actodd, rep
  unsigned level = 0;  // the element lies in F_level (rep: level = t)
  int64_t axis = -1, k = -1, position = -1;
  std::vector<unsigned> levels;  // J
  std::string scale = "1";       // decimal positive integer
  uint64_t max_bits = 0;         // max bit length of the full leaves (x and y)
  SubLayout layout;
  uint64_t block = 0;        // coordinates per block
  unsigned block_level = 0;  // full-tower level of a block subtree
  uint64_t blocks() const { return layout.coordinates() / block; }
};
struct Ray {
  std::string label;
  std::vector<uint64_t> word;
  uint64_t anchor = 0;
  int64_t accuracy_bits = 0;
};

struct Meta {
  uint64_t d = 0;
  int64_t D0 = 0;
  std::vector<unsigned> degrees;
  std::vector<uint64_t> width{1};
  uint64_t D = 1;
  std::vector<uint64_t> smith;
  std::vector<std::vector<uint64_t>> unit_generator_words;
  std::shared_ptr<const UnitGroup> units;  // derived once per file; shared by Meta copies
  std::vector<std::array<std::string, 2>> branches;
  std::vector<ElementInfo> elements;
  std::vector<Ray> rays;
  uint64_t block_coordinates = uint64_t(1) << kLogicalLeafLog2Limit;  // private, derived leaf limit
  uint64_t branch_bits = 128;
  std::vector<uint64_t> rescaling_shifts;
  unsigned ball_limbs = 0;
  // RAYS "decoder" (optional): version name and source commit of the decoder that certified the
  // balls.
  std::string rays_decoder, rays_decoder_source_commit;
  uint64_t ball_working_bits = 0;
  sic::support::Json catalogue = sic::support::Json::object_value();
  // Optional field labels are not stored in EXMT.
  sic::support::Json field_labels = sic::support::Json::object_value();
  std::vector<std::string> required_features;
  uint64_t minor = kMinor;

  // Derived.
  std::map<int64_t, size_t> representative;  // position -> element id
  unsigned levels() const { return static_cast<unsigned>(degrees.size()); }
  // The canonical lift is the first positive unit congruent to m/g modulo d/g.
  // Unit logarithms are indexed, never searched per coordinate.
  struct Entry {
    int64_t representative;
    std::vector<uint64_t> word;
  };
  Entry entry(uint64_t m) const;
};

std::vector<uint64_t> words_add(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b,
                                const std::vector<uint64_t>& smith);

// Block rule: the largest sub-layout width W_k <= block_coordinates.
void derive_blocks(ElementInfo& e, uint64_t block_coordinates);

bool decimal_positive(const std::string& text);

// Canonical element order and the required element set (specification "Elements").
struct ElementKey {
  int kind_rank;
  unsigned level;
  int64_t axis, k, position;
  auto operator<=>(const ElementKey&) const = default;
};
int kind_rank(const std::string& kind);
ElementKey element_key(const ElementInfo& e);
// Tower and action elements sort by level first.
bool element_less(const ElementInfo& a, const ElementInfo& b);

sic::support::Json meta_to_json(const Meta& m);

// RAYS (finalization stage): ball record parameters and the ray records.
sic::support::Json rays_to_json(const Meta& m);

// Parse and validate EXMT (reader rejection rules of the specification).
void validate_branch_string(const std::string& text);

Meta meta_from_json(const sic::support::Json& j);

// Parse RAYS into an already parsed Meta (finalized files only).
void rays_from_json(const sic::support::Json& j, Meta& m);

// ----------------------------------------------------------- element blocks
// Block payload: varint element, varint first, varint count, then runs
// varint(2n + kind) with kind 0 = n zero pairs, kind 1 = n nonzero pairs (r, y).
// Runs alternate in kind; data pairs are never (0, 0).

struct BlockHeader {
  uint64_t element, first, count;
};

struct BallPart {
  bool negative = false;
  int64_t mid_exp = 0, rad_exp = 0;
  uint64_t rad_man = 0;
  std::vector<uint64_t> mantissa;  // |M|, little-endian limbs
};

// Parse "M E m e" (canonical lowercase hex); requires the four fields to fit the record.

// Load a part into an arb (exact: the ball is M 2^E +- m 2^e).
void ball_part_arb(arb_t out, const BallPart& part);
// Upper bound b with radius <= 2^b.

}  // namespace sic::sicx
