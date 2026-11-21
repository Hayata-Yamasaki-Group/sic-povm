// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/flint.h>

#include <string>
namespace sic_check::stabilizer {
int certify(const std::string& stab_path, const std::string& root, const std::string& catalogue,
            const std::string& d2_exact, slong prec, double threshold);
}
