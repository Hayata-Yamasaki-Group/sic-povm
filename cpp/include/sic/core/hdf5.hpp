// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Optional deterministic HDF5 fiducial interchange.
// Reads enforce default InputLimits dimension/byte bounds before allocation (resource).
// Calls throw sic::Error(unsupported) when have_h5() is false.
#include "sic/core/interchange.hpp"
namespace sic::core::interchange {
struct StringAttribute {
  std::string name;
  std::string value;
};
struct NumericAttribute {
  std::string name;
  double value;
};
bool have_h5();
void write_h5(const std::string& path, const sic::core::Vector& psi,
              const std::vector<StringAttribute>& str_attrs,
              const std::vector<NumericAttribute>& num_attrs);
sic::core::Vector read_h5(const std::string& path);
}  // namespace sic::core::interchange
