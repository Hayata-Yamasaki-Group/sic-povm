// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/stages/table/newton_diagnostics.hpp"

#include <algorithm>

#include "sic/support/kernel_settings.hpp"
namespace sic::construct {
void report_newton_table(size_t computed, size_t physical, size_t total, long precision,
                         size_t unknown_conditioning, double cmax,
                         const std::vector<char>& used_quadrature, double elapsed) {
  const size_t nq = std::count(used_quadrature.begin(), used_quadrature.end(), 1);
  if (unknown_conditioning)
    sic::support::progressf(
        "table: %zu Newton + %zu quadrature classes to %ld bits (conditioning unknown for %zu "
        "modular classes), %zu by conjugation   [%.1fs]\n",
        computed - nq, nq, (long)precision + 16, unknown_conditioning, total - physical, elapsed);
  else
    sic::support::progressf(
        "table: %zu Newton + %zu quadrature classes to %ld bits (cond ~ %.1e), %zu by "
        "conjugation   [%.1fs]\n",
        computed - nq, nq, (long)precision + 16, cmax, total - physical, elapsed);
}
}  // namespace sic::construct
