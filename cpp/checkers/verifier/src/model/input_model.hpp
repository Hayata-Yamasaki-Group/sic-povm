// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/acb.h>
#include <flint/acb_poly.h>
#include <flint/arb.h>
#include <flint/flint.h>
#include <flint/fmpq.h>
#include <flint/fmpz.h>
#include <flint/mag.h>
#include <flint/qqbar.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdarg>
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
#include <unordered_map>
#include <vector>
namespace sic_check::verifier {

using std::string;

using std::vector;

typedef uint64_t Uint64;

typedef int64_t Int64;

typedef uint8_t Uint8;

// ------------------------------------------------------------------------------------------------
// errors
struct Reject : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct PrecErr : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct UsageErr : std::runtime_error {
  using std::runtime_error::runtime_error;
};

typedef std::array<Uint8, 32> Hash;

struct Real {
  operator arb_struct*();
  operator const arb_struct*() const;
  arb_struct* operator->();
  const arb_struct* operator->() const;
  arb_t v;

  Real();

  ~Real();
};

struct Integer {
  fmpz_t v;

  Integer();

  ~Integer();

  Integer(const Integer&) = delete;

  operator fmpz*();

  operator const fmpz*() const;
};

// ------------------------------------------------------------------------------------------------
// canonical JSON
struct Jv {
  char t = 's';  // 's' string, 'a' array, 'o' object
  string s;
  vector<Jv> a;
  vector<std::pair<string, Jv>> o;

  const Jv* get(const string& k) const;
};

// ------------------------------------------------------------------------------------------------
// ACB wrapper
struct ACB {
  operator acb_struct*();
  operator const acb_struct*() const;
  acb_struct* operator->();
  const acb_struct* operator->() const;
  acb_t v;

  ACB();

  ACB(const ACB& o);

  ACB(ACB&& o) noexcept;

  ACB& operator=(const ACB& o);

  ACB& operator=(ACB&& o) noexcept;

  ~ACB();
};

struct ExactRoot {
  qqbar_t v;

  ExactRoot();

  ExactRoot(const ExactRoot& x);

  ExactRoot& operator=(const ExactRoot& x);

  ~ExactRoot();
};

// ------------------------------------------------------------------------------------------------
// file
struct File {
  FILE* f = nullptr;
  Uint64 size = 0;
  string path;
  struct stat snapshot {};

  void stable();

  void open(const string& p);

  void read(Uint64 off, void* b, size_t n);

  vector<Uint8> read(Uint64 off, size_t n);

  ~File();
};

// ------------------------------------------------------------------------------------------------
// EXMT model

struct Elem {
  string kind;
  Int64 level = -1, axis = -1, k = -1, position = -1;
  vector<int> levels;
  string scale_s;
  fmpz_t scale;
  Uint64 max_bits = 0;
  int nfield = 0;  // number of tower levels of its field
  Uint64 ncoord = 0, W = 1;
  Uint64 nblocks = 1;
  vector<size_t> chunks;  // chunk table indices of its blocks

  Elem();

  Elem(const Elem&) = delete;

  ~Elem();
};

struct RayRec {
  string label;
  vector<Uint64> word;
  Uint64 anchor;
  Int64 accuracy_bits;
};

// SPDX-License-Identifier: AGPL-3.0-or-later
// Exact unit layout derived exclusively from SICX section 3.5. No stored unit data.
struct UnitGenerator {
  Uint64 modulus, local, order, lift;
  vector<Uint64> logarithm;
};

struct Meta {
  Uint64 d = 0, minor = 0;
  vector<Uint64> deg;
  vector<std::pair<string, string>> branch_re;
  vector<Uint64> smith, representatives;
  vector<UnitGenerator> units;
  vector<vector<Uint64>> unit_words;
  Uint64 branch_bits = 0;
  vector<Uint64> shifts;
  fmpz_t D0;
  Uint64 D = 1;
  vector<std::unique_ptr<Elem>> el;
  vector<RayRec> rays;
  Uint64 limbs = 0, working_bits = 0;
  bool has_rays = false;
  bool has_decoder = false;
  string decoder_name, decoder_source_commit;  // Informative source identity, spec 5.1a

  Meta();

  ~Meta();

  int t() const;

  // element lookup
  std::map<string, int> idx;
};

// ------------------------------------------------------------------------------------------------
// containers
struct Chunk {
  char type[5]{};
  uint32_t flags = 0;
  Uint64 off = 0, len = 0, object = 0, first = 0, count = 0;
  Uint64 first_leaf = 0, leaf_count = 0;
  Hash sha{};
};

struct Logical {
  string kind;
  Uint64 element, first, count;
  Hash sha;
};

struct BallRay {
  vector<ACB> w;
  Int64 worst_acc = 0;
};

struct Opts {
  string path, text_path;
  string decode = "auto";
  int max_steps = 80;
  int extra_bits = 400;
  bool balls = true;
  bool quiet = false;
  string only_ray;
  vector<Uint64> partial_elements;
  string expected_exact, expected_file, expected_sha;
  Uint64 branch_degree_limit = 256;
  bool release = false;
};

struct Container {
  File f;
  vector<Chunk> ch, leaf_chunks;
  vector<Logical> logical;
  Hash root{}, exact_root{}, filesha{};
  Uint64 ldir_off = 0, ldir_len = 0, pdir_off = 0, pdir_len = 0;
  Meta M;
  int meta_idx = -1, rays_idx = -1;
  vector<BallRay> balls;
  vector<ExactRoot> base_roots;
  bool all_exact = false;
  vector<size_t> ball_chunks;
  Uint64 fsize = 0;
  size_t footer_size = 0;
  bool has_prov = false;
  Jv prov;  // parsed PROV (canonical JSON), if present
  // spec 3.7 rule state: for every distinct weight vector a the minimum v = min(v_2(x), v_2(y))
  // over the nonzero leaves with that weight vector, and gain_s = sum of a_s over all leaves. A
  // weight vector is packed into one u64: the full leaf index e < D <= 2^40 (its mixed-radix digits
  // are the digits of a; levels at or above the element's field level have digit 0), bits 40..45 =
  // own level + 1 (0: rep, no own weight), bits 46..54 = own weight
  // (<= C + 1 = 466 for p <= 31). Memory: about 40 B per distinct weight vector (hash node +
  // bucket).
  std::unordered_map<Uint64, Uint64> rs_minv;
  vector<Uint64> rs_gain, rs_N;
  Uint64 rs_leaves = 0;
  string rescaling_line;  // summary for the RESULT line
  Uint64 dec_full = 0, dec_ratio = 0, dec_total = 0;
};
}  // namespace sic_check::verifier
