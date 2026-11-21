// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/analytic/cyclotomic_trace.hpp"

#include "sic/error.hpp"

namespace sic::exact {
std::vector<long> norm_image(const Group& group, std::span<const long> generators, long modulus) {
  if (modulus < 2 || modulus > 2 * group.d || (2 * group.d) % modulus)
    throw std::invalid_argument("invalid cyclotomic norm modulus");
  std::vector<char> seen(modulus, 0);
  std::vector<long> image{1};
  seen[1] = 1;
  for (size_t q = 0; q < image.size(); ++q)
    for (long generator : generators) {
      const long norm = group.nn.at(generator) % modulus;
      if (std::gcd(norm, modulus) != 1) throw std::logic_error("nonunit cyclotomic norm");
      const long next = image[q] * norm % modulus;
      if (!seen[next]) {
        seen[next] = 1;
        image.push_back(next);
      }
    }
  std::sort(image.begin(), image.end());
  return image;
}

sic::support::RealBallVector cyclotomic_generator_values(
    const Group& group, long power, std::span<const long> kernel_generators, long subgroup_size,
    long tau, long cc, const sic::support::ComplexBallVector& roots, slong precision) {
  if (power <= 0 || power >= 2 * group.d || roots.size() != (size_t)(2 * group.d))
    throw std::invalid_argument("invalid cyclotomic trace roots");
  const long modulus = 2 * group.d / std::gcd(power, 2 * group.d);
  const auto image = norm_image(group, kernel_generators, modulus);
  const auto reachable = norm_image(group, group.stride, modulus);
  if (subgroup_size <= 0 || subgroup_size % (long)image.size())
    throw sic::Error(sic::ErrorKind::construction,
                     "cyclotomic norm image does not divide subgroup size");
  const long tau_norm = group.nn.at(tau) % modulus;
  if (!std::binary_search(image.begin(), image.end(), group.nn.at(cc) % modulus) ||
      !std::binary_search(image.begin(), image.end(), tau_norm * tau_norm % modulus))
    throw sic::Error(sic::ErrorKind::construction,
                     "cyclotomic trace lacks conjugation or quadratic norm closure");
  sic::support::RealBallVector traces(modulus), values(modulus);
  std::vector<char> complete(modulus, 0);
  sic::support::ComplexBallVector sum(1);
  for (long n : reachable)
    if (!complete[n]) {
      acb_zero(sum[0]);
      for (long u : image)
        acb_add(sum[0], sum[0], roots[power * (n * u % modulus) % (2 * group.d)], precision);
      acb_mul_si(sum[0], sum[0], subgroup_size / (long)image.size(), precision);
      if (!acb_is_finite(sum[0]) || !arb_contains_zero(acb_imagref(sum[0])))
        throw sic::Error(sic::ErrorKind::precision, "nonfinite or nonreal cyclotomic trace");
      for (long u : image) {
        const long target = n * u % modulus;
        arb_set(traces[target], acb_realref(sum[0]));
        complete[target] = 1;
      }
    }
  if (std::binary_search(image.begin(), image.end(), tau_norm)) return values;
  for (long n : reachable) {
    const long other = n * tau_norm % modulus;
    if (!complete[other]) throw std::logic_error("cyclotomic trace norm is unreachable");
    arb_sub(values[n], traces[n], traces[other], precision);
  }
  return values;
}

}  // namespace sic::exact
