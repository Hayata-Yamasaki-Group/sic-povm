// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/numeric/frame_diagnostics.hpp"
// sic-overlaps: binary64 SIC frame check of a fiducial file (.csv, .npy or the .txt vector format).
//   usage: sic-overlaps FILE [--tol T] [--vectors] [--table OUT.bin]
// Computes all d^2 overlaps <psi|D_p|psi> (one FFT per shift, O(d^2 log d)) and reports the
// normalization, max_{p != 0} | |<psi|D_p|psi>|^2 - 1/(d+1) | and timings. --vectors also streams
// all d^2 frame vectors D_p psi (O(d^3), O(d) memory) and reports max | ||D_p psi|| - 1 | and a
// checksum. --table writes the overlaps as raw native-endian complex128, index p1*d + p2.
// Exit: 0 passes the selected tolerance, 1 fails, 2 usage error (runtime/input errors = 1). Not a
// certificate.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "sic/core/fiducial_io.hpp"
#include "sic/core/sic_frame.hpp"
#include "sic/support/cli.hpp"

namespace {
double seconds_since(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
}  // namespace

#include "command.hpp"
namespace sic::apps::overlaps {
int command(const Options& invocation) try {
  const auto& [path, table, tol, vectors] = invocation;
  auto t0 = std::chrono::steady_clock::now();
  const sic::core::Vector psi = sic::core::files::read_fiducial(path);
  const double t_read = seconds_since(t0);
  t0 = std::chrono::steady_clock::now();
  const sic::core::SicFrame frame(psi);
  const sic::core::SicCheck r = sic::core::check_sic(frame);
  const double t_check = seconds_since(t0);
  std::printf(
      "d %d\ninput_norm %.17g\nnorm_defect %.3e\nmax_overlap_error %.3e at p = (%lld, %lld)\n"
      "parseval_defect %.3e\ntime_read_s %.3f\ntime_overlaps_s %.3f\n",
      r.d, r.input_norm, r.norm_defect, r.max_overlap_error, static_cast<long long>(r.worst_p1),
      static_cast<long long>(r.worst_p2), r.parseval_defect, t_read, t_check);
  if (!table.empty()) {
    std::ofstream out(table, std::ios::binary | std::ios::trunc);
    if (!out) throw sic::Error(sic::ErrorKind::io, "cannot create " + table);
    sic::core::Vector row;
    for (int p1 = 0; p1 < r.d; ++p1) {
      frame.overlap_row(p1, row);
      out.write(reinterpret_cast<const char*>(row.data()),
                static_cast<std::streamsize>(row.size() * sizeof(sic::core::Complex)));
    }
    out.close();
    if (!out) throw sic::Error(sic::ErrorKind::io, "write failed for " + table);
    std::printf("table %s (%d x %d complex128, index p1*d+p2)\n", table.c_str(), r.d, r.d);
  }
  if (vectors) {
    t0 = std::chrono::steady_clock::now();
    const auto diagnostics = sic::core::frame_diagnostics(frame);
    const auto worst = diagnostics.worst;
    const auto checksum = diagnostics.checksum;
    std::printf(
        "vectors %lld\nmax_vector_norm_defect %.3e\nchecksum_sum_first_entries %.17g %.17g\n"
        "time_vectors_s %.3f\n",
        static_cast<long long>(r.d) * r.d, worst, checksum.real(), checksum.imag(),
        seconds_since(t0));
  }
  const bool ok = r.passes(tol);
  std::printf("%s (tol %.3g)\n",
              ok ? "OK: SIC fiducial within tolerance" : "FAIL: not a SIC fiducial", tol);
  return ok ? 0 : 1;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::apps::overlaps
