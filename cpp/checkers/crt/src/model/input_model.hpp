// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/acb.h>
#include <flint/acb_poly.h>
#include <flint/arb.h>
#include <flint/flint.h>
#include <flint/fmpq.h>
#include <flint/fmpz.h>
#include <flint/qqbar.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace sic_check::crt::sicx {

using std::string;

using std::vector;

using Uint64 = uint64_t;

using Int64 = int64_t;

using Uint8 = uint8_t;

static_assert(sizeof(ulong) == 8, "SICX input requires 64-bit FLINT limbs");

struct Magnitude {
  operator mag_struct*();
  operator const mag_struct*() const;
  mag_struct* operator->();
  const mag_struct* operator->() const;
  mag_t v;
  Magnitude();
  ~Magnitude();
  Magnitude(const Magnitude&) = delete;
  Magnitude& operator=(const Magnitude&) = delete;
};

struct Reject : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Refuse : std::runtime_error {
  using std::runtime_error::runtime_error;
};

using Hash = std::array<Uint8, 32>;

// ------------------------------------------------------------------------------------------------
// file
struct File {
  FILE* f = nullptr;
  Uint64 size = 0;
  string path;
  struct stat snapshot {};
  File() = default;
  File(const File&) = delete;
  File& operator=(const File&) = delete;

  void open(const string& p);

  void assert_size() const;

  void read(Uint64 off, void* b, size_t n) const;

  vector<Uint8> read(Uint64 off, size_t n) const;

  ~File();
};

// All limits are admission limits, independent of mathematical validity. Raising them is explicit.
struct Limits {
  Uint64 json_bytes = 64ull << 20, chunk_bytes = 256ull << 20;
  Uint64 directory_entries = 1ull << 24, directory_bytes = 512ull << 20;
  Uint64 coordinate_pairs = 1ull << 22, dense_coordinates = 1ull << 24;
  Uint64 rescaling_constraints = 1ull << 22, branch_bits = 1ull << 20;
  slong algebraic_degree = 4096, algebraic_bits = 1 << 20;
};

struct Z {
  operator fmpz*();
  operator const fmpz*() const;
  fmpz* operator->();
  const fmpz* operator->() const;
  fmpz_t v;

  Z();

  explicit Z(Int64 n);

  Z(const Z& z);

  Z(Z&& z) noexcept;

  Z& operator=(const Z& z);

  Z& operator=(Z&& z) noexcept;

  ~Z();
};

struct A {
  operator arb_struct*();
  operator const arb_struct*() const;
  arb_struct* operator->();
  const arb_struct* operator->() const;
  arb_t v;

  A();

  A(const A& x);

  A& operator=(const A& x);

  ~A();
};

struct AlgebraicNumber {
  qqbar_t v;

  AlgebraicNumber();

  explicit AlgebraicNumber(Int64 n);

  AlgebraicNumber(const AlgebraicNumber& q);

  AlgebraicNumber& operator=(const AlgebraicNumber& q);

  ~AlgebraicNumber();
};

struct B {
  operator acb_struct*();
  operator const acb_struct*() const;
  acb_struct* operator->();
  const acb_struct* operator->() const;
  acb_t v;

  B();

  B(const B& b);

  B& operator=(const B& b);

  ~B();
};

struct Elem {
  string kind;
  Int64 level = -1, axis = -1, k = -1, position = -1;
  vector<int> levels;
  string scale;
  Uint64 max_bits = 0, ncoord = 1, W = 1, nblocks = 1;
  int field = 0;
  vector<size_t> chunks;
};

struct Ray {
  string label;
  vector<Uint64> word;
  Uint64 anchor = 0;
  Int64 accuracy = 0;
};

struct Logical {
  string kind;
  Uint64 element = 0, first = 0, count = 0;
  Hash hash{};
};

struct Chunk {
  string type;
  uint32_t flags = 0, codec = 0;
  Uint64 off = 0, len = 0, decoded = 0, first_leaf = 0, leaf_count = 0, object = 0, first = 0,
         count = 0;
  Hash sha{};
};

struct Pair {
  Uint64 sub = 0;
  Z x, y;
};

struct LeafData {
  size_t index = 0;
  vector<Pair> pairs;
};

struct Trust {
  string exact, file, whole;
};

struct Scope {
  bool full = false, exact_trusted = false, file_trusted = false, whole_checked = false;
  bool exact_checked = false;
  string exact_route, file_route;
  std::set<size_t> chunks, elements;

  string describe() const;
};
}  // namespace sic_check::crt::sicx
