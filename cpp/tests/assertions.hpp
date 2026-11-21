// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Minimal test harness (no external dependencies).
// Use SIC_CHECK / SIC_CHECK_NEAR in a test, and end main() with `return sic_test::report();`.

#include <cmath>
#include <complex>
#include <iostream>

namespace sic_test {
int& failures();
int& checks();
int report();
}  // namespace sic_test

#define SIC_CHECK(cond)                                                          \
  do {                                                                           \
    ++sic_test::checks();                                                        \
    if (!(cond)) {                                                               \
      ++sic_test::failures();                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
    }                                                                            \
  } while (0)

#define SIC_CHECK_NEAR(a, b, tol)                                                      \
  do {                                                                                 \
    ++sic_test::checks();                                                              \
    const auto _err = std::abs((a) - (b));                                             \
    if (!(_err <= (tol))) {                                                            \
      ++sic_test::failures();                                                          \
      std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_NEAR failed: |" #a " - " #b \
                << "| = " << _err << " > " << (tol) << "\n";                           \
    }                                                                                  \
  } while (0)
