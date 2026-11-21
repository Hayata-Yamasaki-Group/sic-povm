// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/analytic/step_selection.hpp"

#include <unistd.h>

#include <fstream>

#include "assertions.hpp"
#include "temporary_directory.hpp"

int main() {
  sic_test::TemporaryDirectory work("sic-step-selection");
  const std::string path = (work.path / "selection.txt").string();
  const sic::exact::StepSelectionKey key{16, 1, 512, 9};
  const std::vector<std::string> names{
      "x1", "x1", "x2", "x1", "nonlinear-product", "zeta^4", "zeta^2", "x4", "sqrt(eps) zeta^4"};
  sic::exact::write_step_selection(path.c_str(), key, names);
  SIC_CHECK(sic::exact::read_step_selection(path.c_str(), key) == names);
  bool refused = false;
  try {
    sic::exact::write_step_selection(path.c_str(), key, names);
  } catch (const std::runtime_error&) {
    refused = true;
  }
  SIC_CHECK(refused);
  refused = false;
  try {
    sic::exact::read_step_selection(path.c_str(), {16, 2, 512, 9});
  } catch (const std::runtime_error&) {
    refused = true;
  }
  SIC_CHECK(refused);
  FILE* file = std::fopen(path.c_str(), "a");
  SIC_CHECK(file != nullptr);
  if (file) {
    std::fputs("extra\n", file);
    std::fclose(file);
  }
  refused = false;
  try {
    sic::exact::read_step_selection(path.c_str(), key);
  } catch (const std::runtime_error&) {
    refused = true;
  }
  SIC_CHECK(refused);
  std::remove(path.c_str());
  for (const auto* header :
       {"SIC_STEP_SELECTION 1", "sic-step-selection-checkpoint/2.0"}) {  // identity-negative
    {
      std::ofstream out(path);
      out << header << " 16 1 512 9\n";
      for (const auto& name : names) out << name << '\n';
    }
    refused = false;
    try {
      sic::exact::read_step_selection(path.c_str(), key);
    } catch (const std::runtime_error&) {
      refused = true;
    }
    SIC_CHECK(refused);
    std::remove(path.c_str());
  }
  return sic_test::report();
}
