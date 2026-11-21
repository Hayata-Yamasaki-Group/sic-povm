// SPDX-License-Identifier: AGPL-3.0-or-later
#include "diagnostics.hpp"

namespace norm = sic_check::norm;
namespace sic_check::norm {
void require(bool ok, const std::string& reason) {
  if (!ok) throw std::runtime_error("REFUSED: SICX " + reason);
}

void resource(bool ok, const std::string& reason) {
  if (!ok) throw ResourceRefusal("REFUSED: SICX resource/precision limit: " + reason);
}
double now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

[[noreturn]] void refuse(const std::string& why) { throw std::runtime_error("REFUSED: " + why); }
}  // namespace sic_check::norm
