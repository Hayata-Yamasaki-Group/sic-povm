// SPDX-License-Identifier: AGPL-3.0-or-later
#include "residual.hpp"

#include "../format/canonical_json.hpp"
#include "../format/container.hpp"
#include "../format/integer_codec.hpp"
#include "../runtime/diagnostics.hpp"
namespace sic_check::crt {
using namespace sic_check::crt::sicx;
bool residual_fourier_impl(const NTT& T, const vector<Uint64>& V, const vector<Uint64>& W,
                           vector<Uint64>& U, vector<Uint64>* F, long& bad_a, long& bad_l) {
  const Uint64 d = T.n, msk = d - 1;
  const nmod_t& mod = T.mod;
  for (Uint64 a = 0; a < d; ++a) {
    Uint64* u = &U[a * d];
    for (Uint64 j = 0; j < d; ++j) u[j] = nmod_mul(V[j], W[(j + a) & msk], mod);
    T.run(u, false);
  }
  Uint64 Nn = 0;
  for (Uint64 j = 0; j < d; ++j) Nn = nmod_add(Nn, nmod_mul(V[j], W[j], mod), mod);
  const Uint64 N2 = nmod_mul(Nn, Nn, mod), dp1 = (d + 1) % mod.n,
               N2d1 = nmod_mul(N2, (d + 1) % mod.n, mod);
  if (F) F->assign((d / 2 + 1) * d, 0);
  for (Uint64 a = 0; a <= d / 2; ++a) {
    const Uint64* ua = &U[a * d];
    const Uint64* um = &U[((d - a) & msk) * d];
    for (Uint64 l = 0; l < d; ++l) {
      Uint64 x = nmod_mul(nmod_mul(ua[(d - l) & msk], T.wi[(a * l) & msk], mod), um[l], mod);
      x = nmod_sub(nmod_mul(dp1, x, mod), (a == 0 && l == 0) ? N2d1 : N2, mod);
      if (F)
        (*F)[a * d + l] = x;
      else if (x) {
        bad_a = (long)a;
        bad_l = (long)l;
        return false;
      }
    }
  }
  return true;
}

void residual_fourier(const NTT& T, const vector<Uint64>& V, const vector<Uint64>& W,
                      vector<Uint64>& U, vector<Uint64>& F) {
  long a, l;
  residual_fourier_impl(T, V, W, U, &F, a, l);
}

bool residual_rows_vanish(const NTT& T, const vector<Uint64>& V, const vector<Uint64>& W,
                          vector<Uint64>& U, long& bad_a, long& bad_l) {
  return residual_fourier_impl(T, V, W, U, nullptr, bad_a, bad_l);
}
}  // namespace sic_check::crt
