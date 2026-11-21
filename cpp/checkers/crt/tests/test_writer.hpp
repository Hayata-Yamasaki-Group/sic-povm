// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/fmpz.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace fixture {

using ByteBuffer = std::vector<uint8_t>;

using Hash = std::array<uint8_t, 32>;

using U = uint64_t;

void append(ByteBuffer& a, const ByteBuffer& b);

ByteBuffer text(const std::string& s);

void word(ByteBuffer& b, U n, int width = 8);

void set(ByteBuffer& b, size_t p, U n, int width = 8);

U get(const ByteBuffer& b, size_t p);

Hash digest(const ByteBuffer& b);

Hash tree(const std::vector<ByteBuffer>& entries, size_t begin, size_t end);

void var(ByteBuffer& b, U n);

void integer(ByteBuffer& b, int64_t n);

std::string str(const std::string& s);

std::string arr(const std::vector<std::string>& a);

std::string obj(const std::map<std::string, std::string>& a);

std::string nums(const std::vector<U>& a);

struct Big {
  fmpz_t z;

  Big();

  ~Big();

  Big(const Big&) = delete;
};

std::string hex(const fmpz_t n);

std::string sqrt_box(U number, U p = 128);

std::string cbrt2_box(U p = 128);

std::string sqrt2_box(U p = 128);

struct L {
  std::string kind;
  U element = 0, first = 0, count = 1;
  ByteBuffer bytes;
  uint32_t reserved = 0;
};

struct P {
  std::string type;
  uint32_t flags = 1, codec = 0, reserved = 0;
  U offset = 0, decoded = 0, first_leaf = 0, leaf_count = 0, object = 0, first = 0, count = 0;
  ByteBuffer bytes;
};

ByteBuffer leaf(U id, U first, U count, const std::vector<std::array<int64_t, 3>>& pairs,
                int D0 = 5);

std::string descriptor(const std::string& kind, U index, const std::vector<U>& levels,
                       const std::string& scale, U bits, int axis = -1, int k = -1);

// Test writer derives only the number of local cyclic factors, without the reader's generator code.
std::string zero_unit_words(U d, const std::vector<U>& word);

struct File {
  U d = 4;
  std::vector<L> leaves;
  std::vector<P> physical;
  std::string meta;

  explicit File(U dimension = 4, bool finalized = false, bool grouped = false);

  void rebuild_physical(bool join);

  void finalize();

  unsigned get_rank() const;

  ByteBuffer encode() const;
};

File odd(bool actions = false);

void replace(std::string& s, const std::string& a, const std::string& b);

// Integer-only nearest rounding for 2^(1/n), independent of Qbar/Arb.
std::string root2_box(U n, U p = 128);

// g0^3 = 2, gj^2 = g(j-1). The last radicand may instead be -g(j-1)
// or 1. Trace coordinates are computed symbolically: only sub=N-1 is
// nonzero, with lambda=2N, since the product collapses to g0^3=2.
File odd_quadratic(bool imaginary = false, bool singleton = false, bool negative = false);

// The selected cubic root is 1, but its dual contains 1/3-free dyadics:
// f=(X-1)((X-1)^2+2^258), b=[1+2^-258,-2^-257,2^-258].
// lambda=[1,0,2^130] gives c=(1+2^-129)^2, an exact rounding tie.
// At work=256 the radicand is nonexact, exercising the open-cell proof.
File odd_quadratic_tie(bool negative = false, bool wrong = false);

// Dense conjugate combinations exercise the pre-expansion resultant-height budget.
File odd_admission();

// The explicit §9b presentation, including the negative-discriminant pair transform and branch i.
File d2_spec();

std::string decimal(const fmpz_t n);

// Exact rational-root constructions test half-integer rounding and the minimum global branch
// precision. The action at level zero is a synthetic rescaling obstruction, not a claimed
// automorphism.
File dyadic(bool tiny = false, unsigned tie = 1, bool negative = false);

// Synthetic container presentation with a faithful word map into the direct-product factors.
// The action records are rescaling obstructions only; no algebraic/SIC claim is made.
File faithful_units(U dimension, std::vector<U> orders);

// A square in the derived integral basis. Its selected positive root is
// sqrt(D0)-1 (non-half basis) or (sqrt(D0)+1)/2 (half basis).
// Integer comparisons alone produce the canonical rectangle, independently of the reader.
std::string base_square_box(U D0, bool half, U p = 128);

void replace(std::string& s, const std::string& a, const std::string& b);

// Re-sign directory mutations to test semantics independently of root mismatch checks.
void reseal(ByteBuffer& out);
}  // namespace fixture
