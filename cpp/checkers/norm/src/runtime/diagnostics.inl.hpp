// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace sic_check::norm {
inline void input_require(bool ok, const std::string& reason) {
  if (!ok) throw InvalidInput("REJECTED: SICX " + reason);
}

}  // namespace sic_check::norm
