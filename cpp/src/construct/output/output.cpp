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
#include <filesystem>
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
#include "construct/components/exact_services.hpp"
#include "construct/model/failure_test_hooks.hpp"
#include "construct/model/session_state.hpp"
#include "construct/output/action_elements.hpp"
#include "construct/output/guarded_writer.hpp"
#include "construct/output/output_plan.hpp"
#include "construct/output/output_plan.tpp"
#include "construct/output/representative_leaves.hpp"
#include "construct/output/transported_values.hpp"
#include "construct/runtime/resources/construction_model.hpp"
#include "construct/runtime/resources/resident_memory.hpp"
#include "construct/runtime/resources/table_memory.hpp"
#include "exact/analytic/cyclotomic_trace.hpp"
#include "exact/analytic/direct_eta.hpp"
#include "exact/analytic/product_candidate.hpp"
#include "exact/analytic/step_selection.hpp"
#include "exact/analytic/table.hpp"
#include "exact/arithmetic/maximal_order.hpp"
#include "exact/certification/bounds/height_bounds.hpp"
#include "exact/certification/newton/recognition.hpp"
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
#include "sicx/codec/block.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/codec/unrescaled.hpp"
#include "sicx/container/producer_writer.hpp"
#include "sicx/container/rescaling_input.hpp"
#include "support/resources.hpp"
#include "support/resources/platform_snapshot.hpp"
namespace sic::construct {
int Construction::write_attempt(slong wp, arb_srcptr sq0) try {
#ifdef SIC_ENABLE_TEST_HOOKS
  if (failure_test_hooks::write_attempt) failure_test_hooks::write_attempt();
#endif
  const int representative_count = entry_translations.representatives.size();
  const double ts0 = stamp();
  const size_t nl = (*group_).invf.size();
  // ---- (a) action data, recognized in O_K ----
  using Elem = ActionElement;
  auto action_spool = std::make_unique<PrivateLeafStore>(true, D0);
  if (stream_actions)
    sic::support::progressf(
        "action leaves: binary disk spool (exact integers; retries retained)\n");
  std::vector<std::vector<sic::support::Index>> gens(t);
  std::vector<std::unique_ptr<Elem>> el;
  std::vector<std::vector<long>> el_of(t);
  initialize_actions(nl, gens, el, el_of);

  ScopedMemoryHook action_owners(
      inspect_final_local, with_local([&](auto& census) {
        census.dynamic(gens);
        census.dynamic(el_of);
        census.allocation(sic::support::resources::mul(el.capacity(), sizeof(el[0])));
        for (const auto& element : el) {
          census.allocation(sizeof(Elem));
          census.dynamic(element->height);
          census.dynamic(element->lv);
          element->source.resident_metadata(census);
        }
        census.allocation(sizeof(PrivateLeafStore));
        action_spool->resident_metadata(census);
      }));
  long n_act = 0;
  // leaves of one action element (a on G/H_i, consumed) and their recognition
  if (const int status = recognize_actions(wp, sq0, *action_spool, gens, el, el_of, n_act))
    return status;
  const double ts1 = stamp();
  sic::support::progressf("action data recognized: %zu elements (generators per step:", el.size());
  for (int i = 0; i < t; ++i) sic::support::progressf(" %zu", gens[i].size());
  sic::support::progressf("), %ld leaves   [%.1fs]\n", n_act, ts1);
  if (stream_actions)
    sic::support::progressf("action spool bytes=%llu\n", (unsigned long long)action_spool->bytes());

  if (phased_storage) {
    analytic_by_class.clear();
#if defined(__GLIBC__)
    sic::support::resources::release_free_heap();
#endif
  }

  // ---- (b) labelled conjugates under iota_path and the dual bases, at the decoding precision wd
  // ---- iota'(lambda) = x + y omega' is formed with enough bits for its absolute error to stay
  // below 2^{-wd}
  sic::support::RealBallVector decoder_scalars(2);
  arb_ptr omh = decoder_scalars[0];
  slong omh_prec = 0;
  TransportedValues transported{*this, wp, omh, omh_prec, nl, gens, el, el_of};
  using Dec = TransportedValues::Dec;
  double maximum_rep_height = 0;
  for (int r = 0; r < representative_count; ++r)
    maximum_rep_height = std::max(maximum_rep_height, (double)representative_height(sdt, r));
  Dec dec;
  const auto inspect_decoder = [&](auto& census) {
    census.dynamic(dec.h2w);
    census.dynamic(dec.Bo);
    census.dynamic(dec.top_dual);
    census.dynamic(dec.labels);
    census.dynamic(dec.odd_labels);
  };
  ScopedMemoryHook decoder_owners(inspect_final_local, with_local([&](auto& census) {
                                    census.dynamic(decoder_scalars);
                                    inspect_decoder(census);
                                  }));
  {
    slong wd = (slong)std::ceil(maximum_rep_height + std::log2((double)Ncos[t]) + 224);
    std::string why;
    while (!transported.build_dec(wd, dec, why)) {
      if (wd > 4 * wp) {
        sic::support::progressf("transported decoding: %s; retrying the precision\n", why.c_str());
        return 2;
      }
      sic::support::progressf("  transported decoding at %ld bits: %s; retrying at %ld bits\n",
                              (long)wd, why.c_str(), (long)(2 * wd));
      wd *= 2;
    }
  }
  const double ts2 = stamp();
  sic::support::progressf(
      "labelled conjugates of the step generators under iota_path (inverse FFT and Galois action "
      "at %ld bits)   [%.1fs]\n",
      (long)dec.wd, ts2);

  // ---- (c) the representatives: leaves, recognition, conjugates under iota_path; entries by
  // lookup ----
  arb_ptr sqd1 = decoder_scalars[1];
  arb_sqrt_ui(sqd1, d + 1, wp);
  sic::support::ComplexBallVector z2d = roots_2d(wp);
  EntryCtx Ectx;
  make_entry_ctx(*T, Ectx);
  if (phased_storage) {
    Ectx.load_class = [this](sic::support::Index c) { load_table_class(c); };
    Ectx.drop_class = [this](sic::support::Index c) { drop_table_class(c); };
  }
  std::vector<std::vector<long>> group(representative_count);
  for (long mm = 0; mm < d; ++mm) {
    long sg;
    group[rep_of(mm, sg)].push_back(mm);
  }
  // ---------------------------------------------------------------- native SICX
  // Every representative is recognized first (each into its own exact spool, or held in
  // memory below the streaming threshold); then the canonical generator rescaling
  // (docs/sicx.md section 3.7, sicx/codec/rescale.hpp) is computed from the 2-adic
  // valuations of all leaves, and the tower, action and representative elements are
  // streamed through ProducerWriter from their exactly rescaled leaves, releasing each
  // representative spool after its element is written. Extra disk against a write-as-you-go
  // producer: all representative spools at once instead of one.
  // Disk admission: a forecast of the whole file (height bounds of the recognized integers)
  // is checked against the free space up front and before every representative is
  // computed; when writing, the remaining bound is checked again before every
  // representative, and each element is admitted from its exact (rescaled) bit bound
  // before any of its blocks is written (fail-closed).
  std::unique_ptr<sic::sicx::ProducerWriter> sicx_out;
  sic::sicx::Meta sicx_header;
  sic::sicx::StreamOptions sicx_options;
  sicx_options.limits = producer_options.policy.resources;
  sic::support::resources::Bytes sicx_tower_bound = 0;
  std::vector<sic::support::resources::Bytes> sicx_rep_bounds(representative_count, 0);
  sic::support::resources::Bytes sicx_meta_reserve = 64ull << 20;
  // Bytes of one coordinate whose full integers have at most `bits` bits: two integers
  // (varint header <= 10 bytes, magnitude after the (r, y) transform <= bits + 24 bits)
  // plus one run header (<= 10 bytes).
  if (d >= (1L << 22))
    throw sic::Error(sic::ErrorKind::resource,
                     "SICX admission bound assumes d < 2^22 (24-bit pair-transform slack)");
  const std::filesystem::path sicx_dir = std::filesystem::absolute(outp).parent_path();
  plan_output(wp, representative_count, el, sicx_dir, sicx_header, sicx_options, sicx_tower_bound,
              sicx_rep_bounds, sicx_meta_reserve);
  // Tower and action elements in canonical order: per level, the step element(s), then
  // actdelta, then the actions by axis (and coefficient).

  // Recognized representatives, kept until the exact part is written.
  std::vector<KeptRepresentative> kept(representative_count);
  // Kept representative spools hold lattice coordinates (sicx_rep_lattice.hpp: about half the raw
  // size; every spool lives until the generator rescaling is known), unless the spool already
  // applies them itself (SIC_SPOOL_LATTICE).
  const bool rep_lattice = stream_reps && !producer_options.spool_lattice.value_or(false);

  // Stored leaf k maps to the logical leaf (k / prefix) * D/2 + k % prefix (adapted:
  // the bands [0, q) and [D/2, D/2 + q); other leaves are exactly zero).
  auto rep_source_of = [&](int r) -> sic::sicx::LeafSource {
    KeptRepresentative& k = kept[r];
    const long prefix = k.ne / 2, half = Ncos[t] / 2;
    return [this, &k, prefix, half, stream = (bool)k.source, lattice = rep_lattice, D0 = (long)D0,
            threads = (int)nth](const sic::sicx::LeafVisitor& visit) {
      if (stream && lattice)  // decoded blocks grow back to the original bits
        budget.require_additional(
            "representative lattice spool decode",
            sic::support::resources::add(
                sic::support::resources::leaf_block_bytes(65536,
                                                          sic::support::resources::add(k.bits, 66)),
                sic::exact::representative_lattice::scratch_bytes(k.bits, threads)));
      auto consume = [&](size_t first_leaf, std::span<const Leaf> block) {
        for (size_t q = 0; q < block.size(); ++q) {
          const size_t index = first_leaf + q;
          visit((index / prefix) * half + index % prefix, block[q].x, block[q].y);
        }
      };
      if (stream)
        k.source.read_blocks(
            [&](size_t first_leaf, std::span<Leaf> block) {
              if (lattice) sic::exact::representative_lattice::transform(block, D0, threads, true);
              consume(first_leaf, block);
            },
            65536);
      else
        consume(0, std::span<const Leaf>(k.lv));
    };
  };

  // 2-adic valuations of every leaf for the generator rescaling: the tower and action elements
  // now, each representative block while it is recognized (or read back), so J is known after
  // the last representative without another pass over the kept spools.
  {
    // Dense valuation tables (uint32): D entries for the representatives, and per tower level n
    // one table of width N_n per own weight (at most 2 p_n + 1 distinct weights). They are
    // resident from here until J is computed.
    sic::support::resources::Count entries = sicx_header.D;
    for (size_t n = 0; n < sicx_header.degrees.size(); ++n)
      entries = sic::support::resources::add(
          entries, sic::support::resources::mul(
                       sicx_header.width[n],
                       2 * (sic::support::resources::Count)sicx_header.degrees[n] + 1));
    budget.require_additional("generator rescaling valuation tables",
                              sic::support::resources::mul(4, entries));
  }
  sic::sicx::RescalingCollector collector(sicx_header, producer_options.policy.valuation);
  for_each_tower_element(el,
                         [&](const sic::sicx::ElementId& id, const sic::sicx::LeafSource& source) {
                           collector.add(id, source);
                         });

  size_t tower_elements = 0;
  for_each_tower_element(
      el, [&](const sic::sicx::ElementId&, const sic::sicx::LeafSource&) { ++tower_elements; });
  // ---- resume (--resume with the partial file of an interrupted run)
  // The partial file is opened before the representatives, and the elements it holds
  // completely (journaled element notes over re-hashed blocks) are adopted in canonical
  // order. Adopted representatives are not recomputed: their full leaves are read back from
  // the durable blocks (stored coordinate times the element scale times 2^(sum_i e_i J_i), J
  // the journaled generator rescaling) for the valuation collector and the transported
  // decoding (hence the SIC check). The recomputed J must equal the journaled one. The plan
  // binds the producer binary and the inputs.
  const std::string rescaling_note = "generator rescaling";
  size_t tower_adopted = 0;
  std::vector<char> rep_adopted(representative_count, 0);
  std::vector<uint64_t> journaled_shifts;
  const std::filesystem::path sicx_partial = std::string(outp) + ".spool.partial";
  if (sicx_options.resume && std::filesystem::exists(sicx_partial)) {
    restore_output_prefix(sicx_header, sicx_options, el, sicx_out, rescaling_note, tower_elements,
                          tower_adopted, representative_count, rep_adopted, journaled_shifts);
  }
  // Full leaves of an adopted representative in stored order, filled block by block
  // (zero where the file has no pair).

  double tm_reps = 0, tm_fft = 0, tm_rec = 0, tm_inv = 0;
  sic::support::ComplexBallVector v, vb;
  sic::support::RealBallVector y, yb;
  std::vector<Leaf> lv;
  int st = 0;
  long stored_rep_leaves = 0;
  ScopedMemoryHook representative_owners(
      inspect_final_local, with_local([&](auto& census) {
        census.dynamic(z2d);
        census.dynamic(Ectx.Tord);
        census.capacity(Ectx.Tptr);
        census.dynamic(Ectx.oi);
        census.dynamic(group);
        census.dynamic(v);
        census.dynamic(vb);
        census.dynamic(y);
        census.dynamic(yb);
        census.dynamic(lv);
        census.allocation(
            sic::support::resources::mul(kept.capacity(), sizeof(KeptRepresentative)));
        for (const auto& item : kept) {
          census.dynamic(item.lv);
          item.source.resident_metadata(census);
          if (item.spool) {
            census.allocation(sizeof(PrivateLeafStore));
            item.spool->resident_metadata(census);
          }
        }
        census.allocation(collector.resident_valuation_bytes());
        census.allocation(
            sicx_meta_reserve);  // Header, writer/journal and bounded source controls.
        // Ectx.direct aliases T; its payload is already counted by inspect_final_owners.
      }));
  ScopedMemoryHook representative_credit(
      final_local_credit,
      decltype(final_local_credit)([&](unsigned phase, sic::support::resources::BitCount bits)
                                       -> sic::support::resources::Bytes {
        if (phase != 4) return 0;
        sic::support::resources::Bytes credit =
            std::min(model_->rescaling_workspace(), collector.resident_valuation_bytes());
        // A requested wd or an old decoder never credits a new full decoder/BFS build.
        if (dec.pruned && dec.wd >= static_cast<slong>(bits)) {
          sic::construct::resources::ResidentCensus census;
          inspect_decoder(census);
          sic::support::resources::Count largest = 8;
          for (auto p : ps) largest = std::max<sic::support::resources::Count>(largest, p);
          const auto expected =
              sic::support::resources::mul(sic::support::resources::mul(t + nl, largest * d),
                                           sic::support::resources::complex_bytes(bits));
          credit = sic::support::resources::add(credit, std::min(expected, census.storage.payload));
        }
        return credit;
      }));
  RepresentativeLeaves representatives{representative_count,
                                       wp,
                                       sq0,
                                       sicx_meta_reserve,
                                       sicx_tower_bound,
                                       tower_adopted,
                                       tower_elements,
                                       rep_adopted,
                                       sicx_rep_bounds,
                                       sicx_dir,
                                       sicx_partial,
                                       dec,
                                       transported,
                                       Ectx,
                                       sqd1,
                                       z2d,
                                       v,
                                       vb,
                                       group,
                                       y,
                                       yb,
                                       lv,
                                       st,
                                       kept,
                                       rep_lattice,
                                       collector,
                                       tm_reps,
                                       tm_fft,
                                       tm_rec,
                                       tm_inv,
                                       stored_rep_leaves,
                                       journaled_shifts,
                                       sicx_out};
  process_representatives(representatives);
  if (st != 0) {
    // A precision retry recomputes everything; nothing of this attempt was written.
    kept.clear();
    return st;
  }
  // Every forward consumer and physical decoding retry has finished. Keep exact
  // step/action/representative sources for rescaling and emission; the analytic
  // inputs and pruned physical duals have no remaining consumers in this attempt.
  Ectx = EntryCtx();  // Drop borrowed table pointers before releasing their owners.
  z2d = sic::support::ComplexBallVector();
  v = sic::support::ComplexBallVector();
  vb = sic::support::ComplexBallVector();
  dec = Dec();
  for (auto& generator : sdt.g) {
    generator.full = sic::support::ComplexBallVector();
    generator.scalar = sic::support::RealBallVector();
    generator.periodic = sic::support::RealBallVector();
    generator.backing.reset();
    generator.pieces.clear();
  }
  if (T && T->compact()) {
    for (auto& values : T->packed) values = sic::support::ComplexBallVector();
    // A component can still request a whole-attempt precision retry. Preserve
    // T's shell/precision and require the retry driver to rebuild the coarse seed.
    table_parked = true;
    final_table_immutable = false;
    immutable_table_owned.reset();
  }
  table_backing.clear();
  analytic_by_class.clear();
#ifdef SIC_ENABLE_TEST_HOOKS
  trace_representative_owners("emission", representative_count - 1, representatives);
#endif
  {
    // ---- canonical generator rescaling (valuations collected above), then the exact part
    const double r0 = stamp();
    ExactTowerOps exact_ops;
    exact_ops.degrees = ps;
    exact_ops.threads = nth;
    exact_ops.exact_services = [&]() {
      ExactTower exact;
      exact.metadata = sicx_header;
      exact.metadata.elements.clear();
      auto append = [&](const sic::sicx::ElementId& id, const sic::sicx::LeafSource& source) {
        auto stats = sic::sicx::element_stats(exact.metadata, id, source);
        exact.metadata.elements.push_back(std::move(stats.info));
        exact.elements.push_back({id, source});
      };
      for_each_tower_element(el, append);
      for (int r = 0; r < representative_count; ++r)
        append({"rep", (unsigned)t, -1, -1, rep_entry(r)}, rep_source_of(r));
      return std::make_shared<ExactServices>(exact, producer_options.policy);
    };
    const sic::sicx::GeneratorRescaling rescaling =
        components.rescaling->choose(collector, exact_ops);
#ifdef SIC_ENABLE_TEST_HOOKS
    if (failure_test_hooks::resume_shifts) failure_test_hooks::resume_shifts(journaled_shifts);
#endif
    if (!journaled_shifts.empty() && journaled_shifts != rescaling.shifts)
      throw sic::Error(sic::ErrorKind::integrity,
                       "SICX resume: the generator rescaling differs from the interrupted run (" +
                           rescaling.text() + ")");
    sic::sicx::rescale_branches(sicx_header, rescaling);
    sic::support::progressf("SICX generator rescaling (%s): J=%s (sum %llu); gains",
                            sic::sicx::kRescalingRule, rescaling.text().c_str(),
                            (unsigned long long)rescaling.total());
    for (size_t i = 0; i < rescaling.gain.size(); ++i)
      sic::support::progressf("%s%llu", i ? "," : " ", (unsigned long long)rescaling.gain[i]);
    sic::support::progressf("   [%.1fs, %.1fs]\n", stamp() - r0, stamp());
    if (!sicx_out) sicx_out = open_writer(sicx_header, sicx_options);
    sicx_out->set_rescaling_shifts(rescaling.shifts);
    sicx_out->set_branches(sicx_header.branches);  // EXMT input; rescaled now that J is known
    // Journaled before the first element (compared on resume): adopted blocks are read back
    // with it.
    const std::string shifts_text = rescaling.text();
    sicx_out->writer().put_note(rescaling_note,
                                sic::sicx::ByteBuffer(shifts_text.begin(), shifts_text.end()));
    if (sicx_out->writer().pending_replay())
      sic::support::progressf(
          "SICX resume: %zu journaled chunks re-hashed; replaying (each regenerated block must "
          "equal its record)\n",
          sicx_out->writer().pending_replay());
    {
      sic::support::resources::Bytes remaining = sicx_meta_reserve;
      if (tower_adopted < tower_elements)
        remaining = sic::support::resources::add(remaining, sicx_tower_bound);
      for (int q = 0; q < representative_count; ++q)
        if (!rep_adopted[q])
          remaining = sic::support::resources::add(remaining, sicx_rep_bounds[q]);
      sicx_admit(sicx_dir, "before writing the exact part", remaining);
    }
    size_t tower_index = 0;
    for_each_tower_element(
        el, [&](const sic::sicx::ElementId& id, const sic::sicx::LeafSource& source) {
          if (tower_index++ < tower_adopted) return;
          sicx_out->add(id, sic::sicx::rescaled_source(sicx_header, id, source, rescaling));
        });
    sic::support::progressf("SICX tower and action elements written: %llu bytes   [%.1fs]\n",
                            (unsigned long long)sicx_out->stats().tower_bytes, stamp());
    for (int r = 0; r < representative_count; ++r) {
      if (rep_adopted[r]) continue;
      {
        sic::support::resources::Bytes remaining = sicx_meta_reserve;
        for (int q = r; q < representative_count; ++q)
          if (!rep_adopted[q])
            remaining = sic::support::resources::add(remaining, sicx_rep_bounds[q]);
        const std::string stage = "before representative eta_" + std::to_string(rep_entry(r));
        sicx_admit(sicx_dir, stage.c_str(), remaining);
      }
      const sic::support::resources::Bytes sicx_body_before = sicx_out->stats().body_bytes;
      sic::support::resources::Bytes element_bound = 0;
      // Certified admission for this element, after its exact first pass and before any of
      // its blocks is written: exact level set and bit bound of its (rescaled) integers; later
      // representatives keep their up-front forecast (rescaling only lowers bit lengths).
      auto admit_element = [&](const sic::sicx::ElementInfo& e) {
        element_bound = sicx_element_bound(e.layout.coordinates(), e.max_bits,
                                           sicx_out->meta().block_coordinates);
        sic::support::resources::Bytes remaining =
            sic::support::resources::add(sicx_meta_reserve, element_bound);
        for (int q = r + 1; q < representative_count; ++q)
          if (!rep_adopted[q])
            remaining = sic::support::resources::add(remaining, sicx_rep_bounds[q]);
        const std::string stage =
            "representative eta_" + std::to_string(rep_entry(r)) + " (exact bits), before writing";
        sicx_admit(sicx_dir, stage.c_str(), remaining);
      };
      const sic::sicx::ElementId id{"rep", (unsigned)t, -1, -1, rep_entry(r)};
      const auto& info = sicx_out->add(
          id, sic::sicx::rescaled_source(sicx_header, id, rep_source_of(r), rescaling),
          admit_element);
      if (sicx_out->stats().body_bytes - sicx_body_before > element_bound)
        throw std::logic_error("SICX element bytes exceed the admission bound for eta_" +
                               std::to_string(rep_entry(r)));
      sic::support::progressf(
          "SICX representative eta_%ld: %llu coordinates (levels %zu of %d), scale digits %lu, max "
          "%llu bits, "
          "body bytes so far %llu   [%.1fs]\n",
          rep_entry(r), (unsigned long long)info.layout.coordinates(), info.levels.size(), t,
          (unsigned long)info.scale.size(), (unsigned long long)info.max_bits,
          (unsigned long long)sicx_out->stats().body_bytes, stamp());
      // release the source (shared spool state) before its spool, then the in-memory leaves
      kept[r].source = ExactSource();
      kept[r].spool.reset();
      kept[r].lv = std::vector<Leaf>();
    }
    sicx_out->write_meta();
    {
      // PROV of a natively produced file: the generator rescaling record only (no producer
      // or host data, so a converter can reproduce the file byte for byte).
      sic::support::Json prov = sic::support::Json::object_value();
      if (components.selector->name() != kStepSelection)
        prov["generator_selection"] = components.selector->provenance();

      const std::string prov_text = sic::support::dump_json(prov);
      sicx_out->writer().chunk(
          "PROV", 0,
          std::span(reinterpret_cast<const uint8_t*>(prov_text.data()), prov_text.size()));
    }
    const auto root = sicx_out->writer().finish();
    quarantine->completed();
    completed_root = root;
#ifdef SIC_ENABLE_TEST_HOOKS
    if (producer_options.raw("SIC_TEST_KILL_AFTER_WRITE")) ::_exit(86);
#endif
    const auto stats = sicx_out->stats();
    sic::support::progressf(
        "SICX writer phases: pass1 %.6fs, collection %.6fs, parallel encode+roundtrip %.6fs, "
        "commit %.6fs\n",
        stats.pass1_seconds, stats.collection_seconds, stats.parallel_seconds,
        stats.commit_seconds);
    sicx_out.reset();
    sic::sicx::Reader check(outp, producer_options.policy.resources);
    if (check.root() != root)
      throw sic::Error(sic::ErrorKind::integrity, "quarantine differs from the completed writer");
    sic::support::progressf(
        "SICX exact part written to %s: %llu bytes, tower+action %llu, body %llu, blocks %llu, "
        "encode %.1fs, commit %.1fs; "
        "exact_root=%s merkle=%s (not finalized: sic-decode --finalize appends "
        "RAYS/BALL/PREC)\n",
        outp, (unsigned long long)check.size(), (unsigned long long)stats.tower_bytes,
        (unsigned long long)stats.body_bytes, (unsigned long long)stats.blocks,
        stats.encode_seconds, stats.commit_seconds, sic::sicx::hex(check.exact_root()).c_str(),
        sic::sicx::hex(root).c_str());
  }
  sic::support::progressf(
      "entries: %d representatives recognized (%ld leaves each, max %ld bits per coordinate); %ld "
      "entries by Galois transport   [%.1fs]\n",
      representative_count, Ncos[t], maxbits, d, stamp());
  sic::support::progressf(
      "  logical representative rows %ld; stored eligible rows %ld (omitted rows are exactly "
      "zero)\n",
      (static_cast<long>(representative_count)) * Ncos[t], stored_rep_leaves);
  sic::support::progressf(
      "  (action data %.1fs, labelled conjugates %.1fs; representatives: values %.1fs, leaf "
      "transform %.1fs, recognition %.1fs, inverse transform %.1fs)\n",
      ts1 - ts0, ts2 - ts1, tm_reps, tm_fft, tm_rec, tm_inv);
  sic::support::progressf("exact representation written to %s\n", outp);
  return 0;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

}  // namespace sic::construct

namespace sic::construct {}  // namespace sic::construct
