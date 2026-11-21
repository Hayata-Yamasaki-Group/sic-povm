// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntt.hpp"

#include "../format/canonical_json.hpp"
#include "../format/container.hpp"
#include "../format/integer_codec.hpp"
#include "../runtime/diagnostics.hpp"
namespace sic_check::crt {
using namespace sic_check::crt::sicx;
auto NTT::init(Uint64 p, Uint64 nn) -> void {
  nmod_init(&mod, p);
  n = nn;
  lg = 0;
  while ((1ull << lg) < n) ++lg;
  if ((p - 1) % n) refuse("NTT: p != 1 mod d");
  Uint64 z = 0;
  for (Uint64 x = 2;; ++x) {
    z = n_powmod2_ui_preinv(x, (p - 1) / n, p, mod.ninv);
    if (n == 1 || n_powmod2_ui_preinv(z, n / 2, p, mod.ninv) != 1) break;
  }
  w.assign(n, 1);
  wi.assign(n, 1);
  const Uint64 zi = n_invmod(z, p);
  for (Uint64 i = 1; i < n; ++i) {
    w[i] = nmod_mul(w[i - 1], z, mod);
    wi[i] = nmod_mul(wi[i - 1], zi, mod);
  }
  ws.resize(n);
  wis.resize(n);
  for (Uint64 i = 0; i < n; ++i) {
    ws[i] = n_mulmod_precomp_shoup(w[i], p);
    wis[i] = n_mulmod_precomp_shoup(wi[i], p);
  }
  rev.assign(n, 0);
  for (Uint64 i = 1, j = 0; i < n; ++i) {
    Uint64 bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    rev[i] = (uint32_t)j;
  }
}

auto NTT::run(Uint64* a, bool inverse) const -> void {  // a_k <- sum_j a_j zeta^{+-jk}
  for (Uint64 i = 1; i < n; ++i)
    if (i < rev[i]) std::swap(a[i], a[rev[i]]);
  const vector<Uint64>& W = inverse ? wi : w;
  const vector<Uint64>& Wp = inverse ? wis : ws;
  const Uint64 p = mod.n;
  for (Uint64 len = 2; len <= n; len <<= 1) {
    const Uint64 step = n / len;
    for (Uint64 i = 0; i < n; i += len)
      for (Uint64 k = 0; k < len / 2; ++k) {
        const Uint64 u = a[i + k],
                     v = n_mulmod_shoup(W[k * step], a[i + k + len / 2], Wp[k * step], p);
        a[i + k] = nmod_add(u, v, mod);
        a[i + k + len / 2] = nmod_sub(u, v, mod);
      }
  }
}
}  // namespace sic_check::crt
