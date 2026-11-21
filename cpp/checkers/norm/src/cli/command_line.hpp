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

#include "../format/input.hpp"
namespace norm = sic_check::norm;
namespace sic_check::norm {
int run_command(int argc, char** argv);
}  // namespace sic_check::norm
