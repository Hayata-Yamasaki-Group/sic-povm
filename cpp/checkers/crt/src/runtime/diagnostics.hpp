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

#include "../model/input_model.hpp"
namespace sic_check::crt::sicx {

[[noreturn]] void resource(const string& m);

[[noreturn]] void reject(const string& m);
[[noreturn]] void refuse(const string& why);

double now();

double rss_gb();

[[noreturn]] void refuse(const string& why);

void say(const char* f, ...) __attribute__((format(printf, 1, 2)));

void say(const char* f, ...);

extern std::atomic<Uint64> g_reach[64];
}  // namespace sic_check::crt::sicx
