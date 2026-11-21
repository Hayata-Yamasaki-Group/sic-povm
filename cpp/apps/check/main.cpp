// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/tool_version.hpp"
// sic-check: numerically check the WH SIC fiducial condition for a vector file.
// Usage: sic-check <file|-> [tol]
// Exit code: 0 = fiducial within tol, 1 = not a fiducial, 2 = usage error; input/runtime errors
// = 1.

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "sic/core/fiducial_io.hpp"
#include "sic/core/verify.hpp"
#include "sic/support/cli.hpp"

namespace {
constexpr double kDefaultTolerance = 1e-10;
const sic::support::cli::Parser kParser{
    "sic-check FILE|- [TOL=" + sic::support::cli::default_text(kDefaultTolerance) + "]",
    {

    }};
}  // namespace

int main(int argc, char** argv) {
  if (const auto status = sic::support::cli::version_request(argc, argv, "sic-check"))
    return *status;
  try {
    const auto args = kParser.parse(argc, argv);
    args.positionals(1, 2);
    const std::string path = args.pos[0];
    const double tol = args.pos.size() == 2 ? sic::support::cli::number<double>(args.pos[1], "TOL")
                                            : kDefaultTolerance;

    const sic::core::Vector psi =
        (path == "-") ? sic::core::read_vector(std::cin) : sic::core::read_vector_file(path);
    const sic::core::VerifyResult r = sic::core::verify_fiducial(psi);
    const bool ok = r.is_fiducial(tol);
    std::cout << "d = " << r.d << "\n"
              << "input norm = " << r.input_norm << "\n"
              << "max | |<psi|D_p|psi>|^2 - 1/(d+1) | = " << r.max_overlap_error << "  at p = ("
              << r.worst_p1 << ", " << r.worst_p2 << ")\n"
              << (ok ? "OK: SIC fiducial" : "FAIL: not a SIC fiducial") << " (tol = " << tol
              << ")\n";
    return ok ? 0 : 1;
  } catch (const std::exception& e) {
    return sic::support::cli::report(kParser, e, "sic-check");
  }
}
