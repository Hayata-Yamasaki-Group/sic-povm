#include "sic/search/clifford.hpp"
#include "sic/support/byte_source.hpp"
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <sstream>

#include "sic/core/clifford.hpp"
#include "sic/core/fiducial_io.hpp"
#include "sic/core/verify.hpp"
#include "sic/support/cli.hpp"
#include "sic/support/parse_number.hpp"
#ifdef SIC_HAVE_FLINT
#error "The core consumer inherited a private FLINT definition"
#endif
#ifdef SIC_GROUP_SOURCE_ID
#error "The core consumer inherited source identity definitions"
#endif
int main() {
  using sic::support::parse_number;
  if (parse_number<unsigned>("42", "count", sic::ErrorKind::invalid_argument, 1, 42) != 42 ||
      parse_number<int>("-2", "offset", sic::ErrorKind::invalid_format, -2, 2) != -2 ||
      sic::support::cli::number<double>("1.5", "scale") != 1.5)
    return 1;
  try {
    sic::support::cli::number<unsigned>("-1", "count");
    return 1;
  } catch (const sic::support::cli::UsageError& error) {
    if (error.kind() != sic::ErrorKind::invalid_argument) return 1;
  }
  std::istringstream input("2\n1 0\n0 0\n");
  const auto psi = sic::core::read_vector(input);
  const sic::core::SicFrame frame(psi);
  const auto a = frame.overlaps();
  const auto b = sic::core::WeylHeisenberg(2).overlaps(psi);
  const auto npy = sic::core::interchange::npy_encode(psi);
  sic::core::Vector image;
  sic::core::clifford::CliffordAction(2).apply({0, 3, 1, 0}, psi, image);
  return !(a == b && image.size() == 2 &&
           sic::core::interchange::bit_equal(psi, sic::core::interchange::npy_decode(npy)) &&
           sic::core::verify_fiducial(psi).d == 2);
}
