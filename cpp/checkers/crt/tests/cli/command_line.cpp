// SPDX-License-Identifier: AGPL-3.0-or-later
// Replace only the certificate boundary so tests can observe actual parsed defaults and large
// limits.
#include "../../src/cli/command_line.hpp"

#include <iostream>

#include "../../src/certificate/crt_certificate.hpp"
#include "../../src/runtime/parallel_for.hpp"

namespace sic_check::crt::sicx {
unsigned g_threads = 0;
Hash parse_hash(const string&) { throw std::logic_error("unexpected trust option in CLI test"); }
}  // namespace sic_check::crt::sicx
namespace sic_check::crt {
int certify(const string&, const Trust&, const string&, Uint64 seed, bool, bool, Int64 int_max_dim,
            Int64 force_primes, slong prec0) {
  std::cout << "threads=" << g_threads << " seed=" << seed << " integral-max-dim=" << int_max_dim
            << " primes=" << force_primes << " prec=" << prec0 << '\n';
  return 0;
}
}  // namespace sic_check::crt
int main(int argc, char** argv) { return sic_check::crt::run_command(argc, argv); }
