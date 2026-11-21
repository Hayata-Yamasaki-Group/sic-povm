// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/data/text_formats.hpp"
#include "search/stabilizer.hpp"
#include "sic/data/catalogue.hpp"
// sic-stabilizer: stabilizer of WH SIC fiducials in the extended Clifford group (binary64).
//   usage: sic-stabilizer FILE [--tol T] [--entry-error E]
//          sic-stabilizer --dim D [--catalogue TSV] [--repo DIR] [--out FILE] [--tol T]
//          [--entry-error E]
// The second form processes every catalogued ray of dimension D and writes the
// sic-stabilizers/1.0 data file documented in docs/data.md#stab. The search is exhaustive over
// ESL(2, Z_d) (see sic/core/clifford.hpp) when z_tol exceeds twice the a priori z error bound;
// every reported element is verified on the vectors (residual 1 - |<phi|D_chi U_F phi>|). The
// element list is closed under multiplication mod d (checked exactly in integer arithmetic). Exit:
// 0 success, 1 a ray failed a check, 2 usage error (runtime/input errors = 1).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "sic/core/fiducial_io.hpp"
#include "sic/core/sic_frame.hpp"
#include "sic/search/clifford.hpp"
#include "sic/support/cli.hpp"

namespace {}  // namespace

#include "command.hpp"
namespace sic::apps::stabilizer {
int command(const Options& invocation) try {
  const auto& [path, root, out_path, catalogue, dim, o] = invocation;
  std::FILE* out = stdout;
  std::unique_ptr<std::FILE, decltype(&std::fclose)> owner(nullptr, std::fclose);
  if (!out_path.empty()) {
    out = std::fopen(out_path.c_str(), "w");
    if (!out) throw sic::Error(sic::ErrorKind::io, "cannot create " + out_path);
    owner.reset(out);
  }
  std::fprintf(out,
               "# %s\n"
               "# Extended Clifford stabilizers of WH SIC fiducials, D_p = tau^(p1 p2) X^p1 "
               "Z^p2, tau = -exp(pi i/d).\n"
               "# Element U = D_chi U_F, F = [[a, b], [c, e]] in ESL(2, Z_dbar) (canonical lift, "
               "Appleby2005symmetric);\n"
               "# det -1: U_F = U_(FJ) K antiunitary. Binary64 computation, see "
               "docs/data.md#stab; not an exact proof.\n",
               sic::data::kStabilizersIdentity);
  bool ok = true;
  if (!path.empty()) {
    ok = sic::search::write_stabilizer(out, std::filesystem::path(path).filename().string(), "-",
                                       "-", sic::core::files::read_fiducial(path), o);
  } else {
    std::fprintf(out, "# catalogue %s\n",
                 std::filesystem::path(catalogue).filename().string().c_str());
    int n = 0;
    for (const auto& e : sic::data::read_catalogue(catalogue)) {
      if (e.d != dim) continue;
      const std::string csv = (std::filesystem::path(root) / e.csv).string();
      if (!std::filesystem::exists(csv)) {
        std::fprintf(out, "# ray %s unavailable: %s\n", e.label.c_str(), e.csv.c_str());
        ok = false;
        continue;
      }
      const sic::core::Vector phi = sic::core::files::read_fiducial(csv);
      if (static_cast<int>(phi.size()) != dim)
        throw sic::Error(sic::ErrorKind::integrity, csv + ": dimension mismatch");
      ok = sic::search::write_stabilizer(out, e.label, e.order, e.status, phi, o) && ok;
      ++n;
    }
    std::fprintf(out, "end %d\n", n);
    if (n == 0) ok = false;
  }
  const bool write_failed = std::fflush(out) != 0 || std::ferror(out);
  const bool close_failed = owner && std::fclose(owner.release()) != 0;
  if (write_failed || close_failed)
    throw sic::Error(sic::ErrorKind::io,
                     "write failed for " + (out_path.empty() ? "stdout" : out_path));
  return ok ? 0 : 1;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::apps::stabilizer
