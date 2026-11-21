// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// SICX -> exact.json (schema sic-exact-json/1.0, docs/sicx.md section 9a).
//
// The stabilizer-adapted trace-dual leaves of a SICX file are converted to the relative power
// basis of the tower K = F_0 < F_1 < ... < F_t = L, F_{i+1} = F_i(g_i), and the Galois transports
// of the representatives are applied exactly, so that every coordinate of every ray is an exact
// element of L written in the monomial basis g^e (e little-endian mixed radix, coefficients in
// K = Q(omega), omega^2 = s*omega + m). Small d only (the cost is about D^2 K-multiplications per
// Galois image; d <= 32 is measured empirically).
//
// Premises are those of the specification section 1: the minimal polynomials, the action
// records and the branch rectangles come from the file. New here and certified at generation:
//  * root isolation: every published root ball contains exactly one root of its defining
//    polynomial (interval Newton on the published balls of the lower levels);
//  * complex conjugation as an exact automorphism of L, identified by ball arithmetic;
//  * the power-basis coordinates, evaluated at the published root balls, overlap the BALL chunk
//    of the file coordinate by coordinate (finalized files).

#include <flint/fmpq.h>

#include "sic/sicx/io.hpp"
namespace sic::sicx::exact {
struct QuadraticElement {  // a + b*omega
  fmpq_t a, b;
  QuadraticElement();
  ~QuadraticElement();
  QuadraticElement(const QuadraticElement& o);
  QuadraticElement(QuadraticElement&& o) noexcept;
  QuadraticElement& operator=(const QuadraticElement& o);
  QuadraticElement& operator=(QuadraticElement&& o) noexcept;
  bool zero() const;
  bool operator==(const QuadraticElement& o) const;
};
using Elt = std::vector<QuadraticElement>;

struct BuildOptions {
  slong precision = 768;  // working precision of the root certification and the numerical check
  slong places = 80;      // decimals of the published root balls
  // Optional exact semilinear conjugation images of g_i in F_(i+1).
  // For an imaginary base these enable arbitrary presentations. Images are
  // checked against every defining relation, the embedding and involution.
  // Empty: identify a diagonal (+/-g_i) quadratic presentation automatically.
  std::vector<Elt> conjugation_images;
  uint64_t maximum_conjugation_search = uint64_t{1} << 20;
  bool numeric_check = true;
  std::string
      multiplet;  // release id m<N> (not derivable from the file); empty: ray ids are d<d>-c<N>
};

struct BuildReport {
  size_t coordinates = 0, checked = 0;
  double max_radius_log2 = -1e9;  // largest log2 radius of an evaluated coordinate (normalized)
  std::vector<std::string> notes;
  std::vector<uint64_t> conjugation;  // Smith word of complex conjugation (D0 > 0)
};

// Owns document-building arithmetic through rendering and report consumption.
// The borrowed Reader must outlive this builder.
class DocumentBuilder {
 public:
  DocumentBuilder(const Reader& reader, BuildOptions options);
  DocumentBuilder(Reader&&, BuildOptions) = delete;
  DocumentBuilder(const Reader&&, BuildOptions) = delete;
  ~DocumentBuilder();
  DocumentBuilder(DocumentBuilder&&) noexcept;
  DocumentBuilder& operator=(DocumentBuilder&&) noexcept;
  DocumentBuilder(const DocumentBuilder&) = delete;
  DocumentBuilder& operator=(const DocumentBuilder&) = delete;
  sic::support::Json build_json(const std::string& sicx_name, const std::string& sicx_sha256);
  const BuildReport& report() const;

 private:
  class State;
  std::unique_ptr<State> state_;
};
}  // namespace sic::sicx::exact
