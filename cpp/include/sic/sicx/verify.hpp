// SPDX-License-Identifier: AGPL-3.0-or-later
// SICX 1.0 full verification: every chunk hash, the Merkle root, canonical
// encodings (re-encoding each block must reproduce its bytes), element
// invariants (scale, level set, bit bound), BALL canonicity and consistency,
// and per-element logical leaf digests used by the producer round trip.
#pragma once

#include <atomic>
#include <set>
#include <thread>

#include "sic/sicx/branches.hpp"
#include "sic/sicx/io.hpp"
#include "sic/sicx/rescaling_rule.hpp"

namespace sic::sicx {

// Logical leaf stream of an element: for each nonzero full leaf lambda_e (in
// increasing full position e), varint(e) || int(x) || int(y) in the original
// (x, y) basis of O_K. Its SHA-256 identifies the element independently of the
// container encoding; the packer computes it from the internal export.

struct ElementCheck {
  Digest digest{};
  uint64_t nonzero = 0, max_bits = 0, payload_bytes = 0;
};

struct BlockResult {
  ByteBuffer stream;  // logical leaf stream bytes of this block
  uint64_t nonzero = 0, max_bits = 0, payload_bytes = 0;
  uint64_t level_mask = 0;  // sub-levels with a nonzero digit
  std::string gcd = "0";    // decimal gcd of stored integers
};

// Decode one element block: canonical re-encoding, bounds, logical stream.
BlockResult check_block(const Reader& reader, size_t chunk_index, bool want_stream);

// Element invariants over all blocks, with the logical digest. Blocks are
// decoded in parallel windows and hashed in block order.
ElementCheck check_element(const Reader& reader, size_t id, unsigned threads);

struct BallCheck {
  int64_t accuracy_bits;
  std::string norm;
};

// BALL: canonical records, text round trip, anchor gauge, normalization.
BallCheck check_balls(const Reader& reader, size_t ray);

void check_branches(const Meta& meta);

void check_rescaling(const Reader& reader, unsigned threads, ValuationPolicy policy = {});

// Conservative admission estimate for serial exact checks. Includes physical
// verification/cache growth, scaled leaf expansion and both dense collectors.
uint64_t bounds_memory(const Reader& reader, bool complete = false, ValuationPolicy policy = {});

struct VerifyReport {
  std::vector<ElementCheck> elements;
  std::vector<BallCheck> balls;
  uint64_t chunks = 0, bytes = 0;
};

// Global checks needed by partial consumers once every exact element is read.
// Does not fetch PROV or unrequested finalization payloads.
struct VerificationPolicy {
  PrecisionPolicy precision;
  ValuationPolicy valuation;
};
VerifyReport verify_exact(const Reader& reader, unsigned threads, VerificationPolicy policy = {});

VerifyReport verify_all(const Reader& reader, unsigned threads, VerificationPolicy policy = {});

}  // namespace sic::sicx
