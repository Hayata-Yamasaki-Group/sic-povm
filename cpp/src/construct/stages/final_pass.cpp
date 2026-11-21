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
#include "construct/model/failure_test_hooks.hpp"
#include "construct/model/session_state.hpp"
#include "construct/output/quarantine.hpp"
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
#include "sic/error.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/container/producer_writer.hpp"
#include "support/resources.hpp"
namespace sic::construct {

int Construction::final_pass() {  // ---------------- final pass ----------------

  vec = sic::support::ComplexBallVector(d);

  inspect_newton_caller = [&](sic::construct::resources::ResidentCensus& census) {
    census.dynamic(X);
    census.dynamic(phis);
    census.dynamic(ps);
    census.dynamic(Ncos);
    census.dynamic(cos_top);
    census.dynamic(rep);
    census.dynamic(cos_scale);
    census.dynamic(cos_shift);
    census.dynamic(table_backing);
    census.dynamic(analytic_by_class);
    census.dynamic(step_sel);
    census.dynamic(selected_bounds);
    census.capacity(items);
    census.dynamic(item_idx);
    census.dynamic(ilv);
    census.capacity(item_sources);
    for (const auto& source : item_sources) source.resident_metadata(census);
    census.dynamic(item_constant);
    census.dynamic(vec);
    census.dynamic(path.B);
    census.dynamic(path.root);
    census.dynamic(sdt.g);
    census.dynamic(sdt.fc);
    census.dynamic(sdt.Cg);
    census.dynamic(sdt.log2U);
    census.dynamic(sdt.certified_Cg);
    census.dynamic(sdt.certified_Cf);
    census.dynamic(sdt.sel);
    if (scalar_fields) {
      census.allocation(sizeof(ScalarFixedFieldsGeneral));
      census.allocation(
          sic::support::resources::mul(scalar_fields->fields.capacity(), sizeof(ScalarFixedField)));
      census.dynamic(scalar_fields->representative_field);
      for (const auto& field : scalar_fields->fields) {
        census.dynamic(field.generators);
        census.dynamic(field.members);
      }
    }
    // Spool disk payloads are not RAM. Public tickets/index trees above own
    // their resident maps; reserve opaque FILE/stdio/callable state separately.
    if (item_spool) {
      census.allocation(sizeof(PrivateLeafStore));
      item_spool->resident_metadata(census);
    }
  };
  maxbits = 0;
  outp = quarantine_name.c_str();
  if (!outp)
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "the native producer requires SIC_OUT (the .sicx file)");

