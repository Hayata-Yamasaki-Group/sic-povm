// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdio>

#include "sic/search/clifford.hpp"
namespace sic::search {
struct StabilizerOptions {
  double z_tol = 1e-6, entry_error = std::ldexp(1.0, -50), residual_tol = 1e-9, sic_tol = 1e-8;
};
sic::core::clifford::Int element_order(const sic::core::clifford::CliffordElement& el,
                                       sic::core::clifford::Int d);
bool write_stabilizer(std::FILE* out, const std::string& label, const std::string& order,
                      const std::string& status, const sic::core::Vector& phi,
                      const StabilizerOptions& o);
}  // namespace sic::search
