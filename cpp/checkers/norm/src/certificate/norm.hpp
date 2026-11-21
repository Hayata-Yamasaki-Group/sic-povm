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

#include "../algebra/algebra.hpp"
namespace norm = sic_check::norm;
namespace sic_check::norm {

struct RayResult {
  std::string label;
  bool pass = false, ball_ok = false;
  double worst_log2_R = -1e300;
  slong worst_upper = LONG_MIN;
  size_t pairs = 0;
  double seconds = 0;
  slong precision = 0;
};
int certify(const std::string& path, unsigned threads, bool estimate, bool leaf_estimate,
            bool verify_input, const ReadOptions& input_options,
            const std::optional<size_t>& only_ray, slong max_bits);
}  // namespace sic_check::norm
