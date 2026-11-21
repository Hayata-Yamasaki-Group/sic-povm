// SPDX-License-Identifier: AGPL-3.0-or-later
#include <stdexcept>

#include "sic/core/clifford.hpp"
#include "sic/error.hpp"

int main() {
  using namespace sic::core::clifford;
  for (const auto [d, sign] :
       {std::pair<Int, Int>{0, 1}, {-4, 1}, {(Int{1} << 30) + 1, 1}, {2, 5}, {2, 0}}) {
    try {
      (void)canonical_lift(Mat2{}, d, sign);
    } catch (const sic::Error& err) {
      if (err.kind() == sic::ErrorKind::invalid_argument) continue;
      throw;
    }
    throw std::runtime_error("invalid dimension or sign accepted");
  }
  for (Int d : {2, 3, 4})
    for (Int sign : {-1, 1})
      for (Int a = 0; a < d; ++a)
        for (Int b = 0; b < d; ++b)
          for (Int c = 0; c < d; ++c)
            for (Int e = 0; e < d; ++e) {
              Mat2 f{a, b, c, e};
              if (((a * e - b * c - sign) % d + d) % d != 0) {
                try {
                  (void)canonical_lift(f, d, sign);
                } catch (const sic::Error& err) {
                  if (err.kind() == sic::ErrorKind::invalid_argument) continue;
                  throw;
                }
                throw std::runtime_error("determinant mismatch accepted");
              }
              bool found = false;
              Mat2 expected;
              const Int m = d % 2 == 0 ? 2 * d : d;
              for (Int x = 0; x < m && !found; ++x)
                for (Int y = 0; y < m && !found; ++y)
                  for (Int z = 0; z < m && !found; ++z)
                    for (Int w = 0; w < m && !found; ++w)
                      if (x % d == a && y % d == b && z % d == c && w % d == e &&
                          ((x * w - y * z - sign) % m + m) % m == 0) {
                        expected = {x, y, z, w};
                        found = true;
                      }
              if (!found || canonical_lift(f, d, sign) != expected ||
                  canonical_lift({a - d, b + d, c - d, e + d}, d, sign) != expected)
                throw std::runtime_error("smallest lift mismatch");
            }
}
