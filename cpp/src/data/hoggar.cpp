// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/data/hoggar.hpp"

#include "sic/support/parse_number.hpp"

namespace sic::data::hoggar {
namespace detail {
using Big = std::vector<std::uint32_t>;
constexpr std::uint32_t kBase = 1000000000u;
void trim(Big& a) {
  while (!a.empty() && a.back() == 0) a.pop_back();
}
Big big_from_dec(const std::string& s) {
  Big a;
  for (std::size_t end = s.size(); end > 0;) {
    const std::size_t beg = end >= 9 ? end - 9 : 0;
    a.push_back(support::parse_number<std::uint32_t>(s.substr(beg, end - beg), "decimal limb",
                                                     ErrorKind::invalid_format, 0, kBase - 1));
    end = beg;
  }
  trim(a);
  return a;
}
Big big_mul(const Big& a, const Big& b) {
  Big c(a.size() + b.size() + 1, 0);
  for (std::size_t i = 0; i < a.size(); ++i) {
    std::uint64_t carry = 0;
    for (std::size_t j = 0; j < b.size() || carry; ++j) {
      const std::uint64_t cur =
          c[i + j] + carry + (j < b.size() ? static_cast<std::uint64_t>(a[i]) * b[j] : 0);
      c[i + j] = static_cast<std::uint32_t>(cur % kBase);
      carry = cur / kBase;
    }
  }
  trim(c);
  return c;
}
Big big_mul_small(Big a, std::uint32_t m) {
  std::uint64_t carry = 0;
  for (auto& x : a) {
    const std::uint64_t cur = static_cast<std::uint64_t>(x) * m + carry;
    x = static_cast<std::uint32_t>(cur % kBase);
    carry = cur / kBase;
  }
  while (carry) {
    a.push_back(static_cast<std::uint32_t>(carry % kBase));
    carry /= kBase;
  }
  return a;
}
bool big_le(const Big& a, const Big& b) {  // a <= b
  if (a.size() != b.size()) return a.size() < b.size();
  for (std::size_t i = a.size(); i-- > 0;)
    if (a[i] != b[i]) return a[i] < b[i];
  return true;
}
}  // namespace detail
Report verify_exact(const Vec8& v) {
  Report r;
  const std::int64_t n = inner(v, v).re;
  r.norm_squared = n;
  if (inner(v, v).im != 0 || n <= 0) {
    r.message = "<v|v> is not a positive integer";
    return r;
  }
  for (unsigned k = 1; k < 64; ++k) {
    const Gauss g = inner(v, pauli_apply(k >> 3, k & 7, v));
    ++r.overlaps_checked;
    if (9 * norm2(g) != n * n) {
      r.message = "overlap with P_{a,b}, (a,b) = (" + std::to_string(k >> 3) + "," +
                  std::to_string(k & 7) + ") is not of modulus^2 1/9";
      return r;
    }
  }
  std::vector<Vec8> w(64);
  for (unsigned k = 0; k < 64; ++k) w[k] = pauli_apply(k >> 3, k & 7, v);
  for (unsigned k = 0; k < 64; ++k)
    for (unsigned l = k + 1; l < 64; ++l) {
      ++r.pairs_checked;
      if (9 * norm2(inner(w[k], w[l])) != n * n) {
        r.message = "lines " + std::to_string(k) + " and " + std::to_string(l) +
                    " are not equiangular with 1/9";
        return r;
      }
    }
  r.ok = (n == kNormSquared);
  return r;
}
}  // namespace sic::data::hoggar

namespace sic::data::hoggar {
bool hp_contains_sqrt_ratio(const std::string& mid, const std::string& rad, std::uint32_t num,
                            std::uint32_t den) {
  using namespace detail;
  const auto digits = [](const std::string& s) { return s.substr(s.find('.') + 1); };
  const std::string m = digits(mid), r = digits(rad);
  if (m.size() != r.size() || m.empty()) return false;
  const std::size_t P = m.size();
  const Big M = big_from_dec(m), R = big_from_dec(r);
  Big hi(M);  // M + R
  Big lo;     // max(M - R, 0)
  {
    std::uint64_t carry = 0;
    hi.resize(std::max(M.size(), R.size()) + 1, 0);
    for (std::size_t i = 0; i < hi.size(); ++i) {
      const std::uint64_t cur =
          static_cast<std::uint64_t>(hi[i]) + (i < R.size() ? R[i] : 0) + carry;
      hi[i] = static_cast<std::uint32_t>(cur % kBase);
      carry = cur / kBase;
    }
    trim(hi);
    if (big_le(R, M)) {
      lo = M;
      std::int64_t borrow = 0;
      for (std::size_t i = 0; i < lo.size(); ++i) {
        std::int64_t cur = static_cast<std::int64_t>(lo[i]) - (i < R.size() ? R[i] : 0) - borrow;
        borrow = cur < 0;
        if (cur < 0) cur += kBase;
        lo[i] = static_cast<std::uint32_t>(cur);
      }
      trim(lo);
    }
  }
  const Big target = big_mul_small(big_from_dec("1" + std::string(2 * P, '0')), num);
  const auto lhs = [&](const Big& x) { return big_mul_small(big_mul(x, x), den); };
  return big_le(lhs(lo), target) && big_le(target, lhs(hi));
}
}  // namespace sic::data::hoggar
