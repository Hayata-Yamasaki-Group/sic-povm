// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/group/ray_selection.hpp"

#include <algorithm>
#include <limits>
#include <chrono>

#include "exact/arithmetic/maximal_order.hpp"
#include "exact/runtime/parallel.hpp"
#include "support/sha256.hpp"

namespace sic::exact {
namespace {
using sic::support::Index;
namespace resources = sic::support::resources;
using sic::sicx::RayList;

Index certified_class(const Arithmetic& ar, const Lattice& ideal) {
  const auto [target, scale] = ar.identify(ideal);
  if (!totally_positive(ar.kc, scale) ||
      !lattice_equal(ar.kc, ideal, lattice_scale(ar.kc, scale, ar.class_involution(target))))
    throw sic::Error(sic::ErrorKind::construction, "ideal class has no positive lattice witness");
  return target;
}

RayList make_list(const GroupProjection& group, std::vector<SmithWord> words) {
  const auto source =
      group.exponent == 1 ? "closed-form" : sic::support::sha256::digest_hex(group.source);
  return {uint64_t{1} << group.exponent, source, {}, group.smith, std::move(words)};
}

RayList selected_list(const GroupProjection& group, const RaySelection& selection) {
  auto words = selection.derive();
  selection.check(words);
  return make_list(group, std::move(words));
}

long selection_tasks(uint64_t classes, size_t rank) {
  const auto count = resources::mul(classes, rank);
  if (count > static_cast<uint64_t>(std::numeric_limits<long>::max()))
    throw sic::Error(sic::ErrorKind::resource, "ray selection task count exceeds policy");
  return static_cast<long>(count);
}
}  // namespace

void sort_ray_words(std::vector<SmithWord>& words) { std::sort(words.begin(), words.end()); }

GroupProjection dimension_two_projection() {
  GroupProjection group;
  group.exponent = 1;
  group.smith = {2};
  return group;
}

int admit_ray_selection(const GroupProjection& group, int threads,
                        const sic::support::resources::Budget& budget) {
  const bool d2 = group.exponent == 1;
  const auto tasks =
      d2 ? 1 : selection_tasks(group.classes, std::max<size_t>(1, group.smith.size()));
  const int requested = static_cast<int>(std::min<long>(threads, tasks));
  // Keep the established forecast for reduced forms and exact ideal
  // witnesses. Admit at least one active worker before selecting the share.
  const auto shared =
      d2 ? 0
         : resources::add(
               resources::mul(group.classes,
                              resources::add(4096, resources::mul(32, group.smith.size()))),
               resources::mul(uint64_t{1} << group.exponent, 32768));
  constexpr auto per_worker = resources::kMib;
  budget.require_additional("ray selection", resources::add(shared, per_worker));
  const auto workers =
      budget.workers(requested, resources::add(resources::inspect().rss, shared), per_worker);
  sic::support::progressf(
      "RAY-SELECTION shared_bytes=%llu per_worker_bytes=%llu workers=%d requested=%d\n",
      static_cast<unsigned long long>(shared), static_cast<unsigned long long>(per_worker), workers,
      requested);
  return workers;
}

RayList select_rays(const GroupProjection& group, int threads,
                    const sic::support::resources::Budget& budget) {
  const int workers = admit_ray_selection(group, threads, budget);
  if (group.exponent == 1) return make_list(group, {{0}});
  return selected_list(group, RaySelection(group, workers));
}

RayList select_rays(const Group& group, int unit_power, int threads,
                    const sic::support::resources::Budget& budget) {
  const auto start = std::chrono::steady_clock::now();
  const auto projection = GroupCheckpoint::project(group);
  const int workers = admit_ray_selection(projection, threads, budget);
  auto result = selected_list(projection, RaySelection(projection, group.ar, unit_power, workers));
  const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
  sic::support::progressf("RAY-SELECTION wall_s=%.6f rss_bytes=%llu rays=%zu\n", seconds.count(),
                          static_cast<unsigned long long>(resources::inspect().rss),
                          result.words.size());
  return result;
}

RaySelection::RaySelection(const GroupProjection& group, int threads) : group_(group) {
  // Class labels follow the exact source identity supplied by the Group projection.
  Arithmetic ar(group.exponent, false, DimensionPolicy{static_cast<unsigned>(group.exponent)});
  const long standard_degree = reduced_order::degree(ar, 1);
  int unit_power = 1;
  if (group.order_index != 1 || group.elements != standard_degree)
    unit_power = reduced_order::configure_arith(ar).k;
  prepare(ar, unit_power, threads);
}

RaySelection::RaySelection(const GroupProjection& group, const Arithmetic& arithmetic,
                           int unit_power, int threads)
    : group_(group) {
  prepare(arithmetic, unit_power, threads);
}

void RaySelection::prepare(const Arithmetic& ar, int unit_power, int threads) {
  const auto& group = group_;
  const long standard_degree = reduced_order::degree(ar, 1);
  const bool standard_order = group.order_index == 1 && group.discriminant == ar.original_Delta &&
                              group.original_discriminant == ar.original_Delta &&
                              group.classes == ar.h();
  if (ar.Delta != group.discriminant || ar.original_Delta != group.original_discriminant ||
      ar.order_index != group.order_index || ar.h() != group.classes ||
      group.elements != reduced_order::degree(ar, unit_power)) {
    if (standard_order && group.elements > 0 && group.elements < standard_degree &&
        standard_degree % group.elements == 0)
      throw sic::Error(sic::ErrorKind::unsupported, "reduced standard-order Group is unsupported");
    throw sic::Error(sic::ErrorKind::integrity, "Group projection arithmetic/dimension mismatch");
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
  parallel_for(selection_tasks(labels.size(), group.smith.size()), threads, [&](long task) {
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
  sort_ray_words(words);
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
