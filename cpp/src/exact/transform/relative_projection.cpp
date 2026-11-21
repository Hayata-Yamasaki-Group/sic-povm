// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/transform/relative_projection.hpp"

namespace sic::exact {
long smith_character_exponent(const Group& group, long character, long element, long modulus) {
  long exponent = 0;
  for (size_t l = 0; l < group.invf.size(); ++l)
    exponent += group.digit(character, l) * group.digit(element, l) * (modulus / group.invf[l]);
  return exponent % modulus;
}

std::vector<long> quadratic_annihilator_generators(const Group& group,
                                                   std::span<const long> characters) {
  long modulus = 1;
  for (long factor : group.invf) modulus = std::lcm(modulus, factor);
  std::vector<long> generators = group.stride;
  for (long character : characters) {
    long pivot = -1;
    std::vector<long> signs;
    for (long generator : generators) {
      const long exponent = smith_character_exponent(group, character, generator, modulus);
      if (exponent != 0 && exponent != modulus / 2)
        throw std::logic_error("character is not quadratic on the current annihilator");
      signs.push_back(exponent != 0);
      if (exponent) pivot = generator;
    }
    if (pivot < 0) throw std::logic_error("redundant quadratic character extension");
    std::vector<long> kernel{group.mul(pivot, pivot)};
    const long inverse = group.pow(pivot, -1);
    for (size_t q = 0; q < generators.size(); ++q)
      kernel.push_back(signs[q] ? group.mul(generators[q], inverse) : generators[q]);
    std::sort(kernel.begin(), kernel.end());
    kernel.erase(std::unique(kernel.begin(), kernel.end()), kernel.end());
    if (!kernel.empty() && kernel[0] == 0) kernel.erase(kernel.begin());
    generators = std::move(kernel);
  }
  return generators;
}

std::vector<long> enumerate_exact_subgroup(const Group& group, std::span<const long> generators,
                                           size_t limit) {
  if (!limit) throw std::invalid_argument("empty subgroup limit");
  std::unordered_set<long> seen{0};
  std::vector<long> members{0};
  for (size_t q = 0; q < members.size(); ++q)
    for (long generator : generators) {
      const long next = group.mul(members[q], generator);
      if (seen.insert(next).second) {
        if (members.size() == limit)
          throw std::runtime_error("relative subgroup exceeds its exact bound");
        members.push_back(next);
      }
    }
  std::sort(members.begin(), members.end());
  return members;
}

}  // namespace sic::exact
