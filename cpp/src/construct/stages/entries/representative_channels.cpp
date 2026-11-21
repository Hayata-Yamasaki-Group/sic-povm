// SPDX-License-Identifier: AGPL-3.0-or-later
#include <flint/acb_dft.h>
#include <flint/acb_poly.h>
#include <malloc.h>
#include <sys/resource.h>
#include <sys/statvfs.h>

#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string_view>

#include "construct/components/components.hpp"
#include "construct/model/session_state.hpp"
#include "construct/runtime/resources/construction_model.hpp"
#include "construct/runtime/resources/resident_memory.hpp"
#include "construct/runtime/resources/table_memory.hpp"
#include "construct/runtime/session/field_values.inl.hpp"
#include "exact/analytic/cyclotomic_trace.hpp"
#include "exact/analytic/direct_eta.hpp"
#include "exact/analytic/product_candidate.hpp"
#include "exact/analytic/step_selection.hpp"
#include "exact/analytic/table.hpp"
#include "exact/arithmetic/maximal_order.hpp"
#include "exact/certification/bounds/height_bounds.hpp"
#include "exact/certification/reconstruction/ghost.hpp"
#include "exact/group/group.hpp"
#include "exact/group/group_checkpoint.hpp"
#include "exact/group/group_fourier.hpp"
#include "exact/runtime/newton_class_memory.hpp"
#include "exact/runtime/parallel.hpp"
#include "exact/storage/analytic_generator.hpp"
#include "exact/storage/completed_class_cache.hpp"
#include "exact/storage/ghost_class_backing.hpp"
#include "exact/storage/leaf_source.hpp"  // exact leaf spools (sic::exact::storage::Source, Private)
#include "exact/storage/leaf_store.hpp"
#include "exact/storage/table_checkpoint.hpp"
#include "exact/transform/fixed_field_tower_general.hpp"
#include "exact/transform/leaf_transform.hpp"
#include "exact/transform/quadratic_transform.hpp"
#include "exact/transform/relative_projection.hpp"
#include "exact/transform/representative_lattice.hpp"
#include "exact/transform/trace_band.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/container/producer_writer.hpp"
#include "support/resources.hpp"
namespace sic::construct {
void Construction::representative_channels(const EntryCtx& context, int r, slong wp,
                                           const sic::support::ComplexBallVector& roots,
                                           const arb_t sqrt_d1, const StepData& sd,
                                           sic::support::RealBallVector& channels) {
  const auto& field = scalar_fields->fields[scalar_fields->representative_field[r]];
  if (!sd.g.back().character) throw std::logic_error("direct real channels require symbolic top");
  channels = sic::support::RealBallVector(2 * field.degree);
  auto write_channel = [&](sic::support::RealBallVector& destination, long g, long c,
                           acb_srcptr value) {
    arb_mul_2exp_si(destination[c], acb_realref(value), 1);
    arb_mul(destination[field.degree + c], acb_imagref(value), acb_imagref(sd.g.back().root[0]),
            wp);
    arb_mul_si(destination[field.degree + c], destination[field.degree + c],
               -2 * eighth_character((*group_).nn[g]), wp);
  };
  std::function<void(long, long, acb_srcptr)> consume = [&](long g, long c, acb_srcptr value) {
    write_channel(channels, g, c, value);
  };
  sic::support::ComplexBallVector unused;
  const bool check_groups = producer_options.check_entry_groups;
  if (check_groups && (context.load_class || !context.direct))
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "pointwise entry-group checks require a resident compact table");
  const bool group_resident =
      context.direct &&
      (check_groups || producer_options.group_resident_entries.value_or(field.degree >= 1048576));
  if (!context.load_class && !group_resident) {
    eta_entry(context, rep_entry(r), wp, roots, sqrt_d1, unused, nth, &rep[field.level], &consume);
    return;
  }
  try {
    auto extra = sic::support::resources::add(sic::support::resources::kMib,
                                              sic::support::resources::mul(field.degree, 16));
    if (check_groups)
      extra = sic::support::resources::add(
          extra,
          sic::support::resources::mul(2 * field.degree, sic::support::resources::real_bytes(wp)));
    budget.require_additional("class-grouped entry labels/reference", extra);
  } catch (const sic::support::resources::AdmissionError& error) {
    if (context.load_class || check_groups) throw;
    sic::support::progressf("resident entry grouping unavailable: %s; using original order\n",
                            error.what());
    eta_entry(context, rep_entry(r), wp, roots, sqrt_d1, unused, nth, &rep[field.level], &consume);
    return;
  }
  sic::support::RealBallVector reference(check_groups ? 2 * field.degree : 0);
  if (check_groups) {
    std::function<void(long, long, acb_srcptr)> consume_reference =
        [&](long g, long c, acb_srcptr value) { write_channel(reference, g, c, value); };
    eta_entry(context, rep_entry(r), wp, roots, sqrt_d1, unused, nth, &rep[field.level],
              &consume_reference);
  }
  // All terms at a fixed g belong to the class of g applied to class 0.
  // Paired table labels identify its physically stored conjugate class.
  std::vector<std::vector<sic::support::Index>> by_class((*ar_).h());
  const sic::support::Index anchor = (*group_).ghost_point(0, 1);
  for (long c = 0; c < field.degree; ++c) {
    const long g = rep[field.level][c];
    const sic::support::Index encoded =
        context.direct->layout->encoded((*group_).act(g, anchor), context.direct->paired_values);
    if (!encoded) throw std::logic_error("zero anchor in class-grouped entry evaluation");
    const long source =
        ((encoded < 0 ? -(long)encoded : encoded) - 1) / context.direct->layout->slots;
    by_class.at(source).push_back(sic::support::checked_index(c));
  }
  for (sic::support::Index source = 0; source < (*ar_).h(); ++source) {
    const auto& indices = by_class[source];
    if (indices.empty()) continue;
    std::vector<long> requested;
    requested.reserve(indices.size());
    for (sic::support::Index c : indices) requested.push_back(rep[field.level][c]);
    std::function<void(long, long, acb_srcptr)> scatter = [&](long g, long c, acb_srcptr value) {
      consume(g, indices[c], value);
    };
    class_entries(context, source, rep_entry(r), wp, roots, sqrt_d1, requested, scatter, 1);
  }
  if (check_groups) {
    for (size_t q = 0; q < channels.size(); ++q)
      if (!arb_equal(channels[q], reference[q]))
        throw sic::Error(sic::ErrorKind::construction,
                         "resident class-grouped entry balls differ from the original order");
    sic::support::progressf(
        "resident class grouping Arb equality passed: representative=%ld rows=%zu\n", rep_entry(r),
        channels.size());
  }
}

