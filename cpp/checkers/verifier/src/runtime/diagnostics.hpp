// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::verifier {

[[noreturn]] void reject(const string& m);

void say(const char* f, ...);
}  // namespace sic_check::verifier
