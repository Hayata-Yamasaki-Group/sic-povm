// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/transform/relative_projection.hpp"

#include "assertions.hpp"
#include "support/resources.hpp"

int main() {
  sic::exact::Arithmetic arithmetic(3);
  sic::exact::Group group(arithmetic);
  group.generate_standard();
  group.close(
      sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")));
  long modulus = 1;
  for (long factor : group.invf) modulus = std::lcm(modulus, factor);
  std::vector<long> chain;
  for (size_t l = 0; l < group.invf.size(); ++l)
    for (long denominator = 2; denominator <= group.invf[l]; denominator *= 2)
      chain.push_back(group.stride[l] * (group.invf[l] / denominator));
  for (size_t count = 0; count < chain.size(); ++count) {
    auto characters = std::span<const long>(chain.data(), count);
    const auto generators = sic::exact::quadratic_annihilator_generators(group, characters);
    const auto members = sic::exact::enumerate_exact_subgroup(group, generators, group.N >> count);
    std::vector<long> reference;
    for (long g = 0; g < group.N; ++g)
      if (std::all_of(characters.begin(), characters.end(), [&](long c) {
            return sic::exact::smith_character_exponent(group, c, g, modulus) == 0;
          }))
        reference.push_back(g);
    SIC_CHECK(members == reference);
    const auto next_generators = sic::exact::quadratic_annihilator_generators(
        group, std::span<const long>(chain.data(), count + 1));
    const auto next =
        sic::exact::enumerate_exact_subgroup(group, next_generators, members.size() / 2);
    long tau = -1;
    for (long g : members)
      if (!std::binary_search(next.begin(), next.end(), g)) {
        tau = g;
        break;
      }
    SIC_CHECK(tau >= 0 && std::binary_search(next.begin(), next.end(), group.mul(tau, tau)));
    sic::support::ComplexBallVector result(3), sum(1);
    auto value = [&](long g, acb_ptr out) { acb_set_si(out, (g % 11 - 5) * (g % 7 - 3)); };
    bool nonzero = false;
    for (long g = 0; g < group.N; ++g) {
      sic::exact::relative_projection_at(result[0], group, next, tau, g, 96, value);
      sic::exact::relative_projection_at(result[1], group, next, tau, group.mul(g, tau), 96, value);
      acb_neg(result[1], result[1]);
      SIC_CHECK(acb_equal(result[0], result[1]));
      acb_zero(result[2]);
      for (long h : next) {
        value(group.mul(g, h), sum[0]);
        acb_add(result[2], result[2], sum[0], 96);
        value(group.mul(group.mul(g, tau), h), sum[0]);
        acb_sub(result[2], result[2], sum[0], 96);
      }
      SIC_CHECK(acb_equal(result[0], result[2]));
      nonzero |= !acb_contains_zero(result[0]);
      sic::exact::relative_projection_at(result[1], group, next, tau, g, 192, value);
      SIC_CHECK(acb_equal(result[0], result[1]));
    }
    SIC_CHECK(nonzero);
    sic::exact::relative_projection_at(result[0], group, next, tau, 0, 96,
                                       [](long, acb_ptr out) { acb_zero(out); });
    SIC_CHECK(acb_is_zero(result[0]));
  }
  return sic_test::report();
}
