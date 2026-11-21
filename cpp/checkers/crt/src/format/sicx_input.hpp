// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
#include "canonical_json.hpp"
namespace sic_check::crt::sicx {

struct Input {
  File f;
  string path;
  Hash file_sha{}, merkle{}, exact_root{};
  Uint64 d = 0, D = 1, branch_bits = 0, limbs = 0, working_bits = 0;
  Int64 D0 = 0;
  vector<Uint64> deg, smith, shifts, positions;
  struct UnitGenerator {
    Uint64 unit, order;
  };
  vector<UnitGenerator> unit_generators;
  vector<vector<Uint64>> unit_words;
  mutable vector<Uint64> unit_indices;
  vector<std::array<string, 2>> branches;
  vector<Elem> el;
  std::map<Int64, size_t> rep;
  vector<Ray> rays;
  vector<Chunk> ch;
  vector<Logical> logical;
  Limits limits;
  mutable Scope scope;
  bool layout_ready = false;
  mutable bool checking_exact = false;

  int t() const { return (int)deg.size(); }

  // The authenticated shifts describe an input presentation, not an admitted integer expansion.
  // The CRT acceleration-hint ceiling admits the proved retry path from zero otherwise.

  vector<Uint64> integral_shift_hints() const;

  size_t find(const string& kind, Int64 level, Int64 axis = -1, Int64 k = -1) const;

  Hash whole_file_sha() const;

  void header_at(Uint64 off, const string& type, uint32_t flags, Uint64 len) const;

  vector<Uint8> payload(size_t ci) const;

  static bool is_prime(Uint64 p);

  static Uint64 squarefree_part(Uint64 n);

  static Uint64 powmod(Uint64 a, Uint64 e, Uint64 n);

  // All arguments come from the bounded dimension; intermediate products are below 2^40.

  static vector<UnitGenerator> canonical_units(Uint64 dimension);

  Uint64 entry_position(Uint64 m) const;

  vector<Uint64> entry_word(Uint64 m) const;

  void open(const string& p, const Trust& trust = {}, bool full = true);

  void parse_meta(const vector<Uint8>& b, Uint64 hminor);

  void parse_rays(const vector<Uint8>& b);

  void verify_chunk(size_t ci);

  void parse_optional(size_t ci);

  // Transactional at chunk granularity: no parsed value escapes until every constituent leaf
  // checks.

  vector<LeafData> read_chunk(size_t ci) const;

  vector<Pair> element(size_t id) const;

  void check_element(size_t id, const vector<Pair>& pairs) const;

  void coordinates(size_t id, vector<fmpz>& X, vector<fmpz>& Y) const;

  vector<Uint64> weights(const Elem& e, Uint64 sub) const;

  struct Constraint {
    size_t id;
    Uint64 sub, v;
  };

  void check_rescaling(const vector<Constraint>& constraints) const;

  void check_ball(size_t ci, size_t ray);

  void algebraic_limit(const AlgebraicNumber& a) const;

  static Uint64 ceil_log2(Uint64 n);

  static Uint64 factor_allowance(Uint64 degree);

  AlgebraicNumber sqrt_checked(const AlgebraicNumber& x) const;

  // Upper bounds on generic resultant growth are checked before algebraic expansion.

  AlgebraicNumber op(const AlgebraicNumber& a, const AlgebraicNumber& b, char operation) const;

  AlgebraicNumber scaled(const AlgebraicNumber& a, Int64 exponent) const;

  AlgebraicNumber evaluate(size_t id, const vector<vector<AlgebraicNumber>>& dual,
                           const AlgebraicNumber& omega) const;

  static string zhex(const fmpz_t z);

  void real_enclosure(arb_t enclosure, const AlgebraicNumber& x, Uint64 work) const;

  int real_sign(const AlgebraicNumber& x) const;

  string component_box(const AlgebraicNumber& x, Uint64 p) const;

  void parts(AlgebraicNumber& re, AlgebraicNumber& im, const AlgebraicNumber& z) const;

  std::array<string, 2> root_box(const AlgebraicNumber& z, Uint64 p) const;

  bool component_contains(const string& s, const AlgebraicNumber& x) const;

  bool box_contains(const std::array<string, 2>& b, const AlgebraicNumber& z) const;

  static bool boxes_overlap(const std::array<string, 2>& a, const std::array<string, 2>& b);

  struct BranchLevel {
    vector<AlgebraicNumber> roots;
    size_t chosen = 0;
    vector<AlgebraicNumber> basis;
  };

  BranchLevel algebraic_branch_level(int i, const vector<vector<AlgebraicNumber>>& dual,
                                     const AlgebraicNumber& omega) const;

  // Axis-quadratic proof: each trace term is exactly real or imaginary, since the
  // selected roots/duals have proved zero components. An optional first
  // odd level is isolated exactly in Qbar, including its full root family and trace
  // dual. If its selected root and dual are exactly real, bounded real interval
  // Newton enclosures seed the quadratic suffix. Inductively, trace contraction
  // encloses the exact radicand, its certified sign proves two distinct nonzero
  // axis roots, and 1/(2r) encloses the exact next dual. Thus no compositum/minimal
  // polynomial of a suffix root is needed. Structural zero components prove the
  // singleton equalities; strict rounding bounds and the radicand-minus-dyadic-
  // square test prove the other canonical components. Prefix and suffix families
  // participate in the same minimum-precision test. No numerical zero test
  // supplies an equality. Non-axis or unresolved dyadic cases use Qbar below.

  bool quadratic_branches() const;

  void check_branches() const;

  void check_branches_algebraic() const;

  // Partial reads have the same global obligations once every exact dependency is present.
  // The guard permits branch evaluation to reread checked elements without recursion.

  void complete_exact_if_ready() const;

  void verify_full();
};
}  // namespace sic_check::crt::sicx
