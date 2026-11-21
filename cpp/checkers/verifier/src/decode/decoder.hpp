// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::verifier {

// ------------------------------------------------------------------------------------------------
// decoding
struct RootSet {
  vector<ACB> r;
  vector<vector<ACB>> a, b;
};

RootSet numeric_base(Container&, slong, vector<vector<ACB>>*);

struct Decoder {
  Container& C;
  Meta& M;
  slong prec = 512;
  arb_t omega;
  acb_t omegac;
  bool cplx = false;
  int t;
  vector<ACB> branch;
  std::map<string, int> eid;

  explicit Decoder(Container& c);

  ~Decoder();

  // Section 2: derive the rational or quadratic integral basis and its fixed embedding.

  void set_prec(slong p);

  int id(const char* kind, Int64 a, Int64 b = -1, Int64 c = -1);

  void stage(vector<ACB>& cur, const vector<ACB>& bd);

  // iota(x) for the element eid, using the root set (levels < nfield)

  void eval_elem(int e, const RootSet& rs, acb_t out);

  void horner(acb_t y, acb_t dy, const vector<ACB>& f, const acb_t x);

  // interval Newton from ball z; the root in z is unique when N(z) is inside z

  void newton(const vector<ACB>& f, const acb_t z, acb_t out);

  void make_duals(RootSet& rs, int i);

  // build level i of rs (levels < i present) with candidate z for the root

  void build_level(RootSet& rs, int i, const ACB& z);

  RootSet base();

  RootSet apply_generator(const RootSet& old, int l);

  RootSet roots_for_word(const RootSet& base_rs, const vector<Uint64>& w);

  // entry rule (spec 3.5)

  void entry_word(Uint64 m, Uint64& rep, vector<Uint64>& word);
};

// Precondition: roots encloses all deg(f) roots, certified simple and pairwise
// disjoint. If every coefficient has exactly zero imaginary part, conjugation
// permutes the exact roots. The conjugate of root j lies in conj(roots[j]); if
// that box excludes every other root enclosure, completeness forces it to be
// root j itself. Thus Im(root j) = 0 exactly, even if its isolating ball has a
// nonzero imaginary radius. This is an equality proof, not a small-radius test.
// Nonreal coefficients or unresolved conjugate matches leave the balls intact;
// refinement and the bounded exact fallback still handle unresolved comparisons.
void certify_real_roots(const vector<ACB>& f, vector<ACB>& roots);

// Return base roots only after each enclosure is certified to contain one root
// and to be contained in its stored rectangle, including exact singleton parts.
RootSet numeric_base(Container& C, slong prec, vector<vector<ACB>>* all);
}  // namespace sic_check::verifier
