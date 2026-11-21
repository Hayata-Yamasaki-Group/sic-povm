// SPDX-License-Identifier: AGPL-3.0-or-later
#include "units.hpp"

namespace sic_check::verifier {
Uint64 unit_power(Uint64 a, Uint64 n, Uint64 modulus) {
  Uint64 r = 1;
  for (; n; n >>= 1, a = a * a % modulus)
    if (n & 1) r = r * a % modulus;
  return r;
}

vector<Uint64> divisor_positions(Uint64 d) {
  vector<Uint64> r{0};
  for (Uint64 g = 1; g < d; ++g)
    if (d % g == 0) r.push_back(g);
  return r;
}

vector<UnitGenerator> unit_generators(Uint64 d) {
  vector<UnitGenerator> generators;
  Uint64 remaining = d;
  for (Uint64 p = 2; p <= remaining; ++p) {
    if (remaining % p) continue;
    Uint64 q = 1;
    do {
      q *= p;
      remaining /= p;
    } while (remaining % p == 0);
    auto add = [&](Uint64 a, Uint64 order) {
      Uint64 lift = a;
      while (lift % (d / q) != 1 % (d / q)) lift += q;
      UnitGenerator gen{q, a, order, lift, vector<Uint64>(q, d)};
      Uint64 v = 1;
      for (Uint64 k = 0; k < order; ++k, v = v * a % q) gen.logarithm[v] = k;
      generators.push_back(std::move(gen));
    };
    if (p == 2) {
      if (q >= 4) add(q - 1, 2);
      if (q >= 8) add(5, q / 4);
    } else {
      Uint64 phi = (p - 1) * (q / p), rest = phi;
      vector<Uint64> factors;
      for (Uint64 r = 2; r <= rest; ++r)
        if (rest % r == 0) {
          factors.push_back(r);
          do {
            rest /= r;
          } while (rest % r == 0);
        }
      Uint64 a = 2;
      for (;; ++a) {
        if (std::gcd(a, q) != 1) continue;
        bool primitive = true;
        for (Uint64 r : factors)
          if (unit_power(a, phi / r, q) == 1) primitive = false;
        if (primitive) break;
      }
      add(a, phi);
    }
  }
  return generators;
}

vector<Uint64> unit_exponents(Uint64 u, const vector<UnitGenerator>& generators) {
  vector<Uint64> exponents;
  for (const auto& gen : generators) {
    Uint64 v = u % gen.modulus;
    if (gen.modulus % 2 == 0) {
      bool negative = v % 4 == 3;
      if (gen.local == gen.modulus - 1) {
        exponents.push_back(negative);
        continue;
      }
      if (negative) v = gen.modulus - v;
    }
    exponents.push_back(gen.logarithm[v]);
  }
  return exponents;
}
}  // namespace sic_check::verifier
