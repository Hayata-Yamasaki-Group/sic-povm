// SPDX-License-Identifier: AGPL-3.0-or-later
// read-fiducial: load a released SIC-POVM fiducial from FILE and verify the Weyl-Heisenberg SIC
// property  |<psi|D_(a,b)|psi>|^2 = 1/(d+1)  for all (a,b) != (0,0).
//
//   read-fiducial fiducial-fp64.csv      the released binary64 CSV (index,real,imag)
//   read-fiducial fiducial.npy           NumPy v1.0 '<c16' vector written by sic-data
//   read-fiducial fiducial-balls.txt     decimal Arb balls (needs the FLINT build): the
//                                        deviation is then a rigorous enclosure, not a diagnostic
//
// Convention: D_(a,b) = tau^(ab) X^a Z^b, tau = -exp(i pi/d), X|j> = |j+1>, Z|j> = omega^j |j>.
// Then (D psi)_k = tau^(ab) omega^(b(k-a)) psi_(k-a) and
//   <psi|D_(a,b)|psi> = (tau omega^-1)^(ab) * sum_k u^a_k omega^(bk),   u^a_k = conj(psi_k)
//   psi_(k-a).
// The prefactor has modulus 1, so for each shift a ONE length-d DFT of u^a gives the overlaps for
// all d values of b: O(d^2 log d) in total instead of O(d^3).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "sic/core/fiducial_io.hpp"
#include "sic/core/sic_frame.hpp"
#include "sic/support/cli.hpp"

#ifdef SIC_HAVE_FLINT
#include <flint/acb.h>
#include <flint/acb_dft.h>
#endif

using Complex = std::complex<double>;
using Vec = std::vector<Complex>;

#ifdef SIC_HAVE_FLINT
// ---- rigorous check on decimal balls ------------------------------------------------------
// Same computation with Arb balls: acb_dft computes sum_k x_k exp(-2 pi i j k / n); the set of
// moduli over j is the same as for the +sign transform, so the maximum is unaffected.
static double ball_max_deviation(const std::string& path, slong prec, std::size_t& dim_out) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open " + path);
  std::string line;
  std::vector<std::string> re, im;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const auto i0 = line.find("  ["),
               i1 = line.find("]  [",
                              i0 == std::string::npos ? 0 : i0);  // "j  [re +/- r]  [im +/- r]"
    if (i0 == std::string::npos || i1 == std::string::npos || line.back() != ']')
      throw std::runtime_error("malformed ball row: " + line);
    if (std::stoul(line.substr(0, i0)) != re.size())
      throw std::runtime_error("ball rows out of order at row " + std::to_string(re.size()));
    re.push_back(line.substr(i0 + 2, i1 + 1 - (i0 + 2)));
    im.push_back(line.substr(i1 + 3));
  }
  if (re.size() < 2) throw std::runtime_error("ball file has fewer than two coordinates");
  dim_out = re.size();
  const slong d = static_cast<slong>(re.size());
  acb_ptr psi = _acb_vec_init(d), u = _acb_vec_init(d), f = _acb_vec_init(d);
  for (slong j = 0; j < d; ++j)
    if (arb_set_str(acb_realref(psi + j), re[j].c_str(), prec) ||
        arb_set_str(acb_imagref(psi + j), im[j].c_str(), prec) || !acb_is_finite(psi + j))
      throw std::runtime_error("arb_set_str failed or non-finite ball in row " + std::to_string(j));
  arb_t target, dev, worst;
  arb_init(target);
  arb_init(dev);
  arb_init(worst);
  arb_set_si(target, 1);
  arb_div_si(target, target, d + 1, prec);
  arb_zero(worst);
  acb_dft_pre_t pre;
  acb_dft_precomp_init(pre, d, prec);
  for (slong a = 0; a < d; ++a) {
    for (slong k = 0; k < d; ++k) {
      acb_conj(u + k, psi + k);
      acb_mul(u + k, u + k, psi + ((k - a) % d + d) % d, prec);
    }
    acb_dft_precomp(f, u, pre, prec);
    for (slong b = 0; b < d; ++b) {
      if (a == 0 && b == 0) continue;
      acb_abs(dev, f + b, prec);     // |z| as a ball
      arb_mul(dev, dev, dev, prec);  // |z|^2
      arb_sub(dev, dev, target, prec);
      arb_abs(dev, dev);
      arb_get_ubound_arf(arb_midref(dev), dev, prec);  // upper bound of the deviation
      mag_zero(arb_radref(dev));
      if (arf_is_nan(arb_midref(dev)) || !arb_is_finite(dev))
        throw std::runtime_error("non-finite overlap ball");
      if (arf_cmp(arb_midref(dev), arb_midref(worst)) > 0) arb_set(worst, dev);
    }
  }
  const double r = arf_get_d(arb_midref(worst), ARF_RND_UP);
  acb_dft_precomp_clear(pre);
  _acb_vec_clear(psi, d);
  _acb_vec_clear(u, d);
  _acb_vec_clear(f, d);
  arb_clear(target);
  arb_clear(dev);
  arb_clear(worst);
  return r;
}
#endif

namespace cli = sic::support::cli;
namespace {
const cli::Parser kParser{"read-fiducial FILE.csv|FILE.npy|FILE.txt", {}, false};
}

int main(int argc, char** argv) {
  cli::prepare_stdout();
  try {
    const auto args = kParser.parse(argc, argv);
    args.positionals(1, 1);
    const std::string path = args.pos[0];
    const auto ends_with = [&](const char* suffix) { return path.ends_with(suffix); };
    const auto t0 = std::chrono::steady_clock::now();
    double dev = 0;
    std::size_t d = 0, wa = 0, wb = 0;
    const char* kind = "";
    if (ends_with(".txt")) {
#ifdef SIC_HAVE_FLINT
      kind = "rigorous upper bound (Arb balls, 256-bit)";
      dev = ball_max_deviation(path, 256, d);
#else
      throw std::runtime_error("built without FLINT: ball text input unavailable");
#endif
    } else {
      const Vec psi = sic::core::files::read_fiducial(path);
      d = psi.size();
      kind = "binary64 diagnostic";
      const auto check = sic::core::check_sic(psi);
      dev = check.max_overlap_error;
      wa = static_cast<std::size_t>(check.worst_p1);
      wb = static_cast<std::size_t>(check.worst_p2);
    }
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s: d = %zu\nmax | |<psi|D_(a,b)|psi>|^2 - 1/(d+1) | = %.3e (%s)", path.c_str(), d,
                dev, kind);
    if (!ends_with(".txt")) std::printf(" at (a,b) = (%zu,%zu)", wa, wb);
    std::printf("\nelapsed %.3f s\n", secs);
    return cli::finish_stdout(dev < 1e-10 ? 0 : 1, "read-fiducial");  // NaN fails.
  } catch (const std::exception& e) {
    return cli::report(kParser, e, "error");
  }
}
