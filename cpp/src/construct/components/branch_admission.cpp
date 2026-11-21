// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/components/branch_admission.hpp"

namespace sic::construct {
sic::support::resources::Bytes branch_scratch_bytes(const std::vector<long>& degrees,
                                                    sic::support::resources::BitCount precision,
                                                    sic::support::resources::BitCount leaf_bits,
                                                    sic::support::resources::Bytes source_reader) {
  sic::support::resources::Count maximum = 2, sum = 0;
  for (long degree : degrees) {
    if (degree < 2)
      throw sic::Error(sic::ErrorKind::invalid_argument, "invalid branch tower degree");
    maximum = std::max<sic::support::resources::Count>(maximum, degree);
    sum = sic::support::resources::add(sum, degree);
  }
  const auto cells = sic::support::resources::add(
      2048,
      sic::support::resources::add(
          sic::support::resources::mul(4, degrees.size()),
          sic::support::resources::add(
              sic::support::resources::mul(2, sum),
              sic::support::resources::add(
                  sic::support::resources::mul(32, sic::support::resources::mul(maximum, maximum)),
                  sic::support::resources::mul(8, maximum)))));
  const auto bits =
      sic::support::resources::add(sic::support::resources::add(precision, leaf_bits), 64);
  return sic::support::resources::add(
      64 * sic::support::resources::kMib,
      sic::support::resources::add(
          source_reader,
          sic::support::resources::add(
              sic::support::resources::mul(sic::support::resources::add(degrees.size(), 1), 2048),
              sic::support::resources::mul(cells, sic::support::resources::complex_bytes(bits)))));
}

}  // namespace sic::construct
