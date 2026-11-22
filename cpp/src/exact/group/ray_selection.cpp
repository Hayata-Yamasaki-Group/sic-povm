// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/group/ray_selection.hpp"

#include "exact/arithmetic/maximal_order.hpp"
#include "exact/runtime/parallel.hpp"

namespace sic::exact {
namespace {
using sic::support::Index;

Index certified_class(const Arithmetic& ar, const Lattice& ideal) {
  const auto [target, scale] = ar.identify(ideal);
  if (!totally_positive(ar.kc, scale) ||
      !lattice_equal(ar.kc, ideal, lattice_scale(ar.kc, scale, ar.class_involution(target))))
    throw sic::Error(sic::ErrorKind::construction, "ideal class has no positive lattice witness");
  return target;
}
}  // namespace

RaySelection::RaySelection(const GroupProjection& group, int threads) : group_(group) {
  // Class labels follow the exact source identity checked by GroupCheckpoint::project.
  Arithmetic ar(group.exponent);
  const long standard_degree = reduced_order::degree(ar, 1);
  const bool standard_order = group.order_index == 1 && group.discriminant == ar.Delta &&
                              group.original_discriminant == ar.original_Delta &&
                              group.classes == ar.h();
  int unit_power = 1;
  if (group.order_index != 1 || group.elements != standard_degree)
    unit_power = reduced_order::configure_arith(ar).k;
  if (ar.Delta != group.discriminant || ar.original_Delta != group.original_discriminant ||
      ar.order_index != group.order_index || ar.h() != group.classes ||
      group.elements != reduced_order::degree(ar, unit_power)) {
    if (standard_order && group.elements > 0 && group.elements < standard_degree &&
        standard_degree % group.elements == 0)
      throw sic::Error(sic::ErrorKind::unsupported, "reduced standard-order Group is unsupported");
    throw sic::Error(sic::ErrorKind::integrity, "group checkpoint arithmetic/dimension mismatch");
  }

  // A mixed-sign principal ideal generates the kernel of forgetting positivity.
  const auto alpha = sub(QuadraticNumber(1), ar.eps);
  if (sign1(ar.kc, alpha) >= 0 || sign2(ar.kc, alpha) <= 0)
    throw sic::Error(sic::ErrorKind::construction, "expected a mixed-sign principal ideal");
  std::vector<Lattice> ideals;
  for (Index c = 0; c < ar.h(); ++c) ideals.push_back(ar.class_involution(c));
  std::vector<Index> partner(ar.h());
  parallel_for(ar.h(), threads, [&](long c) {
    partner[c] = certified_class(ar, lattice_scale(ar.kc, alpha, ideals[c]));
  });
  std::vector<Index> labels;
  ordinary_.resize(ar.h());
  for (Index c = 0; c < ar.h(); ++c) {
    if (partner[partner[c]] != c)
      throw sic::Error(sic::ErrorKind::construction, "ordinary sign pairing is not involutive");
    ordinary_[c] = std::min(c, partner[c]);
    if (ordinary_[c] == c) labels.push_back(c);
  }
  ordinary_count_ = labels.size();
  if (unit_power != 1 && ordinary_count_ != 1)
    throw sic::Error(sic::ErrorKind::unsupported,
                     "ordinary projection on the reduced field is unproved");

  translate_.assign(group.smith.size(), std::vector<Index>(ar.h(), -1));
  parallel_for(static_cast<long>(group.smith.size() * labels.size()), threads, [&](long task) {
    const size_t axis = task / labels.size();
    const Index c = labels[task % labels.size()];
    const Index a = group.axis_classes[axis];
    // Multiplication by the neutral ordinary class fixes every label.
    translate_[axis][c] =
        ordinary_[a] == 0
            ? c
            : ordinary_[certified_class(ar, lattice_mul(ar.kc, ideals[c], ideals[a]))];
  });
  for (size_t i = 0; i < translate_.size(); ++i) {
    const auto& translate = translate_[i];
    Index c = translate[0];
    long order = 1;
    while (c != 0 && order < static_cast<long>(ordinary_count_)) {
      c = translate[c];
      ++order;
    }
    if (c != 0 || group.smith[i] % order)
      throw sic::Error(sic::ErrorKind::integrity,
                       "Smith axis does not satisfy its cyclic relation");
    axis_orders_.push_back(order);
  }
}

std::vector<SmithWord> RaySelection::words_from_rows(const std::vector<long>& rows) const {
  std::vector<SmithWord> words;
  for (size_t c = 0; c < rows.size(); ++c) {
    if (ordinary_[c] != static_cast<Index>(c)) continue;
    if (rows[c] < 0)
      throw sic::Error(sic::ErrorKind::integrity, "group projection misses an ordinary class");
    SmithWord word;
    for (size_t i = 0; i < group_.smith.size(); ++i)
      word.push_back(rows[c] / group_.stride[i] % group_.smith[i]);
    words.push_back(std::move(word));
  }
  std::sort(words.begin(), words.end());
  return words;
}

std::vector<SmithWord> RaySelection::derive() const {
  std::vector<long> first(ordinary_.size(), -1);
  first[0] = 0;
  std::vector<Index> classes{0};
  classes.reserve(ordinary_count_);
  for (size_t axis = 0; axis < translate_.size(); ++axis) {
    const size_t prefix_size = classes.size();
    const auto& translate = translate_[axis];
    // Every block is a coset of the preceding subgroup. The first repeat
    // of its origin ends this axis, since all later blocks repeat cosets.
    for (Index origin = translate[0]; first[origin] < 0; origin = translate[origin]) {
      const size_t previous = classes.size() - prefix_size;
      for (size_t i = 0; i < prefix_size; ++i) {
        const Index source = classes[previous + i], target = translate[source];
        if (first[target] >= 0)
          throw sic::Error(sic::ErrorKind::integrity, "ordinary cosets overlap");
        first[target] = first[source] + group_.stride[axis];
        classes.push_back(target);
      }
    }
  }
  return words_from_rows(first);
}

void RaySelection::check(const std::vector<SmithWord>& words) const {
  if (words.size() != ordinary_count_)
    throw sic::Error(sic::ErrorKind::invalid_format, "incorrect number of ray words");
  std::vector<bool> seen(ordinary_.size());
  for (const auto& word : words) {
    if (word.size() != group_.smith.size())
      throw sic::Error(sic::ErrorKind::invalid_format,
                       "ray word rank differs from the Smith basis");
    Index c = 0;
    for (size_t i = 0; i < word.size(); ++i) {
      if (word[i] < 0 || word[i] >= group_.smith[i])
        throw sic::Error(sic::ErrorKind::invalid_format, "ray word digit out of range");
      for (long digit = word[i] % axis_orders_[i]; digit > 0; --digit) c = translate_[i][c];
    }
    if (seen[c]) throw sic::Error(sic::ErrorKind::integrity, "ray words repeat an ordinary class");
    seen[c] = true;
  }
}

}  // namespace sic::exact
