// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Extended Clifford group action on WH SIC fiducials (binary64): Appleby's unitaries U_F,
// Clifford-invariant overlap-phase fingerprints, explicit Clifford equivalences and stabilizers.
//
// Conventions follow Appleby2005symmetric (the repository's D_p convention):
//   dbar = 2d (d even) or d (d odd);  F = [[a, b], [c, e]] in ESL(2, Z_dbar), acting on column
//   labels p = (p1, p2).  For det F = 1 and b a unit mod dbar,
//     U_F = d^(-1/2) sum_{u,v} tau^(b'(a v^2 - 2 u v + e u^2)) |u><v|,   b' = b^(-1) mod dbar,
//   and U_F D_p U_F^dagger = D_(F p).  If b is not a unit, F = F1 F2 with
//   F1 = [[0, -1], [1, x]], F2 = [[c + x a, e + x b], [-a, -b]] and e + x b a unit; U_F = U_F1
//   U_F2. For det F = -1, U_F = U_(F J) K with J = diag(1, -1) and K complex conjugation in the
//   standard basis (K D_p K = D_(J p)).  A general element is D_chi U_F, chi in Z_d^2.
//
// Clifford-invariant overlap-phase fingerprint (PEC invariant):
//   z_p = (a_p / |a_p|)^(2d),  a_p = <psi|D_p|psi>,   M_k = sum_{p != 0} Re(z_p^k) / (d^2 - 1).
// Every Clifford unitary U satisfies U D_p U^dagger = mu_p D_q with mu_p^(2d) = 1 and q = G p
// mod d for a symplectic G, so z is permuted (and complex-conjugated for an antiunitary); z_p
// depends on p mod d only.  Hence psi = c D_chi U_F phi implies z_psi(p) = z_phi(F^(-1) p).
//
// Equivalence search (clifford_maps): G = F^(-1) mod d is determined by G e1 and G e2, which
// must lie in the level sets {q : z_phi(q) = z_psi(e1)}, {q : z_phi(q) = z_psi(e2)} and satisfy
// det[G e1, G e2] = 1.  Enumerating those pairs (within a tolerance that exceeds the numerical
// error of z) is therefore exhaustive over the extended Clifford group; each surviving G is
// checked on all d^2 labels and then on the vectors: u = U_F phi and the displacement chi is
// the argmax of |<D_chi u|psi>| (all chi by FFT).  A reported element satisfies
// 1 - |<psi|D_chi U_F phi>| <= residual_tol for the normalized binary64 inputs.  These are
// numerical statements about the binary64 points, not exact identities.

#include "sic/core/clifford.hpp"

namespace sic::search {
// ---------------------------------------------------------------- fingerprints
struct PhaseTable {
  sic::core::clifford::Int d = 0;
  std::vector<sic::core::Complex> z;    // index p1 * d + p2; z[0] = 1
  sic::core::Real min_abs_overlap = 0;  // min_{p != 0} |a_p| (positive for a SIC: 1/sqrt(d+1))
  sic::core::Complex at(sic::core::clifford::Int p1, sic::core::clifford::Int p2) const {
    return z[static_cast<std::size_t>(sic::core::clifford::mod(p1, d) * d +
                                      sic::core::clifford::mod(p2, d))];
  }
};

PhaseTable phase_table(const sic::core::SicFrame& frame);

// M_k = sum_{p != 0} Re(z_p^k) / (d^2 - 1), k = 1..count.
std::vector<sic::core::Real> clifford_moments(const PhaseTable& t, int count);

// A priori bound on |z_p(computed) - z_p(exact)| for a unit SIC fiducial whose binary64 entries
// are within `entry_error` (complex modulus) of the exact normalized entries; standard rounding
// model (not interval arithmetic):  |delta a| <= 2 sqrt(d) e + d e^2 + (4 log2 d + 8) eps,
// |delta arg a| <= (pi/2) |delta a| sqrt(d+1) / (1 - |delta a| sqrt(d+1)), |delta z| <= 2d |delta
// arg a| + (8 pi d + 8) eps.
sic::core::Real z_error_bound(sic::core::clifford::Int d, sic::core::Real entry_error);

// ---------------------------------------------------------------- equivalences
struct MatchOptions {
  sic::core::Real z_tol = 1e-6;         // must exceed the numerical error of z (see z_error_bound)
  sic::core::Real residual_tol = 1e-6;  // acceptance threshold for 1 - |<psi|D_chi U_F phi>|
  bool antiunitary = true;              // also search det -1
  bool find_all = false;                // false: stop at the first element
};

struct MatchStats {
  std::size_t level_set_sizes = 0;  // sum over passes of |{q : z_phi(q) ~ z_psi(e1)}| + |{.. e2}|
  std::size_t candidate_pairs = 0;  // (G e1, G e2) pairs with det 1 tested on all labels
  std::size_t z_survivors = 0;      // G passing the all-label z test
  // Smallest all-label z deviation among rejected G (-1: none).
  sic::core::Real best_rejected_z = -1;
  sic::core::Real best_rejected_residual =
      -1;  // smallest residual among z-survivors without a displacement (-1: none)
};

// All (or the first) extended Clifford elements U with psi = c U phi, |c| = 1, for SIC fiducials
// phi, psi of the same dimension (normalized internally). The search over G is exhaustive when
// z_tol exceeds the z errors of both inputs (see header comment and z_error_bound).
// Empty or differently sized inputs throw sic::Error(invalid_argument); normalization
// and frame admission use the core API failure contract.
std::vector<sic::core::clifford::CliffordElement> clifford_maps(const sic::core::Vector& phi_in,
                                                                const sic::core::Vector& psi_in,
                                                                const MatchOptions& opt = {},
                                                                MatchStats* stats = nullptr);

}  // namespace sic::search
