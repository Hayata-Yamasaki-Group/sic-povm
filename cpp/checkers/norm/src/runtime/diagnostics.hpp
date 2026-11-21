// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/acb_poly.h>
#include <flint/fmpz.h>
#include <flint/fmpz_factor.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../model/input_model.hpp"
namespace norm = sic_check::norm;
namespace sic_check::norm {

// Retain the mathematical checker's original refusal contract.
void require(bool ok, const std::string& reason);

void input_require(bool ok, const std::string& reason);

void resource(bool ok, const std::string& reason);

double now();

[[noreturn]] void refuse(const std::string& why);
}  // namespace sic_check::norm

#include "diagnostics.inl.hpp"
