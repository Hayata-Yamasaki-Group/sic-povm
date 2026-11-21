// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstddef>

namespace sic::core::detail {
// Admit the native overlap index before narrowing or accessing vector storage.
int checked_verify_dimension(std::size_t size);
}  // namespace sic::core::detail
