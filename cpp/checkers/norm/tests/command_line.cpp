// SPDX-License-Identifier: AGPL-3.0-or-later
// Observe the real CLI at its certificate boundary, without running numerical work.
#include "../src/cli/command_line.hpp"

#include <iostream>

#include "../src/certificate/norm.hpp"
#include "../src/format/container.hpp"

namespace sic_check::norm {
// Trust options are outside this numeric/defaults test; never substitute a mock hash parser.
Hash parse_hash(const std::string&) {
  throw std::logic_error("unexpected trust option in CLI test");
}
int certify(const std::string&, unsigned threads, bool, bool, bool, const ReadOptions& options,
            const std::optional<size_t>& only_ray, slong max_bits) {
  std::cout << "threads=" << threads << " max-bits=" << max_bits
            << " max-dimension=" << options.max_dimension
            << " max-field-dimension=" << options.max_field_dimension;
  if (only_ray) std::cout << " ray=" << *only_ray;
  for (auto id : options.elements) std::cout << " element=" << id;
  for (auto id : options.physical_chunks) std::cout << " chunk=" << id;
  std::cout << '\n';
  return 0;
}
}  // namespace sic_check::norm
int main(int argc, char** argv) { return sic_check::norm::run_command(argc, argv); }
