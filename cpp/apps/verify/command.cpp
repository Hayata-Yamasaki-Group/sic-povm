// SPDX-License-Identifier: AGPL-3.0-or-later
#include "access/source_arguments.hpp"
#include "core/numeric/overlap_deviation.hpp"
#include "sic/support/flint_raii.hpp"
#include "sicx/container/rescaling_input.hpp"
// sic-verify FILE [--threads T] [--quick] [--sic-check] [--rescaling]
//
// Full verification of a SICX 1.0 file: footer, LDIR/PDIR layout, Merkle root,
// every chunk SHA-256, canonical encoding of every block (re-encoding must
// reproduce its bytes), element invariants (primitive scale, minimal level
// set, attained bit bound), canonical BALL records, anchor gauge and norm,
// and canonical JSON chunks. --quick checks only the footer, LDIR/PDIR, Merkle
// root and EXMT. --sic-check adds an FP64 (non-certified) diagnostic of the
// SIC overlaps |<w, D_p w>|^2 = 1/(d+1) from the BALL midpoints. --rescaling also checks the
// canonical 2-adic generator form (docs/sicx.md section 3.7): the rule applied to the stored
// leaves must give J = 0 at every level; the EXMT record generator_rescaling, if any, is printed.
#include <cmath>
#include <complex>
#include <filesystem>
#include <iostream>

#include "sic/access/remote.hpp"
#include "sic/core/fft.hpp"
#include "sic/sicx/verify.hpp"
#include "sic/support/cli.hpp"
#include "sicx/codec/rescale.hpp"

namespace {
using C = std::complex<double>;
// max over p != 0 of | |<w, D_p w>|^2 - 1/(d+1) |; the modulus does not depend on tau.

}  // namespace

#include "command.hpp"
namespace sic::apps::verify {
int command(Invocation invocation) try {
  auto& input = invocation.input;
  const auto threads = invocation.threads;
  const auto quick = invocation.quick, sic_check = invocation.sic_check,
             rescaling = invocation.rescaling;
  flint_set_num_threads(1);
  sic::sicx::Reader reader(input.open(invocation.path));
  const auto& meta = reader.meta();
  std::cout << "SICX " << sic::sicx::kMajor << '.' << reader.minor() << " d=" << meta.d
            << " D0=" << meta.D0 << " [L:K]=" << meta.D << " levels=" << meta.levels()
            << " elements=" << meta.elements.size() << " rays=" << meta.rays.size()
            << " chunks=" << reader.index().size() << " merkle=" << sic::sicx::hex(reader.root())
            << " exact_root=" << sic::sicx::hex(reader.exact_root())
            << (reader.finalized() ? " finalized"
                                   : " NOT FINALIZED (exact data only; no RAYS/BALL)")
            << '\n';
  std::cout << "ROOT TRUST exact="
            << (input.trusted_exact() ? "matched supplied pin" : "self-consistent only")
            << " file=" << (input.trusted_file() ? "matched supplied pin" : "self-consistent only")
            << "; whole-file SHA-256 not established by directory verification\n";
  if (quick) {
    std::cout << "PASS quick (footer, LDIR/PDIR, Merkle root, EXMT)\n";
    return 0;
  }
  std::vector<size_t> chunks(reader.index().size());
  std::iota(chunks.begin(), chunks.end(), 0);
  reader.plan_chunks(chunks);
  const auto policy = invocation.verification_policy();
  const auto report = sic::sicx::verify_all(reader, threads, policy);
  uint64_t nonzero = 0, body = 0, tower = 0;
  for (size_t id = 0; id < meta.elements.size(); ++id) {
    nonzero += report.elements[id].nonzero;
    (meta.elements[id].kind == "rep" ? body : tower) += report.elements[id].payload_bytes;
  }
  for (size_t r = 0; r < meta.rays.size(); ++r) {
    std::cout << "RAY " << meta.rays[r].label << " accuracy_bits=" << report.balls[r].accuracy_bits
              << " norm=" << report.balls[r].norm;
    if (sic_check) {
      const auto parts = reader.ball_parts(r);
      std::vector<C> w(meta.d);
      sic::support::flint::Arb x;
      for (uint64_t j = 0; j < meta.d; ++j) {
        sic::sicx::ball_part_arb(x, parts[2 * j]);
        const double re = arf_get_d(arb_midref(x), ARF_RND_NEAR);
        sic::sicx::ball_part_arb(x, parts[2 * j + 1]);
        w[j] = C(re, arf_get_d(arb_midref(x), ARF_RND_NEAR));
      }
      std::cout << " fp64_sic_deviation=" << sic::core::overlap_deviation(w)
                << " (diagnostic, not certified)";
    }
    std::cout << '\n';
  }
  if (rescaling) {
    const std::string recorded =
        sic::support::dump_json(reader.meta_json().at("generator_rescaling"));
    const auto r = sic::sicx::rescaling_of_file(reader, threads);
    std::cout << "RESCALING rule " << sic::sicx::kRescalingRule
              << " on the stored data: J=" << r.text() << "; EXMT record " << recorded << '\n';
    if (!r.identity())
      throw sic::Error(
          sic::ErrorKind::integrity,
          "not in canonical 2-adic generator form (spec section 3.7): the rule still gives J=" +
              r.text());
  }
  std::cout << "PASS full: " << report.chunks << " chunks, " << report.bytes
            << " bytes, nonzero coordinates " << nonzero << ", tower+action payload " << tower
            << ", body payload " << body << '\n';
  return 0;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::apps::verify
