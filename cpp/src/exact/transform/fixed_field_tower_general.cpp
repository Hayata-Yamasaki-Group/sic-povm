// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/transform/fixed_field_tower_general.hpp"

#include "sic/error.hpp"

namespace sic::exact {
ScalarFixedFieldsGeneral::ScalarFixedFieldsGeneral(const Group& group, const std::vector<long>& sig,
                                                   long cc) {
  const long d = group.ar.d, N = group.N;
  odd = N;
  while (odd % 2 == 0) odd /= 2;
  if (cc <= 0 || group.mul(cc, cc) != 0 || N % 2)
    throw sic::Error(sic::ErrorKind::construction,
                     "adapted tower needs an even group order and a conjugation involution");
  std::map<long, ScalarFixedField> unique;
  std::vector<long> rep_degree(group.ar.s + 1);
  for (int r = 0; r <= group.ar.s; ++r) {
    const long m = r == 0 ? 0 : 1L << (r - 1);
    const long M = d / std::gcd(d, m);
    ScalarFixedField field;
    if (M <= 2)
      field.generators = {sig[5 % (2 * d)], sig[2 * d - 1]};
    else
      field.generators = {group.pow(sig[5], M / 4)};
    std::unordered_set<long> seen{0};
    field.members = {0};
    for (size_t q = 0; q < field.members.size(); ++q)
      for (long generator : field.generators) {
        const long next = group.mul(field.members[q], generator);
        if (seen.insert(next).second) field.members.push_back(next);
        if (field.members.size() > (size_t)(2 * d))
          throw sic::Error(sic::ErrorKind::construction,
                           "scalar stabilizer exceeds its unit-group bound");
      }
    std::sort(field.members.begin(), field.members.end());
    if (std::binary_search(field.members.begin(), field.members.end(), cc) ||
        N % (2 * field.members.size()) != 0)
      throw sic::Error(sic::ErrorKind::construction,
                       "scalar stabilizer has invalid real fixed-field degree");
    for (long h : field.members)
      if (eighth_character(group.nn[h]) != 1)
        throw sic::Error(sic::ErrorKind::construction,
                         "scalar stabilizer does not fix the symbolic top generator");
    field.degree = N / (2 * field.members.size());
    if (field.degree % odd || !std::has_single_bit((unsigned long)(field.degree / odd)))
      throw sic::Error(
          sic::ErrorKind::construction,
          "scalar real fixed-field degree is not odd * 2^k (stabilizer is not a 2-group?)");
    rep_degree[r] = field.degree;
    auto [it, inserted] = unique.emplace(field.degree, field);
    if (!inserted && it->second.members != field.members)
      throw sic::Error(sic::ErrorKind::construction,
                       "distinct scalar stabilizers have the same fixed-field degree");
  }
  unique.emplace(N / 2, ScalarFixedField{{}, {0}, N / 2, -1});
  for (auto& [degree, field] : unique) {
    if (!fields.empty() &&
        !std::includes(fields.back().members.begin(), fields.back().members.end(),
                       field.members.begin(), field.members.end()))
      throw sic::Error(sic::ErrorKind::construction,
                       "scalar fixed fields do not form a nested chain");
    fields.push_back(std::move(field));
  }
  for (long degree : rep_degree) {
    size_t q = 0;
    while (q < fields.size() && fields[q].degree != degree) ++q;
    if (q == fields.size()) throw std::logic_error("missing scalar fixed field");
    representative_field.push_back(q);
  }
}

std::vector<long> prime_annihilator_generators(const Group& group, std::span<const long> characters,
                                               std::span<const long> primes) {
  if (characters.size() != primes.size())
    throw std::logic_error("annihilator: one prime per character");
  long modulus = 1;
  for (long factor : group.invf) modulus = std::lcm(modulus, factor);
  std::vector<long> generators = group.stride;
  for (size_t c = 0; c < characters.size(); ++c) {
    const long p = primes[c];
    if (modulus % p)
      throw std::logic_error("annihilator: step degree does not divide the exponent");
    const long unit = modulus / p;
    long pivot = -1, pivot_k = 0;
    std::vector<long> ks;
    for (long generator : generators) {
      const long exponent = smith_character_exponent(group, characters[c], generator, modulus);
      if (exponent % unit)
        throw std::logic_error("character does not have prime order p on the current annihilator");
      const long k = (exponent / unit) % p;
      ks.push_back(k);
      if (k && pivot < 0) {
        pivot = generator;
        pivot_k = k;
      }
    }
    if (pivot < 0) throw std::logic_error("redundant character extension");
    long inverse_k = 1;
    while ((inverse_k * pivot_k) % p != 1) ++inverse_k;
    std::vector<long> kernel{group.pow(pivot, p)};
    for (size_t q = 0; q < generators.size(); ++q) {
      const long shift = (p - (ks[q] * inverse_k) % p) % p;
      kernel.push_back(group.mul(generators[q], group.pow(pivot, shift)));
    }
    std::sort(kernel.begin(), kernel.end());
    kernel.erase(std::unique(kernel.begin(), kernel.end()), kernel.end());
    if (!kernel.empty() && kernel[0] == 0) kernel.erase(kernel.begin());
    generators = std::move(kernel);
  }
  return generators;
}

}  // namespace sic::exact
