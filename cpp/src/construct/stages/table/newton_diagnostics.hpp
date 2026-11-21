// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstddef>
#include <vector>
namespace sic::construct {
void report_newton_table(size_t computed, size_t physical, size_t total, long precision,
                         size_t unknown_conditioning, double cmax,
                         const std::vector<char>& used_quadrature, double elapsed);
}