  t_exact = 0;
  for (int attempt = 0;; ++attempt) {
    direct_eta_rows.reset();
    direct_eta_rows_planned = false;
    final_table_immutable = false;
    immutable_table_owned.reset();
    item_sources.clear();
    item_sources.resize(items.size());
    item_spool.reset();
    pending_table_backing.clear();
    const slong wp = P + 16;
    component_precision.working(wp);
    slong component_requested = 0;
    bool numerical_refusal = false;
    try {
      if (attempt > 0) {
        sdt = StepData();
        final_generator_precision = 0;
        ilv.clear();
        ilv.resize(items.size());
        path = Path();
      }
      adjust_memory("final attempt", wp);
      plan_final(wp);
      prepare_completed_cache(wp, stream_tables);
      reserve_final("final refinement", 0, wp, model_->decoder_hint ? model_->decoder_hint : wp);
      item_spool = std::make_unique<PrivateLeafStore>(true, D0);
      item_sources.assign(items.size(), {});
      std::fill(item_constant.begin(), item_constant.end(), 0);
      const auto matrix_hint = producer_options.matrix_bits.get().value_or(
          std::min<sic::support::resources::BitCount>(wp, 512));
      const bool recompute_seed = MemoryPlanner::recompute_retry_seed(
          attempt > 0, phased_storage, T->prec, sic::support::resources::add(first_pass_bits, 16),
          matrix_hint);
      if (recompute_seed)
        sic::support::progressf(
            "memory item: retry seed plan=recompute source_bits=%ld coarse_bits=%ld "
            "matrix_hint=%llu\n",
            (long)T->prec, (long)first_pass_bits + 16, (unsigned long long)matrix_hint);
#ifdef SIC_ENABLE_TEST_HOOKS
      // Simulate a seed already parked by the storage planner at this boundary.
      if (failure_test_hooks::park_coarse) table_parked = true;
#endif
      if (table_parked || recompute_seed) {
        // A class-streamed table is not a complete Newton seed. Restore
        // the certified coarse table (possibly from its exact checkpoint). Also
        // discard a dense previous attempt when its source precision would inflate
        // the backed retry's Newton workspace and inverse arithmetic.
        T.reset();
        table_backing.clear();
        analytic_by_class.clear();
        const std::string origin = verified_coarse.digest;
        const auto original_cc = cc_pre;
        const auto original_derived = derived;
        T = build_pass1(first_pass_bits);
        if (completed_cache_root && origin != verified_coarse.digest)
          throw sic::Error(sic::ErrorKind::integrity,
                           "coarse origin changed during precision retry");
        if (cc_pre != original_cc || derived != original_derived)
          throw sic::Error(sic::ErrorKind::integrity,
                           "certified class pairing changed during precision retry");
        table_parked = false;
      }
      if (P != first_pass_bits || attempt > 0) {
        bool done = false;
        // seed: the current table (pass 1, or the previous attempt)
        auto Tn = newton_table(P, sic::support::resources::mul(N, phased_storage ? 32 : 40),
                               stream_tables);
        if (Tn) {
          T = std::move(Tn);
          done = true;
        }
        if (!done) {
          T.reset();
          T = build_table(P, false, stream_tables);
          sic::support::progressf(
              "table: %ld states x %ld points, %ld nodes, at %ld bits   [%.1fs]\n",
              (long)T->nstates(), d * d, T->n_nodes_total, (long)P + 16, stamp());
        }
      }
      if (phased_storage) park_table();
      final_table_immutable = !phased_storage;
      reserve_final("generators", 0, wp, model_->decoder_hint ? model_->decoder_hint : wp);
      sdt = generator_evaluator->evaluate_all(generator_choices, wp, phased_storage, true);
      final_generator_precision = wp;
      if (!sdt.ok) {
        component_precision.request(wp + std::max<slong>(32, wp / 4), "step data not certified");
      }
      if (phased_storage && !sdt.g.back().character)
        throw sic::Error(sic::ErrorKind::construction,
                         "phased storage lost its certified symbolic top");
      sic::support::progressf("step data certified   [%.1fs]\n", stamp());
      sic::support::RealBallVector attempt_scalars(1);
      arb_ptr sq0 = attempt_scalars[0];
      arb_sqrt_ui(sq0, D0, wp);
      // 1. leaves of the step data, recognized in O_K
      bool fail = false;
      for (size_t q = 0; q < items.size() && !fail; ++q) {
        reserve_final("step recognition", 1, wp, model_->decoder_hint ? model_->decoder_hint : wp);
        const Item& it = items[q];
        if (constant_item(sdt, it)) {
          item_constant[q] = 1;
          ilv[q] = std::vector<Leaf>(1);
          fmpz_set_si(ilv[q][0].x, -2 * Ncos[it.i]);
          continue;
        }
        sic::support::RealBallVector a;
        item_leaves(sdt, it, wp, a, nth);
        RecognitionLattice RL(log2_bp_of(sdt, it.i, item_height(sdt, it)), sq0, wp);
        std::atomic<long> bad(-1);
        const long ne = Ncos[it.i], nchunk = std::min<long>(ne, 16L * nth);
        const bool spill_item =
            memory.decide_reserved("step/" + std::to_string(q),
                                   {sic::support::resources::leaf_block_bytes(
                                        ne, recognition_bits(a, (slong)RL.log2Bp)),
                                    true, false}) == Retention::spill;
        if (spill_item) {
          admit_recognition_block(a, (slong)RL.log2Bp);
          struct RecognitionFailure {};
          try {
            item_sources[q] =
                item_spool->append_generated(ne, [&](size_t first_leaf, std::span<Leaf> block) {
                  const long chunks = std::min<long>(block.size(), 16L * nth);
                  parallel_for(chunks, nth, [&](long chunk) {
                    RecognitionLattice::Work work;
                    for (size_t j = chunk * block.size() / chunks;
                         j < (chunk + 1) * block.size() / chunks && bad < 0; ++j) {
                      const size_t e = first_leaf + j;
                      if (!RL.recognize(a[e], block[j].x, block[j].y, work)) {
                        bad = e;
                        break;
                      }
                      arb_clear(a[e]);
                      arb_init(a[e]);
                    }
                  });
                  if (bad >= 0) throw RecognitionFailure{};
                });
            ilv[q] = std::vector<Leaf>();
          } catch (const RecognitionFailure&) { /* numerical retry below */
          }
        } else {
          ilv[q] = std::vector<Leaf>(ne);
          parallel_for(nchunk, nth, [&](long ch) {
            RecognitionLattice::Work rw;
            for (long e = ch * ne / nchunk; e < (ch + 1) * ne / nchunk && bad < 0; ++e)
              if (!RL.recognize(a[e], ilv[q][e].x, ilv[q][e].y, rw)) bad = e;
          });
        }
        if (bad >= 0) {
          fail = true;
          sic::support::progressf(
              "  step %d %s leaf %ld not recognized: log2 rad = %.1f, needed < %.1f\n", it.i,
              it.k < 0 ? "w^2" : "f coefficient", (long)bad,
              mag_get_d_log2_approx(arb_radref(a[bad])), -std::ceil(RL.log2Bp) - 7);
        }
      }
      if (!fail) {
        long ib = 0;
        for (auto& v : ilv)
          for (auto& l : v) ib = std::max(ib, (long)std::max(fmpz_bits(l.x), fmpz_bits(l.y)));
        for (const auto& source : item_sources) ib = std::max(ib, (long)source.max_bits());
        sic::support::progressf(
            "step data recognized: %zu elements, max %ld bits per coordinate   [%.1fs]\n",
            items.size(), ib, stamp());
        if (item_spool)
          sic::support::progressf("step spool bytes=%llu\n",
                                  (unsigned long long)item_spool->bytes());
      }
      // 2. branch path from the exact step data
      if (!fail) {
        slong wpd = P;
        while (!compute_path(wpd, nullptr, path)) {
          if (wpd > 16 * P) {
            sic::support::progressf("branch path: root isolation failed\n");
            throw sic::Error(sic::ErrorKind::precision,
                             "branch path root isolation allowance exhausted");
          }
          wpd *= 2;
        }
        sic::support::progressf("branch path: sum log|iota(g#_i)| = %.3f at %ld bits   [%.1fs]\n",
                                path.score, (long)wpd, stamp());
      }
#ifdef SIC_ENABLE_TEST_HOOKS
      if (!fail && attempt == 0 && producer_options.raw("SIC_TEST_PRECISION_RETRY")) {
        sic::support::progressf("injected outer final precision retry with retained step owners\n");
        fail = true;
      }
#endif
      // Exact item leaves now determine these coefficients, including on a
      // later decoding retry. Their full analytic conjugate arrays are dead.
      if (!fail) sdt.fc.clear();
      // 3T. Galois-transported decoding (default). Only the
      // s+1 representatives are recognized; eta_m = sigma_(u)(eta_r), so iota_path(eta_m) =
      // iota_path(sigma_(u) eta_r) is read off the conjugate vector of eta_r under iota_path, which
      // an inverse Galois FFT computes from the exact leaves. The inverse FFT needs the labelled
      // conjugates iota_path(sigma g_i) for all sigma in G/H_{i+1}; they follow from the branch
      // path root iota_path(g_i) and the exact action of the Smith generators tau_l of G:
      //   quadratic step: gamma_{l,i} = tau_l(w_i) w_i lies in F_i (sigma_i(tau w) = tau(sigma_i w)
      //   = -tau w) and is an
      //     algebraic integer; W(g tau) = Gamma(g) / W(g) and W(g tau^-1) = Gamma(g tau^-1) / W(g),
      //     with W(g) = iota_path(g w_i) and Gamma = iota_path(g gamma);
      //   odd step: tau_l(v_i) = sum_k a_k v_i^k with a_k in F_i; delta (the Vandermonde
      //   determinant of the conjugates of
      //     v_i in sub-coset order, in F_i since a p-cycle is even) and delta a_k are algebraic
      //     integers, and V(g tau) = sum_k (delta a_k / delta)(g) V(g)^k.
      // The levels are processed bottom up: the conjugates of the action data of step i come from
      // an inverse FFT over the levels below i. Returns 0 on success, 1 if a leaf was not
      // recognized (retry at a higher precision), 2 to fall back to the dense decoding of every
      // entry.
      int tstat = -1;
      if (!fail) {
        adjust_memory("actions", wp);
        tstat = write_attempt(wp, sq0);
        numerical_refusal = tstat == 2;
        // 1: a leaf was not recognized; 2: the transported decoding needs more than 4 wp bits. Both
        // retry the whole attempt at a higher precision (there is no dense decoding).
        if (tstat != 0) fail = true;
      }
      if (!fail) break;
    } catch (const ComponentRefusal& error) {
      component_requested = error.requested_bits;
      if (component_requested <= wp) throw;
      sic::support::progressf("component precision refusal: %s (requested %ld bits)\n",
                              error.what(), (long)component_requested);
      if (quarantine && quarantine->has_completed_replay()) throw;
    } catch (const sic::sicx::BranchRefusal&) {
      // Branch isolation exhausted its explicit policy, rather than the attempt's
      // arithmetic precision. Raising P cannot raise that allowance.
      throw;
    }
    if (attempt >= 3) {
      if (component_requested || numerical_refusal)
        throw sic::Error(sic::ErrorKind::precision, "final numerical retry allowance exhausted");
      throw sic::Error(sic::ErrorKind::construction, "recognition failed after final retries");
    }
    P = std::max((slong)std::ceil(P * 1.25),
                 component_requested > 16 ? component_requested - 16 : P);
    sic::support::progressf("recognition failed; retrying at %ld bits\n", (long)P);
  }
  T.reset();
  t_exact = stamp();

  return 0;
}

}  // namespace sic::construct
