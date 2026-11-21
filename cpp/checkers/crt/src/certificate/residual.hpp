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

#include "../algebra/ntt.hpp"
#include "../format/sicx_input.hpp"
namespace sic_check::crt {
using namespace sic_check::crt::sicx;

// Fourier-domain residual rows. For 0 <= a <= d/2 and l in Z_d, with U_a(l) = sum_j V_j W_{j+a}
// zeta^{jl}:
//   F[a d + l] = sum_k R_{a,k} zeta^{kl} = (d+1) U_a(-l) zeta^{-al} U_{-a}(l) - N^2 (1 + d [a =
//   0][l = 0]),
// where R_{a,k} = (d+1) sum_j V_j V_{j+a+k} W_{j+a} W_{j+k} - N^2 (delta_{a,0} + delta_{k,0}), N =
// sum_j V_j W_j. (Derivation: S_{a,k} = sum_j P_j Q_{j+k} with P_j = V_j W_{j+a}, Q_j = V_{j+a} W_j
// = P^{(-a)}_{j+a}.) U is scratch of size d^2; rows_only stops at the first nonzero entry.
bool residual_fourier_impl(const NTT& T, const vector<Uint64>& V, const vector<Uint64>& W,
                           vector<Uint64>& U, vector<Uint64>* F, long& bad_a, long& bad_l);

void residual_fourier(const NTT& T, const vector<Uint64>& V, const vector<Uint64>& W,
                      vector<Uint64>& U, vector<Uint64>& F);

bool residual_rows_vanish(const NTT& T, const vector<Uint64>& V, const vector<Uint64>& W,
                          vector<Uint64>& U, long& bad_a, long& bad_l);
}  // namespace sic_check::crt
