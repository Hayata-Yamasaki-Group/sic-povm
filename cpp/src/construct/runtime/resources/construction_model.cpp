// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/runtime/resources/construction_model.hpp"

#include <algorithm>
#include <limits>

#include "exact/runtime/memory/product_tiles.hpp"

namespace sic::construct::resources {
sic::support::resources::Count construction_points(sic::support::resources::Count dimension,
                                                   sic::support::resources::Count classes) {
  const auto points = mul(mul(std::max<uint64_t>(1, classes), dimension), dimension);
  constexpr auto capacity = std::numeric_limits<Index>::max();
  if (points > static_cast<uint64_t>(capacity))
    throw sic::Error(sic::ErrorKind::resource,
                     "d=" + std::to_string(dimension) + " requires " +
                         (classes ? "" : "at least ") + std::to_string(points) +
                         " points, exceeding the signed 32-bit point-index capacity of " +
                         std::to_string(capacity));
  return points;
}

Model::Model(sic::support::resources::Count dimension, sic::support::resources::Count h,
             sic::support::resources::Count cycle_max, sic::support::resources::Count state_count,
             sic::support::resources::Count reps)
    : d(dimension),
      classes(h),
      degree(construction_points(d, h) / 2),
      points(mul(degree, 2)),
      max_cycle(cycle_max),
      states(state_count),
      representatives(reps) {
  if (mul(degree, sizeof(long)) > std::numeric_limits<std::size_t>::max())
    throw sic::Error(sic::ErrorKind::resource, "group vector exceeds size_t capacity");
}

Bytes Model::group_initial() const {
  return add(64 * kMib, mul(mul(points, kGenerators + 3), sizeof(Index)));
}

Bytes Model::group_resident() const {
  const Bytes labels = compact_tables ? mul(points, sizeof(Index) + (sizeof(Index) + 1) / 2) : 0;
  return add(extra_resident,
             add(labels, add(64 * kMib, add(mul(mul(points, 3), sizeof(Index)),
                                            mul(degree, add(mul(bases, sizeof(Index)),
                                                            sizeof(long) + sizeof(int)))))));
}

Bytes Model::group_closure(sic::support::resources::Count base_count,
                           sic::support::resources::Count generator_count) const {
  const Bytes maps = mul(mul(points, generator_count + 1), sizeof(Index));
  const Bytes row = add(
      mul(sizeof(Index), base_count),
      add(mul(generator_count, 2 * sizeof(Index)), sizeof(long) + sizeof(int) + sizeof(Index) + 1));
  // Explicit bounded reservations avoid growth copies; Smith reindexing
  // permutes rows in place. Include destination/seen arrays in the forecast.
  sic::support::resources::Count slots = 16;
  while (slots < mul(degree, 2)) slots = mul(slots, 2);
  return add(64 * kMib, add(maps, add(mul(degree, row), mul(slots, sizeof(Index)))));
}

Bytes Model::table(sic::support::resources::BitCount precision) const {
  const sic::support::resources::Count count =
      compact_tables ? mul(computed_classes ? computed_classes : classes, (mul(d, d) - 1) / 3)
                     : points;
  return mul(count, complex_bytes(precision));
}

Bytes Model::quadrature_workspace(sic::support::resources::BitCount precision, int workers) const {
  const Bytes state = mul(mul(mul(d, d), 2), complex_bytes(add(precision, 80)));
  // Include the largest full-cycle fallback, not just the selected shorter
  // conjugate cycles. Callers separately charge retained tables and labels.
  const sic::support::resources::Count retained = streamed_states ? 1 : max_cycle;
  const Bytes product = streamed_states ? mul(mul(d, d), complex_bytes(precision + 20)) : 0;
  const sic::support::resources::Count active =
      streamed_states ? 1 : static_cast<sic::support::resources::Count>(workers);
  return add(mul(add(retained, active), state), product);
}

Bytes Model::quadrature(sic::support::resources::BitCount precision, int workers,
                        sic::support::resources::Count table_copies) const {
  return add(group_resident(),
             add(mul(table_copies, table(precision)), quadrature_workspace(precision, workers)));
}

Bytes Model::support(sic::support::resources::BitCount precision) const {
  return add(
      group_resident(),
      add(table(precision), mul(degree, add(complex_bytes(precision), representatives + 40))));
}

Bytes Model::rescaling_workspace() const {
  // Named future owner: the representative valuation table and a conservative
  // bound on distinct own-weight tables, kept until RescalingCollector::compute.
  sic::support::resources::Count entries = degree, width = 1;
  for (auto p : step_degrees) {
    entries = add(entries, mul(width, add(mul(2, p), 1)));
    width = mul(width, p);
  }
  return mul(4, entries);
}

Bytes Model::native_workspace(sic::support::resources::BitCount precision,
                              sic::support::resources::BitCount decoding_precision) const {
  sic::support::resources::Count largest = 2;
  for (auto p : step_degrees) largest = std::max(largest, p);
  // Scalar polynomial/Arb multiply scratch, recognizer temporaries, handles and thread stacks.
  // FLINT integer packing is charged at an enlarged mantissa, as in the Newton forecasts.
  return add(64 * kMib,
             mul(kernel_workers, add(8 * kMib, mul(add(mul(8, mul(largest, largest)), mul(16, d)),
                                                   complex_bytes(newton_matrix_bits(std::max(
                                                       precision, decoding_precision)))))));
}

std::array<Bytes, 5> Model::tower_phase_forecasts(
    sic::support::resources::BitCount precision,
    sic::support::resources::BitCount decoding_precision) const {
  const size_t t = step_degrees.size();
  if (!compact_tables || !t || coset_counts.size() != t + 1 || step_degrees.back() != 2 ||
      !smith_rank || coset_counts.back() != degree)
    throw std::runtime_error("native forecast requires an actual compact tower");
  const auto& n = coset_counts;
  const Bytes real = real_bytes(precision), complex = complex_bytes(precision);
  const Bytes zero_imag = add(real, sizeof(arb_struct)), cd = complex_bytes(decoding_precision);
  Bytes retained_generators = symbolic_top ? complex : mul(n[t - 1], real);
  Bytes reload = 0, step = 0, coefficients = 0, items = 0, action = 0;
  Bytes odd_bo = 0, h2w = 0, decode = 0;
  sic::support::resources::Count largest = 2;
  for (size_t i = 0; i < t; ++i) {
    const sic::support::resources::Count p = step_degrees[i];
    largest = std::max(largest, p);
    if (mul(n[i], p) != n[i + 1]) throw std::runtime_error("invalid forecast tower");
    if (i + 1 < t) {
      if (p == 2 && phased_final)
        reload =
            std::max(reload, mul(add(n[i], i && step_degrees[i - 1] == 2 ? n[i - 1] : 0), real));
      else
        retained_generators =
            add(retained_generators, mul(n[i + 1], p == 2 ? real / 2 : zero_imag));
      // Folded top input is real. The generic mixed transform retains two real rows.
      step = std::max(step, mul(add(mul(2, n[i + 1]), n[i]), real));
    }
    const Bytes element = mul(n[i], add(complex, sizeof(arb_struct)));
    items = std::max(items, element);
    action = std::max(
        action, p == 2 ? add(element, mul(n[i], real)) : add(mul(mul(p, n[i]), complex), element));
    if (p == 2) {
      if (!(symbolic_top && i + 1 == t)) {
        h2w = add(h2w, mul(n[i], cd));
        decode = std::max(decode, add(mul(mul(3, n[i]), cd), mul(n[i], 16)));
      }
    } else {
      coefficients = add(coefficients, mul(n[i + 1], complex));
      odd_bo = add(odd_bo, mul(mul(p, n[i + 1]), cd));
      const sic::support::resources::Count rows =
          add(n[i + 1], mul(n[i], add(add(mul(smith_rank, p), p), 3)));
      decode = std::max(decode, add(mul(rows, cd), mul(n[i + 1], 9)));
    }
  }
  const Bytes one_class = add(mul((mul(d, d) - 1) / 3, complex), table_reload_workspace);
  const Bytes analytic = phased_final ? mul(2, one_class) : mul(2, table(precision));
  const Bytes base =
      add(group_resident(),
          add(mul(descriptor_rows, real),
              add(retained_generators, native_workspace(precision, decoding_precision))));
  const sic::support::resources::Count rep_rows =
      phased_representative_rows ? phased_representative_rows : degree;
  const Bytes entry_work =
      add(mul(mul(kernel_workers, mul(8, d)), complex), mul(mul(d, smith_sum), sizeof(long)));
  const Bytes pruned = mul(
      mul(add(t, smith_rank), mul(std::max<sic::support::resources::Count>(8, largest), d)), cd);
  // A quadratic forward transform is in place. A mixed transform still needs
  // two channel arrays at its peak, but its old array is gone before recognition.
  // Successfully recognized inputs release their payloads as exact leaves grow;
  // retained exact leaves are charged separately by MemoryPlanner::decide_reserved.
  const sic::support::resources::Count channel_buffers =
      std::all_of(step_degrees.begin(), step_degrees.end(),
                  [](sic::support::resources::Count p) { return p == 2; })
          ? 1
          : 2;
  const Bytes rep =
      add(mul(mul(channel_buffers, rep_rows), real), add(pruned, add(entry_work, mul(degree, 16))));
  // Per-phase simultaneous live sets. Generator evaluation keeps folded sums; recognition
  // has coefficients and one item; transport retains unpruned lower duals and current BFS;
  // representatives retain only projected dual labels and a possible generator reload.
  std::array<Bytes, 5> phases{
      add(analytic, add(step, reload)),
      add(phased_final ? one_class : table(precision), add(coefficients, items)),
      add(phased_final ? 0 : table(precision), action),
      add(phased_final ? 0 : table(precision), add(h2w, add(odd_bo, decode))),
      add(phased_final ? one_class : table(precision),
          add(rescaling_workspace(), add(rep, std::max(reload, mul(band_generator_rows, real)))))};
  for (auto& phase : phases) phase = add(base, phase);
  return phases;
}

Bytes Model::tower_final_buffers(sic::support::resources::BitCount precision,
                                 sic::support::resources::BitCount decoding_precision) const {
  const auto phases = tower_phase_forecasts(precision, decoding_precision);
  return *std::max_element(phases.begin(), phases.end());
}

Bytes Model::final_buffers(sic::support::resources::BitCount precision,
                           sic::support::resources::BitCount decoding_precision) const {
  if (compact_tables && !step_degrees.empty())
    return tower_final_buffers(precision, decoding_precision);
  if (phased_final) {
    if (!compact_tables) throw std::runtime_error("phased forecast requires compact tables");
    const Bytes real = real_bytes(precision), complex = mul(2, real);
    const Bytes base =
        add(group_resident(), add(mul(degree, 32), add(256 * kMib, mul(descriptor_rows, real))));
    // Each formula is a simultaneous live set, not a sum of sequential
    // phases. Lower analytic generators are parked between uses; the
    // table is parked before physical transport and streamed by class.
    const Bytes one_class = add(mul((mul(d, d) - 1) / 3, complex), table_reload_workspace);
    const Bytes generator_work = std::max(
        phased_streamed_table ? mul(2, one_class) : table(precision),
        add(one_class,
            mul(generator_metadata ? generator_rows : degree / 2, add(real, sizeof(arb_struct)))));
    // sic::support::ComplexBallVector input midpoints are swapped into a real output vector. Its
    // imaginary components and both structure arrays remain temporarily,
    // but they do not duplicate the real midpoint allocation. Only a
    // nonperiodic generator reload adds another full real payload.
    const Bytes actions =
        generator_metadata
            ? add(mul(degree / 4, add(real, sizeof(acb_struct) + sizeof(arb_struct))),
                  mul(action_generator_rows, real))
            : mul(degree / 4, add(mul(2, real), sizeof(arb_struct)));
    const Bytes transport = mul(mul(3, degree / 4), complex_bytes(decoding_precision));
    const sic::support::resources::Count rep_rows =
        phased_representative_rows ? phased_representative_rows : degree;
    const Bytes transform = mul(add(rep_rows, std::min(degree / 4, generator_window)), real);
    const Bytes entry_work =
        add(mul(mul(kernel_workers, mul(8, d)), complex), mul(mul(d, smith_sum), sizeof(long)));
    const Bytes values =
        add(mul(rep_rows, real),
            add(entry_work,
                add(one_class, add(mul(degree / 2, 16), mul(2 * action_generator_rows, real)))));
    return add(base, std::max({generator_work, actions, transport, transform, values}));
  }
  // Dense analytic generators, retained step leaves, labelled physical
  // generator transport (including BFS/scratch), and representative forward
  // and inverse transforms. Odd steps need a measured tower-specific audit.
  return add(group_resident(), add(mul(2, table(precision)),
                                   add(mul(degree, add(mul(5, complex_bytes(precision)), 64)),
                                       mul(degree, mul(8, complex_bytes(decoding_precision))))));
}

Bytes Model::newton_worker(sic::support::resources::BitCount precision, bool reference,
                           sic::support::resources::BitCount matrix_hint, bool modular,
                           int phase_workers, bool column_inverse, bool threaded_product,
                           bool parallel_reconstruction, bool refined_correction) const {
  const sic::support::resources::Count n = 2 * d - 2;
  const sic::support::resources::Count rows =
      reference ? mul(d, d) - 1 : std::min(mul(8, n), mul(d, d) - 1);
  const sic::support::resources::BitCount selection_bits =
      std::max<sic::support::resources::BitCount>(160, matrix_hint ? matrix_hint : precision);
  const Bytes selection = modular
                              ? add(mul(mul(rows, n), complex_bytes(selection_bits)),
                                    modular_selection_bytes(rows, n, phase_workers, selection_bits))
                              : mul(mul(rows, n), add(complex_bytes(160), 32));
  const Bytes certification =
      add(mul(mul(n, n),
              mul(8, complex_bytes(newton_matrix_bits(matrix_hint ? matrix_hint : precision)))),
          add(column_inverse
                  ? inverse_column_scratch(n, phase_workers, matrix_hint ? matrix_hint : precision)
                  : 0,
              threaded_product
                  ? sic::exact::product_tiles::scratch(
                        n, phase_workers, newton_matrix_bits(matrix_hint ? matrix_hint : precision))
                  : 0));
  // Streamed late work retains one source grid, two residual caches and
  // possibly the previous reconstruction. Setup's four source grids are
  // released before those caches; source/growth guards retain their max.
  // Unknown modular conditioning uses seed/inverse precision in its
  // empirical high-precision proposal. Reserve growth before scheduling;
  // actual source, matrix and vector precisions still have separate gates.
  const sic::support::resources::BitCount vector_growth =
      modular ? std::max<sic::support::resources::BitCount>(256, add(selection_bits, 512)) : 256;
  const Bytes seed_values = mul(streamed_states ? 4 : add(mul(2, max_cycle), 4),
                                complex_bytes(add(precision, vector_growth)));
  // pos/ipos/Kexp and O(d) contexts/vectors are separate from these grids.
  // The small-object margin also covers the bounded scalar row slots and
  // their crew, released before inverse/product worker admission.
  return add(
      refined_correction
          ? linear_correction_workspace(d, add(matrix_hint ? matrix_hint : selection_bits, 512),
                                        phase_workers)
          : 0,
      add(parallel_reconstruction && phase_workers > 1
              ? ghost_reconstruction_scratch(d, add(precision, vector_growth), phase_workers)
              : 0,
          add(512 * kMib, add(mul(mul(d, d), seed_values), std::max(selection, certification)))));
}

sic::support::resources::Count Model::representative_rows() const {
  return mul(representatives, degree);
}

}  // namespace sic::construct::resources
