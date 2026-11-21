// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/sicx/json.hpp"
#include "sicx/exact/conjugation.hpp"
#include "sicx/exact/element_json.hpp"
#include "sicx/exact/embedding.hpp"
namespace sic::sicx::exact {
std::vector<QuadraticElement> read_leaves(const Reader& reader, size_t id, const Tower& T);

// Decimal "0.00..01" with `places` decimals (10^-places).
std::string unit_decimal(slong places);
void arb_from_decimal(arb_t out, const std::string& text, slong prec);

struct Builder {
  const Reader& reader;
  const Meta& meta;
  Tower T;
  BuildOptions opt;
  BuildReport report;
  std::vector<Elt> reps;  // by representative position order in `rep_pos`
  std::map<int64_t, size_t> rep_index;
  std::vector<std::vector<Elt>> tau;  // tau[l][i] in F_{i+1}: tau_l(g_i)
  std::vector<ComplexBall> roots;     // published root balls
  ComplexBall omega;
  std::vector<std::string> root_text;  // 4 strings per level: re mid, re rad, im mid, im rad

  Builder(const Reader& r, BuildOptions o);

  size_t find_element(const std::string& kind, unsigned level, int64_t axis, int64_t k) const;
  Elt element_of(size_t id) const;

  void build_exact();

  // ---- certified roots
  static void load_branch(acb_t out, const std::array<std::string, 2>& b);
  void set_omega();
  // f_i as a polynomial over C with coefficients evaluated at the given roots.
  void level_polynomial(acb_poly_t P, unsigned i, const std::vector<ComplexBall>& rts, slong prec);
  void certify_roots();

  // ---- Galois images
  using Images = std::vector<Elt>;
  Images identity_images() const;
  Images compose(const Images& S, size_t l) const;
  Images word_images(const std::vector<uint64_t>& word) const;
  // Exact check that g_i -> S[i] (and omega -> s - omega if conjugate_k) extends to a ring
  // endomorphism.
  void check_endomorphism(const Images& S, bool conjugate_k, const std::string& what) const;

  std::vector<ComplexBall> conj_images_numeric(const Images& S, bool conjugate_k) const;

  // Complex conjugation as an exact automorphism of L; returns the images of g_i, sets conj_k.
  Images conj;
  bool conj_k = false;
  void find_conjugation();

  Elt conj_omega() const;
  std::vector<Elt> conj_generators_full() const;

  // ---- rays
  struct RayData {
    std::string id;
    std::vector<uint64_t> word;
    std::vector<Elt> coordinates;
  };
  std::vector<RayData> build_rays();

  // Numerical check: the power-basis coordinates at the published root balls overlap BALL.
  void numeric_check(const std::vector<RayData>& rays);

  sic::support::Json build_json(const std::string& sicx_name, const std::string& sicx_sha256);
};

}  // namespace sic::sicx::exact
