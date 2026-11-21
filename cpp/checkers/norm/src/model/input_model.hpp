// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/acb.h>
#include <flint/acb_poly.h>
#include <flint/fmpq.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
namespace sic_check::norm {

struct ComplexPolynomial {
  operator acb_poly_struct*();
  operator const acb_poly_struct*() const;
  acb_poly_struct* operator->();
  const acb_poly_struct* operator->() const;
  acb_poly_t v;
  ComplexPolynomial();
  ~ComplexPolynomial();
  ComplexPolynomial(const ComplexPolynomial&) = delete;
  ComplexPolynomial& operator=(const ComplexPolynomial&) = delete;
};

struct RealBall {
  operator arb_struct*();
  operator const arb_struct*() const;
  arb_struct* operator->();
  const arb_struct* operator->() const;
  arb_t v;
  RealBall();
  ~RealBall();
  RealBall(const RealBall&) = delete;
  RealBall& operator=(const RealBall&) = delete;
};

struct InvalidInput : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct ResourceRefusal : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Integer {
  operator fmpz*();
  operator const fmpz*() const;
  fmpz* operator->();
  const fmpz* operator->() const;
  fmpz_t v;

  Integer();

  Integer(const Integer& x);

  Integer(Integer&& x) noexcept;

  Integer& operator=(const Integer& x);

  ~Integer();
};

struct ComplexBall {
  operator acb_struct*();
  operator const acb_struct*() const;
  acb_struct* operator->();
  const acb_struct* operator->() const;
  acb_t v;

  ComplexBall();

  ComplexBall(const ComplexBall& z);

  ComplexBall(ComplexBall&& z) noexcept;

  ComplexBall& operator=(const ComplexBall& z);

  ~ComplexBall();
};

using ByteBuffer = std::vector<uint8_t>;

using Hash = std::array<uint8_t, 32>;

struct BallPart {
  Integer midpoint;
  int64_t E = 0, e = 0;
  uint64_t radius = 0;
};

struct Element {
  std::string kind, scale;
  unsigned level = 0;
  int64_t axis = -1, k = -1, position = -1;
  uint64_t max_bits = 0;
  std::vector<unsigned> levels;
};

struct Ray {
  std::string label;
  std::vector<uint64_t> word;
  uint64_t anchor = 0;
  int64_t accuracy = 0;
};

// Each factor stores its local inverse table. At a 2-power the sign and 5
// tables are filled together, so their tuple is the direct-product tuple.
struct UnitFactor {
  uint64_t generator, order, modulus;
  std::vector<uint32_t> logarithm;
};

struct Meta {
  uint64_t d = 0, D = 1, branch_bits = 0, limbs = 0, working_bits = 0;
  int64_t D0 = 0;
  std::vector<unsigned> degrees;
  std::vector<uint64_t> shifts;
  std::vector<uint64_t> smith;
  std::vector<std::vector<uint64_t>> unit_words;
  std::vector<UnitFactor> units;
  std::vector<ComplexBall> branches;
  std::vector<Element> elements;
  std::map<int64_t, size_t> representative;
  std::vector<Ray> rays;
  struct Entry {
    int64_t representative;
    std::vector<uint64_t> word;
  };

  Entry entry(uint64_t m) const;
};

struct Chunk {
  std::string type;
  uint64_t flags = 0, offset = 0, length = 0, object = 0, first = 0, count = 0;
  uint64_t decoded_length = 0, first_leaf = 0, leaf_count = 0;
  Hash hash{};
  ByteBuffer payload;
};

struct Pair {
  Integer x, y;
};

struct Logical {
  std::string kind;
  uint64_t element = 0, first = 0, count = 0;
  Hash hash{};
};

struct AlgebraicLimits {
  uint64_t degree = 256, height_bits = 1u << 20;
};

inline constexpr uint64_t kDefaultMaxDimension = 4096;
inline constexpr uint64_t kDefaultMaxFieldDimension = 65536;

struct ReadOptions {
  bool partial = false;
  std::optional<Hash> exact_root, file_root, whole_sha256;
  std::vector<size_t> elements, physical_chunks;
  uint64_t max_dimension = kDefaultMaxDimension, max_field_dimension = kDefaultMaxFieldDimension;
  uint64_t max_dense_coordinates = 1u << 22;
  uint64_t max_directory_bytes = 64u << 20;
  AlgebraicLimits algebraic_limits;
};

enum class TrustRoute { recomputed, direct, file_root, whole_sha256 };

struct Verification {
  Hash exact_root{}, file_root{};
  bool full = false, trusted_exact = false, trusted_file = false, whole_hash_checked = false;
  std::vector<size_t> checked_chunks, checked_elements;
  bool rescaling_checked = false, branches_checked = false;
  TrustRoute exact_trust = TrustRoute::recomputed, file_trust = TrustRoute::recomputed;
};

void verify_canonical_branches(const Meta&, const std::vector<std::vector<Pair>>&,
                               AlgebraicLimits = {});
}  // namespace sic_check::norm
