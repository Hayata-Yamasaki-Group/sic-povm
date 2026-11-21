// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/acb.h>
#include <flint/acb_poly.h>
#include <flint/arb.h>
#include <flint/fmpz_poly.h>
#include <flint/mag.h>
#include <flint/nmod.h>
#include <flint/nmod_poly.h>
#include <flint/nmod_poly_factor.h>
#include <flint/ulong_extras.h>
#include <sys/resource.h>

#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <functional>
#include <mutex>
#include <random>
#include <thread>

#include "../format/sicx_input.hpp"
namespace sic_check::crt {
using namespace sic_check::crt::sicx;

using namespace sic_check::crt::sicx;

struct Check {
  string what;
  Int64 B = 0;
  Int64 T = 0;
};
int certify(const string& path, const Trust& input_trust, const string& inject, Uint64 seed,
            bool bound_only, bool no_hints, Int64 int_max_dim, Int64 force_primes, slong prec0);
}  // namespace sic_check::crt
