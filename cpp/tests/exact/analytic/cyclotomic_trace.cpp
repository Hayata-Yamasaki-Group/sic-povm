// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/analytic/cyclotomic_trace.hpp"

#include "assertions.hpp"
#include "exact/transform/relative_projection.hpp"
#include "support/resources.hpp"

int main() {
  for (int exponent : {3, 4}) {
    sic::exact::Arithmetic arithmetic(exponent);
    sic::exact::Group group(arithmetic);
    group.generate_standard();
    group.close(
        sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")));
    std::vector<long> chain;
    for (size_t l = 0; l < group.invf.size(); ++l)
      for (long denominator = 2; denominator <= group.invf[l]; denominator *= 2)
        chain.push_back(group.stride[l] * (group.invf[l] / denominator));
    sic::support::ComplexBallVector roots(2 * group.d), direct(2), term(1);
    acb_unit_root(term[0], 2 * group.d, 192);
    for (long k = 0; k < 2 * group.d; ++k) acb_pow_ui(roots[k], term[0], k, 192);
    long conjugation = -1;
    for (long g = 0; g < group.N; ++g)
      if (group.nn[g] == 2 * group.d - 1) {
        conjugation = g;
        break;
      }
    SIC_CHECK(conjugation >= 0);
    int tested = 0;
    for (size_t count = 0; count < chain.size(); ++count) {
      const auto before_gens = sic::exact::quadratic_annihilator_generators(
          group, std::span<const long>(chain.data(), count));
      const auto after_gens = sic::exact::quadratic_annihilator_generators(
          group, std::span<const long>(chain.data(), count + 1));
      const auto before =
          sic::exact::enumerate_exact_subgroup(group, before_gens, group.N >> count);
      const auto after = sic::exact::enumerate_exact_subgroup(group, after_gens, before.size() / 2);
      long tau = -1;
      for (long g : before)
        if (!std::binary_search(after.begin(), after.end(), g)) {
          tau = g;
          break;
        }
      for (long power = 1; power < 2 * group.d; power *= 2) {
        const long modulus = 2 * group.d / std::gcd(power, 2 * group.d);
        const auto image = sic::exact::norm_image(group, after_gens, modulus);
        if (!std::binary_search(image.begin(), image.end(), (2 * group.d - 1) % modulus)) continue;
        const auto values = sic::exact::cyclotomic_generator_values(
            group, power, after_gens, after.size(), tau, conjugation, roots, 160);
        for (long g = 0; g < group.N; ++g) {
          acb_zero(direct[0]);
          acb_zero(direct[1]);
          for (long h : after) {
            const long first = group.mul(g, h), second = group.mul(first, tau);
            acb_add(direct[0], direct[0], roots[power * group.nn[first] % (2 * group.d)], 160);
            acb_add(direct[1], direct[1], roots[power * group.nn[second] % (2 * group.d)], 160);
          }
          acb_sub(direct[0], direct[0], direct[1], 160);
          SIC_CHECK(arb_contains_zero(acb_imagref(direct[0])));
          SIC_CHECK(arb_overlaps(values[group.nn[g] % modulus], acb_realref(direct[0])));
        }
        ++tested;
      }
    }
    SIC_CHECK(tested > 0);
  }
  return sic_test::report();
}
