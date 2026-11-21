// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/model/unit_group.hpp"

namespace sic::sicx {
int64_t squarefree_part(int64_t n) {
  if (n == 0) return 0;
  int64_t sign = n < 0 ? -1 : 1, m = n < 0 ? -n : n, result = 1;
  for (int64_t p = 2; p * p <= m; ++p) {
    int e = 0;
    while (m % p == 0) {
      m /= p;
      ++e;
    }
    if (e & 1) result *= p;
  }
  return sign * result * m;
}
}  // namespace sic::sicx

namespace sic::sicx {
int64_t field_radicand(uint64_t d) {
  if (d < 2 || d > kMaximumDimension)
    fail(sic::ErrorKind::invalid_argument, "dimension must be in [2, 2^20]");
  if (d == 2) return -3;
  if (d == 3) return 1;
  const int64_t D = (static_cast<int64_t>(d) + 1) * (static_cast<int64_t>(d) - 3);
  return squarefree_part(D);
}
}  // namespace sic::sicx

namespace sic::sicx {
std::vector<uint64_t> UnitGroup::prime_divisors(uint64_t n) {
  std::vector<uint64_t> result;
  for (uint64_t p = 2; p * p <= n; ++p) {
    if (n % p) continue;
    result.push_back(p);
    do {
      n /= p;
    } while (n % p == 0);
  }
  if (n > 1) result.push_back(n);
  return result;
}
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t UnitGroup::power(uint64_t a, uint64_t e, uint64_t n) {
  uint64_t r = 1;
  while (e) {
    if (e & 1) r = r * a % n;
    a = a * a % n;
    e >>= 1;
  }
  return r;
}
}  // namespace sic::sicx

namespace sic::sicx {
UnitGroup::UnitGroup(uint64_t dimension) : d(dimension) {
  if (d < 2 || d > kMaximumDimension)
    fail(sic::ErrorKind::invalid_argument, "dimension must be in [2, 2^20]");
  for (const auto p : prime_divisors(d)) {
    uint64_t q = p;
    while (d / q % p == 0) q *= p;
    const uint64_t phi = q / p * (p - 1), other = d / q;
    auto add = [&](uint64_t local, uint64_t order) {
      // other is invertible modulo q; Euler's theorem supplies the inverse.
      const auto inverse = power(other % q, phi - 1, q);
      generators.push_back(1 + other * ((local - 1) * inverse % q));
      orders.push_back(order);
    };
    if (p == 2) {
      if (q >= 4) add(q - 1, 2);
      if (q >= 8) add(5, q / 4);
    } else {
      const auto factors = prime_divisors(phi);
      for (uint64_t a = 2; a < q; ++a) {
        if (a % p == 0) continue;
        if (std::all_of(factors.begin(), factors.end(),
                        [&](auto r) { return power(a, phi / r, q) != 1; })) {
          add(a, phi);
          break;
        }
      }
    }
  }
  std::vector<uint64_t> units{1};
  for (size_t j = 0; j < orders.size(); ++j) {
    const size_t prefix = units.size();
    units.resize(prefix * orders[j]);
    uint64_t power_j = 1;
    for (uint64_t a = 1; a < orders[j]; ++a) {
      power_j = power_j * generators[j] % d;
      for (size_t i = 0; i < prefix; ++i) units[a * prefix + i] = units[i] * power_j % d;
    }
  }
  logarithm.assign(d, kNonunit);
  for (size_t i = 0; i < units.size(); ++i) {
    if (logarithm[units[i]] != kNonunit)
      throw std::logic_error(std::string("SICX: ") + "derived unit generators are dependent");
    logarithm[units[i]] = static_cast<uint32_t>(i);
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
std::vector<uint64_t> divisor_positions(uint64_t d) {
  if (d < 2 || d > kMaximumDimension)
    fail(sic::ErrorKind::invalid_argument, "dimension must be in [2, 2^20]");
  std::vector<uint64_t> positions{0};
  for (uint64_t g = 1; g * g <= d; ++g) {
    if (d % g) continue;
    if (g < d) positions.push_back(g);
    if (d / g != g && d / g < d) positions.push_back(d / g);
  }
  std::sort(positions.begin(), positions.end());
  return positions;
}
}  // namespace sic::sicx

namespace sic::sicx {
SubLayout::SubLayout(const std::vector<unsigned>& degrees, const std::vector<uint64_t>& tower_width,
                     std::vector<unsigned> J)
    : levels(std::move(J)) {
  for (size_t j = 0; j < levels.size(); ++j) {
    if (levels[j] >= degrees.size() || (j && levels[j] <= levels[j - 1]))
      fail(sic::ErrorKind::invalid_format, "invalid element level set");
    sub_width.push_back(sub_width.back() * degrees[levels[j]]);
    full_width.push_back(tower_width[levels[j]]);
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t SubLayout::position(uint64_t s) const {
  uint64_t pos = 0;
  for (size_t j = 0; j < levels.size(); ++j) {
    const uint64_t p = sub_width[j + 1] / sub_width[j];
    pos += (s % p) * full_width[j];
    s /= p;
  }
  return pos;
}
}  // namespace sic::sicx