void Construction::transform_channels(sic::support::RealBallVector& channels, int r,
                                      const StepData& sd, slong wp,
                                      sic::support::RealBallVector* scratch, int band_bits) {
  const auto& field = scalar_fields->fields[scalar_fields->representative_field[r]];
  leaves_fft_real(channels, field.level - band_bits, sd, wp, nth, scratch);
  for (size_t e = 0; e < channels.size(); ++e)
    arb_mul_ui(channels[e], channels[e], field.members.size(), wp);
}

void Construction::representative_band_channels(const EntryCtx& context, int r, slong wp,
                                                const sic::support::ComplexBallVector& roots,
                                                const arb_t sqrt_d1, const StepData& sd, int bits,
                                                long band, sic::support::RealBallVector& channels) {
  const auto& field = scalar_fields->fields[scalar_fields->representative_field[r]];
  const long group_size = 1L << bits, size = field.degree >> bits;
  if (!context.direct || bits <= 0 || bits > representative_band_bits(r) || band < 0 ||
      band >= group_size)
    throw std::logic_error("invalid compact representative band");
  budget.require_additional(
      "representative band and grouped labels",
      sic::support::resources::add(
          sic::support::resources::mul(2 * size, sic::support::resources::real_bytes(wp)),
          sic::support::resources::mul(field.degree, 16)));
  channels = sic::support::RealBallVector(2 * size);
  for (int i = field.level - bits; i < field.level; ++i)
    if (sd.g[i].backing) {
      budget.require_additional(
          "upper band generator",
          sic::support::resources::mul(Ncos[i], sic::support::resources::real_bytes(wp)));
      sd.g[i].prepare();
    }
  std::vector<std::vector<sic::support::Index>> by_class((*ar_).h());
  for (long parent = 0; parent < size; ++parent) {
    const sic::support::Index source = source_class(rep[field.level][group_size * parent]);
    // This explicit check also validates locality of the complete ownership
    // group; no table is reloaded per point or shared writer introduced.
    for (long j = 1; j < group_size; ++j)
      if (source_class(rep[field.level][group_size * parent + j]) != source)
        throw std::runtime_error("representative band spans multiple physical classes");
    by_class.at(source).push_back(parent);
  }
  sic::support::ComplexBallVector unused;
  for (sic::support::Index source = 0; source < (*ar_).h(); ++source) {
    const auto& parents = by_class[source];
    if (parents.empty()) continue;
    std::vector<long> requested;
    requested.reserve(parents.size() * group_size);
    for (long parent : parents)
      for (long j = 0; j < group_size; ++j)
        requested.push_back(rep[field.level][group_size * parent + j]);
    std::function<void(long, long, acb_srcptr)> consume = [&](long g, long position,
                                                              acb_srcptr value) {
      const long parent = parents[position / group_size];
      const long label = parent * group_size + position % group_size;
      sic::support::RealBallVector work(2);
      arb_mul_2exp_si(work[0], acb_realref(value), 1);
      arb_mul(work[1], acb_imagref(value), acb_imagref(sd.g.back().root[0]), wp);
      arb_mul_si(work[1], work[1], -2 * eighth_character((*group_).nn[g]), wp);
      for (int channel = 0; channel < 2; ++channel)
        add_trace_band(channels[channel * size + parent], work[channel], label,
                       std::span<const long>(Ncos.data(), field.level + 1), bits, band, wp,
                       [&](int i, long c) { return sd.g[i].real_even(c); });
    };
    class_entries(context, source, rep_entry(r), wp, roots, sqrt_d1, requested, consume,
                  group_size);
  }
  for (int i = field.level - bits; i < field.level; ++i) sd.g[i].park();
}
}  // namespace sic::construct
