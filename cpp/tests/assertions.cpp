// SPDX-License-Identifier: AGPL-3.0-or-later
#include "assertions.hpp"
namespace sic_test {
int& failures() {
  static int f = 0;
  return f;
}

int& checks() {
  static int c = 0;
  return c;
}

int report() {
  std::cout << checks() << " checks, " << failures() << " failures\n";
  return failures() == 0 ? 0 : 1;
}
}  // namespace sic_test
