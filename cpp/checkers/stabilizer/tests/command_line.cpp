// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../command_line.hpp"

#include <cmath>
#include <iostream>

#include "../certificate.hpp"

namespace sic_check::stabilizer {
int certify(const std::string&, const std::string&, const std::string&, const std::string&,
            slong prec, double threshold, long) {
  std::cout << "prec=" << prec << " threshold=2^" << std::log2(threshold) << '\n';
  return 0;
}
}  // namespace sic_check::stabilizer
int main(int argc, char** argv) { return sic_check::stabilizer::run_command(argc, argv); }
