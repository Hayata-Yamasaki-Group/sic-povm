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

// ================================================================================================
// NTT
struct NTT {
  nmod_t mod{};
  Uint64 n = 0;
  int lg = 0;
  vector<Uint64> w, wi, ws, wis;
  vector<uint32_t> rev;

  void init(Uint64 p, Uint64 nn);

  void run(Uint64* a, bool inverse) const;
};
}  // namespace sic_check::crt
