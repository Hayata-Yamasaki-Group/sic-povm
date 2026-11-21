// SPDX-License-Identifier: AGPL-3.0-or-later
#include "search/catalogue_match.hpp"
#include "sic/data/catalogue.hpp"
// sic-identify: which catalogued WH SIC ray (if any) is a binary64 fiducial Clifford-equivalent to?
//   usage: sic-identify FILE [--catalogue TSV] [--repo DIR] [--tol T] [--entry-error E]
//                       [--moments K] [--unitary-only]
// FILE: .csv (index,real,imag), .npy ('<c16') or the .txt vector format, in the repository WH
// convention D_p = tau^(p1 p2) X^p1 Z^p2, tau = -exp(pi i/d) (docs/sicx.md#gabor section 8.2
// explains how to convert from Gabor/time-frequency conventions).
//
// For every catalogued ray of the same dimension it prints the Clifford-moment fingerprint
// M_1..M_K and searches exhaustively for an extended Clifford element U = D_chi U_F with
// psi = c U phi (see sic/core/clifford.hpp). A match prints F in ESL(2, Z_dbar) (det -1 means
// antiunitary, U_F = U_(FJ) K), the displacement chi and the residual 1 - |<psi|U phi>|.
// Exit: 0 MATCH, 1 NO_MATCH or not a SIC within tolerance, 2 usage error (runtime/input errors =
// 1). A match is a numerical statement about binary64 points; NO_MATCH means no catalogued ray of
// that dimension is equivalent within the tolerances, not that the input is a new SIC orbit.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "command.hpp"
#include "sic/core/fiducial_io.hpp"
#include "sic/core/sic_frame.hpp"
#include "sic/search/clifford.hpp"
#include "sic/support/cli.hpp"
namespace sic::apps::identify {
int command(const Options& invocation) try {
  const auto& [path, root, catalogue, z_tol, entry_error, sic_tol, moments, anti] = invocation;
  const sic::core::Vector psi = sic::core::files::read_fiducial(path);
  const int d = static_cast<int>(psi.size());
  const sic::core::SicFrame frame(psi);
  const sic::core::SicCheck chk = sic::core::check_sic(frame);
  std::printf("input %s\nd %d\ninput_norm %.17g\nmax_overlap_error %.3e\n", path.c_str(), d,
              chk.input_norm, chk.max_overlap_error);
  if (d < 2 || !chk.passes(sic_tol)) {
    std::printf("NOT_A_SIC: max | |<psi|D_p|psi>|^2 - 1/(d+1) | exceeds %.1e; no identification\n",
                sic_tol);
    return 1;
  }
  const double bound = sic::search::z_error_bound(d, entry_error);
  std::printf(
      "z_tol %.3e\nz_error_bound_per_input %.3e (entry error %.3e; standard rounding model)\n",
      z_tol, bound, entry_error);
  if (2 * bound > z_tol)
    std::printf(
        "WARNING: 2 * z_error_bound exceeds z_tol; the search may miss an equivalence (raise "
        "--tol)\n");
  const auto zpsi = sic::search::phase_table(frame);
  const auto mpsi = sic::search::clifford_moments(zpsi, moments);
  std::printf("fingerprint");
  for (double m : mpsi) std::printf(" %.15f", m);
  std::printf("\n");

  const auto entries = sic::data::read_catalogue(catalogue);
  sic::search::MatchOptions opt;
  opt.z_tol = z_tol;
  opt.antiunitary = anti;
  int searched = 0, missing = 0;
  std::vector<std::pair<sic::data::CatalogueEntry, sic::core::clifford::CliffordElement>> matches;
  for (const auto& e : entries) {
    if (e.d != d) continue;
    const std::string csv = (std::filesystem::path(root) / e.csv).string();
    if (!std::filesystem::exists(csv)) {
      std::printf("ray %s unavailable (%s)\n", e.label.c_str(), e.csv.c_str());
      ++missing;
      continue;
    }
    const sic::core::Vector phi = sic::core::files::read_fiducial(csv);
    if (static_cast<int>(phi.size()) != d)
      throw sic::Error(sic::ErrorKind::integrity, csv + ": dimension differs from catalogue");
    ++searched;
    const auto comparison = sic::search::catalogue_match(phi, psi, opt, moments, mpsi);
    const auto dm = comparison.moment_difference;
    const auto& st = comparison.stats;
    const auto& found = comparison.matches;
    std::printf("ray %s order %s status %s max_moment_difference %.3e candidates %zu %s\n",
                e.label.c_str(), e.order.c_str(), e.status.c_str(), dm, st.candidate_pairs,
                found.empty() ? "no" : "MATCH");
    if (!found.empty()) matches.emplace_back(e, found.front());
  }
  if (matches.empty()) {
    std::printf("NO_MATCH among %d catalogued rays of dimension %d (%d unavailable)\n", searched, d,
                missing);
    return 1;
  }
  for (const auto& [e, el] : matches)
    std::printf(
        "MATCH %s (d %d, order %s, status %s): psi = c D_chi U_F phi (%s), F = %s mod %lld (det "
        "%+lld), "
        "chi = (%lld, %lld), residual %.3e, z_deviation %.3e\n",
        e.label.c_str(), d, e.order.c_str(), e.status.c_str(),
        el.antiunitary() ? "antiunitary, U_F = U_(FJ) K" : "unitary",
        sic::core::clifford::to_string(el.F).c_str(),
        static_cast<long long>(sic::core::clifford::dbar_of(d)), static_cast<long long>(el.sign),
        static_cast<long long>(el.chi1), static_cast<long long>(el.chi2), el.residual,
        el.z_deviation);
  if (matches.size() > 1)
    std::printf("WARNING: several catalogued rays match (catalogue duplicates?)\n");
  return 0;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::apps::identify
