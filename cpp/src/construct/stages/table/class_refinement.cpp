// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/stages/table/class_refinement.hpp"

#include "construct/model/session_state.hpp"
namespace sic::construct {
void Construction::refine_class_data(ClassRefinement& state, long c, const Table& Ts,
                                     bool check_seed, sic::support::resources::Bytes private_slot,
                                     sic::support::resources::Bytes callback_slot) {
  auto& phase_admission = state.phase_admission;
  auto& memory_audit = state.memory_audit;
  auto& root_backing = state.root_backing;
  auto& target = state.target;
  auto& matrix_hint = state.matrix_hint;
  auto& effective_correction_passes = state.effective_correction_passes;
  const int class_nth = state.class_counts[c];
  auto& publication = state.publication;
  auto& retained_memory = state.retained_memory;
  auto& Shint = state.Shint;
  auto& cond = state.cond;
  auto& ratio = state.ratio;
  auto& selection_kinds = state.selection_kinds;
  auto& inverse_hints = state.inverse_hints;
  auto& ok = state.ok;
  auto& seedfail = state.seedfail;
  auto& used_quadrature = state.used_quadrature;
  auto& why = state.why;
  auto& outcomes = state.outcomes;
  auto& private_requests = state.private_requests;
  auto& Tn = state.Tn;
  auto& completed_roots = state.completed_roots;
  auto& sig = state.sig;
  auto& precision = state.P;
#ifdef SIC_ENABLE_TEST_HOOKS
  auto& defer_class = state.defer_class;
  auto& injected_defer = state.injected_defer;
  auto& injected_entry = state.injected_entry;
  auto& defer_entry = state.defer_entry;
#endif

  const long g = sig[c];
  seedfail[c] = 0;
  if (g < 0) {
    why[c] = "no element maps class 0 to this class";
    return;
  }
  if (check_seed && Ts.prec >= target) return;
  if (Ts.prec >= target) {  // a seed recomputed at the target precision is the class table itself
    sic::support::ComplexBallVector value(1);
    for (long q = 1; q < d * d; ++q) {
      Ts.read(value[0], c * d * d + q);
      acb_set_round(Tn->writable(c * d * d + q), value[0], target);
    }
    ok[c] = 1;
    used_quadrature[c] = 1;
    why[c] = "by quadrature (seed at the target precision)";
    return;
  }
  // A one-state cycle is cheap to integrate directly. At d>=256 its badly
  // scaled Newton subsystem can cost more to select/certify than the whole
  // quadrature.
  if (d >= 256 && (*ar_).cls[c].digit.size() == 1) {
    why[c] = "cost policy: one-state cycle";
    return;
  }
  sic::support::ComplexBallVector expanded_seed(d * d), refined_values;
  auto& V0 = expanded_seed.v;
  auto& V = refined_values.v;
  for (long y = 1; y < d * d; ++y) {
    Ts.read(&V0[y], (*group_).act(g, (*group_).ghost_point(y / d, y % d)));
    if ((*group_).chi[g] < 0) acb_neg(&V0[y], &V0[y]);
  }
  // accuracy of the seed: X = (1/d) sum A q^i p^j has entries of size ~1 built from values up to
  // 2^lmax, so its absolute error is ~2^(lmax - amin). With less than ~64 correct bits in X the
  // box B0 is too wide for the Krawczyk test, so the seed is recomputed at once (the test itself
  // would cost two O(d^3) matrix products).
  slong amin = WORD_MAX;
  double lmax = -1e300, lmin = 1e300;
  for (long y = 1; y < d * d; ++y) {
    amin = std::min(amin, acb_rel_accuracy_bits(&V0[y]));
    mag_t mg;
    mag_init(mg);
    acb_get_mag(mg, &V0[y]);
    double l = mag_get_d_log2_approx(mg);
    mag_clear(mg);
    lmax = std::max(lmax, l);
    lmin = std::min(lmin, l);
  }
  if (producer_options.ghost_debug)
    fprintf(stderr,
            "  [seed class %ld at %ld bits] min rel accuracy %ld bits, log2|V| in [%.1f, %.1f], "
            "%d states\n",
            c, (long)Ts.prec, (long)amin, lmin, lmax, (int)(*ar_).cls[c].digit.size());
  // The d=512 maximal-order principal ghost has a two-state cycle and
  // values up to 2^81. Its approximate inverse failed after expensive
  // selection; direct quadrature took 46.8 s. Apply this empirical cost
  // rule before recomputing a seed or attempting that inverse.
  if (d >= 512 && (*ar_).cls[c].digit.size() <= 2 && lmax >= 64) {
    why[c] = "cost policy: short cycle with large values";
    return;
  }
  // (empirically the B0 test also needs amin >~ 2 lmax + 64 for the badly scaled principal ghost,
  // with the box
  //  shape adapted to I - Y J (ghost.hpp): d = 128 passes with 214 correct bits (lmax = 59), d =
  //  256 with 334 (lmax = 119); with the seed-shaped box it needed 2.5 lmax + 64. The other
  //  ghosts, lmax <= 25, pass with 142.)
  const double seed_a = producer_options.seed_a.get();
  const double short_bits =
      std::max(lmax + 64 + std::log2((double)d), seed_a * lmax + 64) - (double)amin;
  if (short_bits > 0 && Ts.prec < target) {
    ok[c] = 0;
    seedfail[c] = 1;
    ratio[c] = short_bits;
    char buf[160];
    snprintf(buf, sizeof buf, "seed too coarse (%ld correct bits, values up to 2^%.0f)", (long)amin,
             lmax);
    why[c] = buf;
    return;
  }
  if (check_seed) return;
  GhostRefiner GR(d, (*group_).chi[g], ((*ar_).order_index * (*group_).nn[g]) % (2 * d));
  GR.retain_certified_root = root_backing;
  GR.workspace_limit = private_slot;
  GR.correction_passes = effective_correction_passes;
  GR.matrix_precision_hint = matrix_hint;
  GR.kernel_threads = std::max(1, nth / class_nth);
  GR.phase_admission = phase_admission;
  GR.retained_memory = retained_memory[c];
  GR.memory_publication = publication;
  GR.callback_workspace_limit = callback_slot;
  if (memory_audit)
    GR.memory_observer = [c, phase_admission](const phase_memory::Event& event) {
      sic::support::progressf(
          "Newton memory audit: class=%ld phase=%s object=%s actual=%llu forecast=%llu "
          "storage_ok=%d contract_so_far=%d finite_lagom=%d formula_ok=%d "
          "phase_status=%s phase_bytes=%llu peak=%llu "
          "callback_slot=%llu admission=%s\n",
          c, phase_memory::kPhaseNames[static_cast<size_t>(event.phase)], event.object,
          (unsigned long long)event.storage.bytes(), (unsigned long long)event.object_forecast,
          (int)event.storage_fits(), (int)event.observed_contract_ok,
          (int)event.storage.finite_lagom, (int)event.forecast_valid,
          event.observed_storage ? "snapshot" : "requested",
          (unsigned long long)event.accounting.at(event.phase),
          (unsigned long long)event.accounting.peak(), (unsigned long long)event.callback_slot,
          phase_admission ? "phase" : "workspace");
    };
  if (const auto& selection = producer_options.selection_threads.get(); selection)
    GR.kernel_threads = std::min(GR.kernel_threads, *selection);
  std::optional<ClassNativeShare> native_share;
  native_share.emplace(GR.kernel_threads, nth, class_nth);
  if (producer_options.progress)
    sic::support::progressf(
        "Newton class worker share: class=%ld explicit=%d native=%d allocation=%d classes=%d "
        "owned=%d\n",
        c, GR.kernel_threads, flint_get_num_threads(), nth, class_nth, 1);
  if (!Shint[c].empty()) {
    GR.S_hint = Shint[c];
    GR.cond_hint = cond[c];
    GR.hint_kind = selection_kinds[c];
    GR.inverse_precision_hint = inverse_hints[c];
  }
#ifdef SIC_ENABLE_TEST_HOOKS
  if ((sic::support::resources::Count)c == defer_class && !injected_defer[c])
    GR.after_b0 = [&](const acb_mat_struct*, const std::vector<acb_struct>&, slong, slong, slong) {
      injected_defer[c] = 1;
      GR.workspace_limit = 1;
      sic::support::progressf("Newton resource fixture fired: class=%ld stage=after_B0\n", c);
    };
  if ((sic::support::resources::Count)c == defer_class && defer_entry && injected_defer[c] &&
      !injected_entry[c] && !Shint[c].empty()) {
    injected_entry[c] = 1;
    GR.workspace_limit = 1;
    sic::support::progressf("Newton resource fixture fired: class=%ld stage=entry hint_rows=%zu\n",
                            c, Shint[c].size());
  }
#endif
  ok[c] = GR.refine(V0, target, V, why[c]);
  // A low-precision inverse is only an accelerator. Reuse the selected
  // subsystem and retry with the seed-precision floor if needed.
  if (!ok[c] && GR.memory_refusal == phase_memory::Refusal::none && !GR.seed_inverse &&
      GR.selection_kind == GhostRefiner::SelectionKind::native && GR.has_complete_selection()) {
    GR.seed_inverse = true;
    GR.S_hint = GR.S;
    GR.cond_hint = GR.cond_est;
    GR.hint_kind = GR.selection_kind;
    GR.inverse_precision_hint = GR.inverse_precision_proposal;
    ok[c] = GR.refine(V0, target, V, why[c]);
  }
  const bool resource_refused = !ok[c] && GR.memory_refusal != phase_memory::Refusal::none;
  if (phase_admission) retained_memory[c] = GR.retained_memory;
  if (!ok[c])
    outcomes[c] = resource_refused ? phase_memory::ClassOutcome::resource_deferred
                                   : phase_memory::ClassOutcome::numerical_failure;
  private_requests[c] = resource_refused ? GR.required_workspace : 0;
  // An entry refusal resets the refiner, but cannot erase the existing
  // seed's useful hint bundle. Replace it only with a complete new one.
  if (!resource_refused || GR.has_complete_selection()) {
    cond[c] = GR.cond_est;
    ratio[c] = GR.kr_ratio;
    selection_kinds[c] = GR.selection_kind;
    inverse_hints[c] = GR.inverse_precision_proposal;
  }
  if (!ok[c] && GR.memory_refusal != phase_memory::Refusal::none && memory_audit)
    sic::support::progressf(
        "Newton resource refusal: class=%ld kind=%d required=%llu requirement_known=%d; "
        "numerical seed unchanged\n",
        c, (int)GR.memory_refusal, (unsigned long long)GR.required_workspace,
        (int)(GR.required_workspace != 0));
  if (!ok[c] && (!resource_refused || GR.has_complete_selection())) {
    if (GR.has_complete_selection())
      Shint[c] = GR.S;
    else
      Shint[c].clear();
  }
  if (resource_refused)
    seedfail[c] = 0;
  else if (!ok[c] && why[c].find("Krawczyk test on B0") != std::string::npos)
    seedfail[c] = 1;
  if (ok[c])
    for (long y = 1; y < d * d; ++y) {
      sic::support::Index x = (*group_).act(g, (*group_).ghost_point(y / d, y % d));
      acb_ptr dst = Tn->writable(x);
      acb_set_round(dst, &V[y], precision + 16);
      if ((*group_).chi[g] < 0) acb_neg(dst, dst);
    }
  if (ok[c] && GR.certified_root) completed_roots[c] = std::move(GR.certified_root);
}
}  // namespace sic::construct
