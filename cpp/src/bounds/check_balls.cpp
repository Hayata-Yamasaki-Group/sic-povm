#include "bounds/check_balls.hpp"

#include <flint/acb.h>
#include <flint/arb.h>
#include <flint/flint.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "sic/bounds/overlap_bounds.hpp"
#include "sic/data/text_formats.hpp"
#include "sic/support/cli.hpp"
#include "sic/support/flint_raii.hpp"
#include "sic/support/parse_number.hpp"

namespace sic::bounds {
namespace {
[[noreturn]] void die(sic::ErrorKind kind, const std::string& message) {
  throw sic::Error(kind, message);
}
}  // namespace
int check_balls(const sic::support::cli::Arguments& args) {
  args.positionals(1, 1);
  sic::bounds::BallVerificationLimits limits = kDefaultBallLimits;
  limits.maximum_dimension =
      args.numeric<size_t>("--maximum-dimension", kDefaultBallLimits.maximum_dimension,
                           kMinimumBallDimension, kMaximumBallDimension);
  limits.maximum_working_bits =
      args.numeric<long>("--maximum-precision", kDefaultBallLimits.maximum_working_bits,
                         kMinimumBallWorkingBits, sic::kNativeMaximumPrecision);
  limits.maximum_work_score =
      args.numeric<uint64_t>("--maximum-work", kDefaultBallLimits.maximum_work_score, 1);
  limits.maximum_workspace_bytes =
      args.numeric<uint64_t>("--memory-mib", kDefaultBallLimits.maximum_workspace_bytes >> 20, 1,
                             UINT64_MAX >> 20)
      << 20;
  const long bits = args.numeric<long>("--bits", kDefaultBallAccuracyBits, 1,
                                       limits.maximum_working_bits - kBallAccuracyHeadroom);
  const long working = args.numeric<long>("--working-bits", kDefaultBallWorkingBits,
                                          kMinimumBallWorkingBits, limits.maximum_working_bits);
  limits.working_bits = working;
  limits.minimum_absolute_accuracy_bits = bits + 8;
  std::ifstream in(args.pos[0], std::ios::binary);
  if (!in) die(sic::ErrorKind::io, "cannot open ball file " + std::string(args.pos[0]));
  auto line_read = [&](std::string& text) {
    const bool ok = bool(std::getline(in, text));
    if (in.bad()) die(sic::ErrorKind::io, "cannot read ball file");
    return ok;
  };
  std::string line;
  if (!line_read(line) || line != sic::data::kVectorBallsIdentity)
    die(sic::ErrorKind::invalid_format,
        std::string(args.pos[0]) + ": not a sic-vector-balls/1.0 file");
  if (!line_read(line) || line.rfind("d ", 0) != 0)
    die(sic::ErrorKind::invalid_format, "missing dimension line");
  const size_t d = sic::support::parse_number<size_t>(line.substr(2), "dimension",
                                                      sic::ErrorKind::invalid_format,
                                                      kMinimumBallDimension, kMaximumBallDimension);
  (void)sic::bounds::admit_wh_bounds(d, limits);
  acb_ptr v = _acb_vec_init(static_cast<slong>(d));
  struct Guard {
    acb_ptr v;
    slong n;
    ~Guard() { _acb_vec_clear(v, n); }
  } guard{v, static_cast<slong>(d)};
  for (size_t j = 0; j < d; ++j) {
    if (!line_read(line)) die(sic::ErrorKind::invalid_format, "truncated ball file");
    const size_t bar = line.find(" | ");
    if (bar == std::string::npos)
      die(sic::ErrorKind::invalid_format, "bad ball line " + std::to_string(j));
    if (arb_load_str(acb_realref(v + j), line.substr(0, bar).c_str()) ||
        arb_load_str(acb_imagref(v + j), line.substr(bar + 3).c_str()))
      die(sic::ErrorKind::invalid_format, "bad Arb dump in line " + std::to_string(j));
  }
  if (!line_read(line) || line != "end") die(sic::ErrorKind::invalid_format, "missing end line");
  if (in.peek() != std::char_traits<char>::eof())
    die(sic::ErrorKind::invalid_format, "trailing ball data");
  if (in.bad()) die(sic::ErrorKind::io, "cannot read ball file");
  const auto r = sic::bounds::bound_wh_overlap_deviations(v, d, limits);
  sic::support::flint::Arb bound;
  arb_one(bound);
  arb_mul_2exp_si(bound, bound, -bits);
  const bool pass = arb_lt(r.maximum_squared_overlap_deviation(), bound);
  sic::support::flint::Arf up;
  arb_get_ubound_arf(up, r.maximum_squared_overlap_deviation(), 64);
  const double up_log2 = arf_is_zero(up) ? -1e9 : std::log2(arf_get_d(up, ARF_RND_UP));
  std::cout << "SIC-CHECK d=" << d << " labels=" << r.info().checked_nonidentity_labels
            << " working_bits=" << working << " max_deviation_upper_log2="
            << (arf_is_zero(up) ? std::string("-inf") : std::to_string(up_log2)) << " required<2^-"
            << bits << (pass ? " PASS" : " FAIL") << '\n';
  return pass ? 0 : 1;
}

}  // namespace sic::bounds
