// SPDX-License-Identifier: AGPL-3.0-or-later
// A bounded search proposal, never a certificate: every named generator must
// be reconstructed and certified against the current tower before use.
#pragma once

#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sic::exact {

struct StepSelectionKey {
  long dimension, order, degree;
  int levels;
};

std::vector<std::string> read_step_selection(const char* path, const StepSelectionKey& expected);

void write_step_selection(const char* path, const StepSelectionKey& key,
                          const std::vector<std::string>& names);

}  // namespace sic::exact
