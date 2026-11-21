// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/representative_leaves.hpp"

#include "construct/output/output_plan.hpp"
#include "construct/output/resumed_representative.hpp"
#include "construct/runtime/observations/progress.hpp"
#include "construct/runtime/resources/construction_model.hpp"
#include "construct/runtime/resources/resident_memory.hpp"
#include "construct/runtime/session/field_values.inl.hpp"
#include "exact/certification/newton/recognition.hpp"
#include "exact/transform/representative_lattice.hpp"

namespace sic::construct {

void Construction::process_representatives(RepresentativeLeaves& state) {
  auto& representative_count = state.representative_count;
  auto& wp = state.wp;
  auto sq0 = state.sq0;
  auto& sicx_meta_reserve = state.sicx_meta_reserve;
  auto& sicx_tower_bound = state.sicx_tower_bound;
  auto& tower_adopted = state.tower_adopted;
  auto& tower_elements = state.tower_elements;
  auto& rep_adopted = state.rep_adopted;
  auto& sicx_rep_bounds = state.sicx_rep_bounds;
  auto& sicx_dir = state.sicx_dir;
  auto& sicx_partial = state.sicx_partial;
  auto& dec = state.dec;
  auto& transported = state.transported;
  auto& Ectx = state.Ectx;
  auto& sqd1 = state.sqd1;
  auto& z2d = state.z2d;
  auto& v = state.v;
  auto& vb = state.vb;
  auto& group = state.group;
  auto& y = state.y;
  auto& yb = state.yb;
  auto& lv = state.lv;
  auto& st = state.st;
  auto& kept = state.kept;
  auto& rep_lattice = state.rep_lattice;
  auto& collector = state.collector;
  auto& tm_reps = state.tm_reps;
  auto& tm_fft = state.tm_fft;
  auto& tm_rec = state.tm_rec;
  auto& tm_inv = state.tm_inv;
  auto& stored_rep_leaves = state.stored_rep_leaves;
  auto& journaled_shifts = state.journaled_shifts;
  auto& sicx_out = state.sicx_out;
  for (int r = 0; r < representative_count && st == 0; ++r) {
#ifdef SIC_ENABLE_TEST_HOOKS
    trace_representative_owners("begin", r, state);
#endif
    {
      // The whole file (nothing is written before every representative is recognized) plus a
      // forecast of the spools of the representatives still to compute (the SICX bound of each,
      // used as the spool forecast; spools live until their element is written), so a run that
      // would be refused at write time fails here, before the compute.
      // On resume, adopted elements are already in the partial file (a spool for an adopted
      // representative lives only while it is decoded).
      sic::support::resources::Bytes remaining = sicx_meta_reserve;
      if (tower_adopted < tower_elements)
        remaining = sic::support::resources::add(remaining, sicx_tower_bound);
      for (int q = 0; q < representative_count; ++q)
        if (!rep_adopted[q])
          remaining = sic::support::resources::add(remaining, sicx_rep_bounds[q]);
      if (stream_reps)
        for (int q = r; q < representative_count; ++q)
          if (q == r || !rep_adopted[q])
            remaining = sic::support::resources::add(remaining, sicx_rep_bounds[q]);
      const std::string stage = "before computing representative eta_" +
                                std::to_string(rep_entry(r)) +
                                " (remaining file + forecast of the remaining spools)";
      sicx_admit(sicx_dir, stage.c_str(), remaining);
    }
    reserve_final("representative", 4, wp, dec.wd);
    const auto& planned_field = scalar_fields->fields[scalar_fields->representative_field[r]];
    const bool spill_rep =
        requested_band_bits ||
        memory.decide_reserved(
            "representative/" + std::to_string(r),
            {sic::support::resources::add(
                 sicx_rep_bounds[r],
                 sic::support::resources::mul(2 * planned_field.degree, 2 * sizeof(Leaf) + 96)),
             true, false}) == Retention::spill;
    const bool adopted = rep_adopted[r];
    const int band_bits = requested_band_bits && !adopted ? representative_band_bits(r) : 0;
    auto rep_spool = spill_rep ? std::make_unique<PrivateLeafStore>(true, D0) : nullptr;
    long rep_bits = 0;  // bit bound of the original pairs of a spooled representative
    ExactSource rep_source;
    ScopedMemoryHook current_representative_owners(inspect_final_local,
                                                   with_local([&](auto& census) {
                                                     rep_source.resident_metadata(census);
                                                     if (rep_spool) {
                                                       census.allocation(sizeof(PrivateLeafStore));
                                                       rep_spool->resident_metadata(census);
                                                     }
                                                   }));
    FreshRepresentative operation{r,         band_bits,  wp,       sq0,         Ectx,
                                  sqd1,      z2d,        y,        yb,          lv,
                                  rep_spool, rep_source, rep_bits, rep_lattice, collector};
    operation.ne = 2 * planned_field.degree;
#ifdef SIC_ENABLE_TEST_HOOKS
    operation.census = &state;
#endif
    const double resumed_start = stamp();
    if (adopted) {
      // Read back, not recomputed (resume): leaves, valuations and maxbits as after recognition.
      const long ne = 2 * scalar_fields->fields[scalar_fields->representative_field[r]].degree;
      auto reader = std::make_unique<ResumedRepresentative>(
          sicx_partial, *sicx_out, tower_elements + (size_t)r, rep_entry(r), ne / 2, Ncos[t] / 2,
          journaled_shifts);
      if (spill_rep) {
        rep_source = rep_spool->append_generated(ne, [&](size_t first_leaf, std::span<Leaf> block) {
          reader->fill(first_leaf, block);
          spool_representative_block(operation, first_leaf, block);
        });
        maxbits = std::max(maxbits, rep_bits);
      } else {
        lv = std::vector<Leaf>(ne);
        reader->fill(0, std::span<Leaf>(lv));
        collect_representative_block(operation, 0, lv);
        for (const auto& leaf : lv)
          maxbits = std::max(maxbits, (long)std::max(fmpz_bits(leaf.x), fmpz_bits(leaf.y)));
      }
      reader->finish();
      if (producer_options.progress)
        sic::support::progressf(
            "representative eta_%ld adopted from the partial file: %zu blocks read back   "
            "[%.1fs]\n",
            rep_entry(r), reader->blocks.size(), stamp());
      release_representative_forward(r);
      operation.values_time = stamp() - resumed_start;
    } else {
      st = recognize_fresh_representative(operation);
      if (st != 0) break;
    }
    const long ne = operation.ne;
    stored_rep_leaves += ne;
    double c3 = stamp();
#ifdef SIC_ENABLE_TEST_HOOKS
    trace_representative_owners("recognized", r, state);
#endif
    std::vector<long> lab;
    for (long mm : group[r]) {
      long sg;
      rep_of(mm, sg);
      lab.push_back(coset(t, sg));
    }
    std::sort(lab.begin(), lab.end());
    lab.erase(std::unique(lab.begin(), lab.end()), lab.end());
    y = sic::support::RealBallVector();
    yb = sic::support::RealBallVector();
    for (;;) {
      {
        const auto& field = scalar_fields->fields[scalar_fields->representative_field[r]];
        std::vector<long> projected;
        for (long label : lab) projected.push_back(label / (Ncos[t] / field.degree));
        projected.erase(std::unique(projected.begin(), projected.end()), projected.end());
        sic::support::ComplexBallVector U, V;
        transported.inv_fft_pruned(lv, field.level, dec, U, projected, 0, rep_source, rep_lattice,
                                   rep_bits);
        transported.inv_fft_pruned(lv, field.level, dec, V, projected, field.degree, rep_source,
                                   rep_lattice, rep_bits);
        v = sic::support::ComplexBallVector(lab.size());
        sic::support::ComplexBallVector work(1);
        for (size_t index = 0; index < lab.size(); ++index) {
          const long prefix_label = lab[index] / (Ncos[t] / field.degree);
          const size_t position =
              std::lower_bound(projected.begin(), projected.end(), prefix_label) -
              projected.begin();
          acb_mul(work[0], V[position], dec.top_dual[0], dec.wd);
          if (eighth_character((*group_).nn[rep[t][lab[index]]]) < 0) acb_neg(work[0], work[0]);
          acb_mul_2exp_si(v[index], U[position], -1);
          acb_add(v[index], v[index], work[0], dec.wd);
          acb_div_ui(v[index], v[index], field.members.size(), dec.wd);
        }
      }
      bool okv = true;
      for (long mm : group[r]) {
        long sg;
        rep_of(mm, sg);
        const size_t index = std::lower_bound(lab.begin(), lab.end(), coset(t, sg)) - lab.begin();
        acb_set(vec[mm], v[index]);
        if (!acb_is_finite(vec[mm]) || acb_rel_accuracy_bits(vec[mm]) < 160) okv = false;
        if (producer_options.tower_debug) {
          sic::support::progressf("    m=%ld coset %ld acc %ld: ", mm, (long)coset(t, sg),
                                  (long)acb_rel_accuracy_bits(vec[mm]));
          print_acb(vec[mm], 20, 0);
          sic::support::progressf("\n");
        }
      }
      if (okv) break;
      std::string why;
      const slong wd2 = 2 * dec.wd;
      if (wd2 > 4 * wp || !transported.build_dec(wd2, dec, why)) {
        sic::support::progressf(
            "transported decoding: accuracy shortfall; retrying the precision\n");
        st = 2;
        break;
      }
      sic::support::progressf(
          "  transported decoding: accuracy shortfall for representative %ld, redone at %ld bits\n",
          rep_entry(r), (long)dec.wd);
    }
    const double values_time = operation.values_time;
    const double fft_time = operation.fft_time;
    const double recognition_time = operation.recognition_time;
    tm_reps += values_time;
    tm_fft += fft_time;
    tm_rec += recognition_time;
    tm_inv += stamp() - c3;
    if (compact_work) {
      v = sic::support::ComplexBallVector();
      vb = sic::support::ComplexBallVector();
    }
    if (adopted) {
      // already written: its leaves were needed only for the decoding and the valuations
      rep_source = ExactSource();
      rep_spool.reset();
      lv = std::vector<Leaf>();
    } else if (st == 0) {
      kept[r].ne = ne;
      kept[r].bits = rep_bits;
      kept[r].spool = std::move(rep_spool);
      kept[r].source = rep_source;
      kept[r].lv = std::move(lv);
      lv = std::vector<Leaf>();
    }
    if (d >= 128)
      sic::support::progressf(
          "  representative eta_%ld (%zu entries): values %.1fs, leaf transform %.1fs, recognition "
          "%.1fs, conjugates %.1fs   [%.1fs]\n",
          rep_entry(r), group[r].size(), values_time, fft_time, recognition_time, stamp() - c3,
          stamp());
#ifdef SIC_ENABLE_TEST_HOOKS
    trace_representative_owners("retained", r, state);
#endif
  }
}
}  // namespace sic::construct
