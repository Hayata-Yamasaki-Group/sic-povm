// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/reconstruction/ghost.hpp"

#include "exact/runtime/memory/product_tiles.hpp"
#include "sic/error.hpp"
#include "sic/support/parse_number.hpp"
#include "support/resources/platform_snapshot.hpp"

namespace sic::exact {
GhostRefiner::GhostRefiner(long d_, int chi_, long n_)
    : d(d_), chi(chi_), n(n_), pos(d_ * d_), ipos(d_ * d_) {
  for (long i = 0; i < d; ++i)
    for (long j = 0; j < d; ++j) {
      long yp = ((n * i) % d) * d + j;
      pos[i * d + j] = yp;
      ipos[yp] = i * d + j;
    }
}

bool GhostRefiner::refine(const std::vector<acb_struct>& V0in, slong target,
                          std::vector<acb_struct>& Vout, std::string& why) {
  release_vector_workers();
  struct ReleaseVectorWorkers {
    GhostRefiner* owner;
    ~ReleaseVectorWorkers() {
      owner->release_vector_workers();
      owner->vector_attempt.reset();
    }
  } release_workers{this};
  vector_attempt = VectorAttemptContext{sic::support::current_kernel_settings, workspace_limit,
                                        callback_workspace_limit};
  try {
    bool used_short_inverse = false;
    if (refine_impl(V0in, target, Vout, why, true, used_short_inverse)) return true;
    if (!used_short_inverse || memory_refusal != phase_memory::Refusal::none) return false;
    // A failed cheap proposal is never a reason to reject an otherwise
    // certifiable seed. Restart from that same certified input with the
    // original inverse precision and all original B0/B1/accuracy tests.
    if (sic::support::kernel_setting("SIC_PROGRESS"))
      sic::support::progressf("short inverse certificate failed; original Arb retry: %s\n",
                              why.c_str());
    release_vector_workers();
    return refine_impl(V0in, target, Vout, why, false, used_short_inverse);
  } catch (const phase_memory::ObserverException& error) {
    std::rethrow_exception(error.cause);
  } catch (const phase_memory::CapacityRefusal&) {
    return false;
  } catch (const phase_memory::StorageContractError& error) {
    memory_refusal = phase_memory::Refusal::storage_contract;
    required_workspace = 0;
    why = std::string("Newton storage contract unsupported: ") + error.what();
    return false;
  }
}

bool GhostRefiner::refine_impl(const std::vector<acb_struct>& V0in, slong target,
                               std::vector<acb_struct>& Vout, std::string& why, bool short_inverse,
                               bool& used_short_inverse) {
  certified_root.reset();
  memory_refusal = phase_memory::Refusal::none;
  required_workspace = 0;
  S.clear();
  cond_est = 0;
  selection_kind = SelectionKind::native;
  inverse_precision_proposal = 0;
  correction_attempts = correction_accepted = 0;
  high_newton_seconds = high_residual_seconds = high_correction_seconds = 0;
  correction_jacobian_seconds = correction_inner_seconds = 0;
  const char* ghost_debug = sic::support::kernel_setting("SIC_GHOST_DEBUG");
  int ghost_debug_level = ghost_debug ? 1 : 0;
  if (ghost_debug) {
    try {
      ghost_debug_level = sic::support::parse_number<int>(ghost_debug, "SIC_GHOST_DEBUG",
                                                          sic::ErrorKind::invalid_argument);
    } catch (const sic::Error&) {
      // Debug verbosity is advisory: malformed integers retain the presence-selected level 1.
    }
  }
  int passes = correction_passes;
  if (passes < 0 || passes > 2) throw std::invalid_argument("invalid Newton correction pass count");
  const long n2 = 2 * d - 2;
  constexpr slong kPrecisionCeiling = WORD_MAX / 16;
  slong accepted_modular_inverse_hint = 0;
  constexpr bool kColumnInverse = true;
  constexpr bool kThreadedProduct = true;
  bool parallel_reconstruction = true;  // May still fall back after helper admission refusal.
  auto precision_failure = [&]() {
    why = "Newton precision exceeds admitted class slot (quadrature fallback required)";
    return false;
  };
  if (target <= 0 || target > kPrecisionCeiling) return precision_failure();
  auto measured_precision = [&](double bits, slong margin, slong& result) {
    if (!std::isfinite(bits) || bits < 0 || bits > (double)(kPrecisionCeiling - margin))
      return precision_failure();
    const slong rounded = (slong)std::ceil(bits);
    if (rounded > kPrecisionCeiling - margin) return precision_failure();
    result = rounded + margin;
    return true;
  };
  if (&V0in == &Vout) {
    why = "Newton seed and output buffers must be distinct";
    return false;
  }
  auto release_vector = [](std::vector<acb_struct>& values) {
#if defined(__GLIBC__)
    const bool large = values.size() >= 1048576;
#endif
    for (auto& value : values) acb_clear(&value);
    std::vector<acb_struct>().swap(values);
#if defined(__GLIBC__)
    if (large) sic::support::resources::release_free_heap();
#endif
  };
  // A late failed reconstruction is not a completed output. The caller
  // may retry this same refiner, so do not retain another full seed grid.
  release_vector(Vout);
  slong source_bits = 64;
  for (size_t q = 1; q < V0in.size(); ++q) {
    if (!acb_is_finite(&V0in[q])) {
      why = "Newton seed has a nonfinite enclosure";
      return false;
    }
    source_bits = std::max(source_bits, (slong)acb_bits(&V0in[q]));
  }
  if (phase_admission) try {
#ifdef SIC_ENABLE_TEST_HOOKS
      if (before_operation) before_operation(failure_context, "source_forecast");
#endif
      phase_memory::CopySource source;
      for (const auto& value : V0in) source.observe(&value);
      phase_memory::Options options;
      options.workers = std::max(1, kernel_threads);
      options.modular = true;
      options.column_inverse = kColumnInverse;
      options.threaded_product = kThreadedProduct;
      options.parallel_reconstruction = parallel_reconstruction;
      options.refined_correction = passes > 0;
      options.reference = false;
      options.native_scalar_bytes = sizeof(SelectionWide);
      options.native_complex_bytes = sizeof(ComplexBall<SelectionWide>);
      const auto proposal = phase_memory::propose(
          d, target, matrix_precision_hint > 0 ? matrix_precision_hint : target, source, options);
      if (retained_memory)
        phase_memory::merge(*retained_memory, proposal);
      else
        retained_memory = proposal;
      const auto actual = phase_memory::inspect_storage(V0in);
      retained_memory->source_storage_bytes =
          std::max(retained_memory->source_storage_bytes, actual.bytes());
    } catch (const std::overflow_error&) {
      memory_refusal = phase_memory::Refusal::class_slot;
      required_workspace = std::numeric_limits<sic::support::resources::Bytes>::max();
      why = "Newton source forecast is unrepresentable (quadrature fallback required)";
      return false;
    }
  auto requested_phase = phase_memory::Phase::roots;
  std::optional<phase_memory::Trace> trace;
  struct RestoreTrace {
    phase_memory::Trace*& slot;
    phase_memory::Trace* saved;
    ~RestoreTrace() { slot = saved; }
  } restore_trace{memory_trace, memory_trace};
  memory_trace = nullptr;
  if (memory_observer || phase_admission) {
    phase_memory::Ceilings proposal;
    proposal.dimension = d;
    proposal.source_bits = source_bits;
    proposal.selection_bits = std::max<slong>(160, source_bits);
    proposal.inverse_bits = matrix_precision_hint > 0 ? matrix_precision_hint : target;
    proposal.stored_matrix_bits = proposal.inverse_bits;
    proposal.modular = true;
    proposal.high_bits = sic::support::resources::add(
        target, proposal.modular
                    ? std::max<sic::support::resources::BitCount>(
                          256, sic::support::resources::add(proposal.inverse_bits, 512))
                    : 256);
    proposal.candidate_rows = std::min<sic::support::resources::Count>(8 * n2, d * d - 1);
    proposal.workers = std::max(1, kernel_threads);
    proposal.column_inverse = kColumnInverse;
    proposal.threaded_product = kThreadedProduct;
    proposal.parallel_reconstruction = parallel_reconstruction;
    proposal.refined_correction = passes > 0;
    proposal.native_scalar_bytes = sizeof(SelectionWide);
    proposal.native_complex_bytes = sizeof(ComplexBall<SelectionWide>);
    if (phase_admission) proposal = *retained_memory;
    trace.emplace(
        proposal, workspace_limit,
        [this](const phase_memory::Event& event) {
          if (memory_observer) try {
              memory_observer(event);
            } catch (...) {
              throw phase_memory::ObserverException{std::current_exception()};
            }
          if (phase_admission && (!event.forecast_valid || !event.storage_fits()))
            throw phase_memory::StorageContractError(event.object);
        },
        phase_admission ? &*retained_memory : nullptr);
    memory_trace = &*trace;
    memory_trace->source(V0in);
  }
  auto workspace_usage = [&](sic::support::resources::Count rows,
                             sic::support::resources::BitCount matrix_bits,
                             sic::support::resources::BitCount vector_bits, bool selection,
                             bool modular = false) {
    const auto cells = sic::support::resources::mul(rows, n2);
    auto matrix_bytes =
        selection
            ? sic::support::resources::add(
                  sic::support::resources::mul(cells,
                                               sic::support::resources::complex_bytes(matrix_bits)),
                  modular
                      ? modular_row_workspace(rows, n2, std::max(1, kernel_threads), matrix_bits)
                      : sic::support::resources::mul(cells, 32))
            : sic::support::resources::mul(
                  cells, sic::support::resources::mul(
                             8, sic::support::resources::complex_bytes(
                                    sic::support::resources::newton_matrix_bits(matrix_bits))));
    if (!selection && kColumnInverse)
      matrix_bytes = sic::support::resources::add(
          matrix_bytes, sic::support::resources::inverse_column_scratch(
                            n2, std::max(1, kernel_threads), matrix_bits));
    if (!selection && kThreadedProduct)
      matrix_bytes = sic::support::resources::add(
          matrix_bytes, sic::exact::product_tiles::scratch(
                            n2, std::max(1, kernel_threads),
                            sic::support::resources::newton_matrix_bits(matrix_bits)));
    const auto vectors = sic::support::resources::mul(
        sic::support::resources::mul(d, d),
        sic::support::resources::mul(
            4, sic::support::resources::complex_bytes(std::max<sic::support::resources::BitCount>(
                   {(sic::support::resources::BitCount)target, vector_bits,
                    (sic::support::resources::BitCount)source_bits}))));
    return sic::support::resources::add(512 * sic::support::resources::kMib,
                                        sic::support::resources::add(matrix_bytes, vectors));
  };
  auto workspace_fits =
      [&](sic::support::resources::Count rows, sic::support::resources::BitCount matrix_bits,
          sic::support::resources::BitCount vector_bits, bool selection, bool modular = false) {
        try {
#ifdef SIC_ENABLE_TEST_HOOKS
          if (before_operation) before_operation(failure_context, "workspace_forecast");
#endif
          if (phase_admission) {
            auto& c = *retained_memory;
            if (selection) {
              if (modular != c.modular)
                throw std::invalid_argument("Newton selection memory configuration changed");
              c.candidate_rows = std::max(c.candidate_rows, rows);
              c.selection_bits = std::max(c.selection_bits, matrix_bits);
            } else {
              c.inverse_bits = std::max(c.inverse_bits, matrix_bits);
              c.stored_matrix_bits = std::max(c.stored_matrix_bits, matrix_bits);
            }
            c.high_bits = std::max(c.high_bits, vector_bits);
            if (memory_trace) {
              memory_trace->callback_slot(workspace_limit);
              memory_trace->enter(requested_phase);
            }
            const auto needed = phase_memory::requirement(c, memory_publication);
            if (needed.private_peak <= workspace_limit &&
                needed.callback_peak <= callback_workspace_limit)
              return true;
            required_workspace = std::max(required_workspace, needed.private_peak);
            memory_refusal = phase_memory::Refusal::class_slot;
            why = "Newton phase workspace exceeds owned callback (serial readmission required)";
            return false;
          }
          const auto base = workspace_usage(rows, matrix_bits, vector_bits, selection, modular);
          if (!selection && parallel_reconstruction && kernel_threads > 1 &&
              sic::support::resources::add(
                  base, sic::support::resources::ghost_reconstruction_scratch(
                            d,
                            std::max<sic::support::resources::BitCount>(
                                {(sic::support::resources::BitCount)target, vector_bits,
                                 (sic::support::resources::BitCount)source_bits}),
                            kernel_threads)) > workspace_limit)
            parallel_reconstruction =
                false;  // retain the original refinement when only row scratch refuses
          if (base <= workspace_limit) return true;
          required_workspace = std::max(required_workspace, base);
        } catch (const std::overflow_error&) {
          // An unrepresentable forecast also refuses the accelerator. Do not
          // throw out of a class worker and bypass its quadrature fallback.
          required_workspace = std::numeric_limits<sic::support::resources::Bytes>::max();
        }
        memory_refusal = phase_memory::Refusal::class_slot;
        why = "Newton workspace exceeds admitted class slot (quadrature fallback required)";
        return false;
      };
  auto guarded_precision = [&](slong matrix_bits, slong bits) {
    if (bits <= 0 || bits > kPrecisionCeiling - 64) return precision_failure();
    return workspace_fits(n2, matrix_bits, (sic::support::resources::BitCount)bits + 64, false);
  };
  if (!workspace_fits(n2, matrix_precision_hint > 0 ? matrix_precision_hint : target, target,
                      false))
    return false;
  if (V0in.size() != (size_t)(d * d)) {
    why = "Newton seed grid has the wrong size";
    return false;
  }
  auto clk = []() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
  };
  double tt0 = clk();
  std::vector<double> tmarks;
  std::vector<acb_struct> V0(d * d);
  for (auto& x : V0) acb_init(&x);  // by coefficient position
  for (long y = 1; y < d * d; ++y) acb_set(&V0[pos[y]], &V0in[y]);
  struct Clr {
    std::vector<acb_struct>& v;
    ~Clr() {
      for (auto& x : v) acb_clear(&x);
    }
  } clr{V0};
  const slong p0 = source_bits;
  if (memory_trace) memory_trace->enter(phase_memory::Phase::roots);
  // 1. exact K_y: the unique 4d^2-th root of unity in the ball V0_y V0_{-y}
  Kexp.assign(d * d, 0);
  phase_memory::BallStorage root_storage;
  {
    sic::support::flint::Acb pr, r;
    const long M = 4 * d * d;
    RootUnityIsolation isolation((ulong)M, p0);
    std::map<long, acb_struct> roots;
    struct ClearRoots {
      std::map<long, acb_struct>& r;
      ~ClearRoots() {
        for (auto& kv : r) acb_clear(&kv.second);
      }
    } clear_roots{roots};
    for (long y = 1; y < d * d; ++y) {
      acb_mul(pr, &V0[y], &V0[neg(y)], p0);
      double ang = std::atan2(arf_get_d(arb_midref(acb_imagref(pr)), ARF_RND_NEAR),
                              arf_get_d(arb_midref(acb_realref(pr)), ARF_RND_NEAR));
      long e = std::lround(ang / (2 * M_PI) * M);
      e = ((e % M) + M) % M;
      auto [it, inserted] = roots.try_emplace(e);
      if (inserted) {
        acb_init(&it->second);
        root_k(&it->second, p0, e);
      }
      acb_set(r, &it->second);
      // The angle is a guess. This strict Arb/mag comparison includes both
      // rectangles' widths and identifies the known root uniquely.
      if (!isolation.matches(pr, r)) {
        why = "K_y: unique root not certified";
        return false;
      }
      Kexp[y] = e;
    }
    if (memory_trace) {
      root_storage.values = roots.size();
      root_storage.header_bytes = sic::support::resources::mul(roots.size(), sizeof(acb_struct));
      for (const auto& root : roots) root_storage.observe(&root.second);
    }
  }
  if (memory_trace) memory_trace->cache("root-map scalar payload", root_storage, p0);
  tmarks.push_back(clk() - tt0);
  // 2. X from V0, normalisation index, ball Z0 of z*
  if (memory_trace) memory_trace->enter(phase_memory::Phase::projection);
  Ctx C0(d, chi, p0);
  std::vector<acb_struct> A0(d * d), X(d * d);
  for (auto& x : A0) acb_init(&x);
  for (auto& x : X) acb_init(&x);
  struct ClearPair {
    std::vector<acb_struct>& a;
    std::vector<acb_struct>& b;
    ~ClearPair() {
      for (auto& x : a) acb_clear(&x);
      for (auto& x : b) acb_clear(&x);
    }
  } clear_matrices{A0, X};
  {
    sic::support::flint::Acb c;
    acb_one(&A0[0]);
    for (long y = 1; y < d * d; ++y) {
      coef(c, C0, y);
      acb_div(&A0[y], &V0[y], c, p0);
    }  // A = V / coef
    // X[a][a+j] = (1/d) sum_i A_ij w^{ia}: one inverse DFT per j
    acb_ptr col = _acb_vec_init(d), res = _acb_vec_init(d);
    acb_dft_pre_t plan;
    acb_dft_precomp_init(plan, d, p0);
    for (long j = 0; j < d; ++j) {
      for (long i = 0; i < d; ++i) acb_set(col + i, &A0[i * d + j]);
      acb_dft_inverse_precomp(res, col, plan, p0);
      for (long a = 0; a < d; ++a) acb_set(&X[a * d + (a + j) % d], res + a);
    }
    _acb_vec_clear(col, d);
    _acb_vec_clear(res, d);
    acb_dft_precomp_clear(plan);
    double best = -1;
    for (long a = 0; a < d; ++a) {
      acb_abs(acb_realref(c), &X[a * d + a], 64);
      double m = arf_get_d(arb_midref(acb_realref(c)), ARF_RND_NEAR);
      if (m > best) {
        best = m;
        k = (int)a;
      }
    }
  }
  if (ghost_debug && ghost_debug_level >= 2) {  // idempotency and trace of X (O(d^3))
    double mx = 0;
    sic::support::flint::Acb t, a;
    for (long r = 0; r < d; ++r)
      for (long c = 0; c < d; ++c) {
        acb_zero(t);
        for (long m = 0; m < d; ++m) acb_addmul(t, &X[r * d + m], &X[m * d + c], p0);
        acb_sub(t, t, &X[r * d + c], p0);
        acb_abs(acb_realref(a), t, 64);
        mx = std::max(mx, arf_get_d(arb_midref(acb_realref(a)), ARF_RND_NEAR));
      }
    acb_zero(t);
    for (long r = 0; r < d; ++r) acb_add(t, t, &X[r * d + r], p0);
    fprintf(stderr, "  [ghost chi=%d n=%ld] |X^2 - X| = %.3g, tr X = %.6f%+.6fi\n", chi, n, mx,
            arf_get_d(arb_midref(acb_realref(t)), ARF_RND_NEAR),
            arf_get_d(arb_midref(acb_imagref(t)), ARF_RND_NEAR));
  }
  std::vector<acb_struct> Z0(n2);
  for (auto& x : Z0) acb_init(&x);
  Clr clear_seed{Z0};
  {
    sic::support::flint::Acb xkk;
    acb_set(xkk, &X[k * d + k]);
    if (acb_contains_zero(xkk)) {
      why = "X_kk not certified nonzero";
      return false;
    }
    for (long a = 0; a < d; ++a) {
      if (a == k) continue;
      acb_div(&Z0[zidx(a)], &X[a * d + k], xkk, p0);          // v_a = X[a][k] / X[k][k]
      acb_div(&Z0[d - 1 + zidx(a)], &X[k * d + a], xkk, p0);  // u_a = X[k][a] / X[k][k]
    }
  }
  if (memory_trace) {
    memory_trace->vector("projection A0", A0, p0);
    memory_trace->vector("projection X", X, p0);
  }
  release_vector(A0);
  release_vector(X);
  tmarks.push_back(clk() - tt0);
  // 3. Square subsystem by modular rank selection on dyadic Jacobian midpoints.
  // Oversampled candidate sets may refuse; the caller retains the certified quadrature fallback.
  if (S_hint.size() == (size_t)n2) {
    if (hint_kind == SelectionKind::native && (!std::isfinite(cond_hint) || cond_hint <= 0))
      return precision_failure();
    if (hint_kind == SelectionKind::modular_midpoint &&
        (inverse_precision_hint < 0 || inverse_precision_hint > kPrecisionCeiling))
      return precision_failure();
    S = S_hint;
    selection_kind = hint_kind;
    if (hint_kind == SelectionKind::modular_midpoint)
      accepted_modular_inverse_hint = inverse_precision_hint;
    cond_est =
        hint_kind == SelectionKind::native ? cond_hint : std::numeric_limits<double>::quiet_NaN();
  } else {
    std::vector<long> half;
    std::vector<char> seen(d * d, 0);
    for (long y = 1; y < d * d; ++y) {
      if (seen[y]) continue;
      seen[y] = seen[neg(y)] = 1;
      half.push_back(y);
    }
    std::mt19937 rng(12345u + (unsigned)n + 7u * (unsigned)(chi + 1));
    std::vector<long> cand = half;
    std::shuffle(cand.begin(), cand.end(), rng);
    bool selected = false;
    {
      for (int pass = 0; pass < 2 && !selected; ++pass) {
        const long want = (pass == 0 ? 3 : 8) * n2;
        if (pass && (long)cand.size() <= 3 * n2) break;
        std::vector<long> rowsy(cand.begin(), cand.begin() + std::min<long>(cand.size(), want));
        const long R = rowsy.size();
        requested_phase = phase_memory::Phase::selection;
        if (memory_trace && !phase_admission) memory_trace->selection(R, p0, true);
        if (!workspace_fits(R, p0, target, true, true)) return false;
        sic::support::ComplexBallVector midpoint(n2);
        for (size_t q = 0; q < midpoint.size(); ++q) acb_get_mid(midpoint[q], &Z0[q]);
        const bool verbose = sic::support::kernel_setting("SIC_PROGRESS") || ghost_debug;
        if (!selection_normalization_resolved(midpoint.v, p0, verbose)) {
          why = "modular selection denominator unresolved at admitted precision";
          return false;
        }
        sic::support::flint::AcbMat J(R, n2);
        Ctx context(d, chi, p0);
        jacobian(J, context, midpoint.v, rowsy);
        // Modular selection owns a separate row team.
        release_vector_workers();
        if (memory_trace) memory_trace->matrix("selection J", J, p0, false);
        for (ulong prime : {UWORD(2013265921), UWORD(1811939329)}) {
          ModularRowSelection result;
          try {
#ifdef SIC_ENABLE_TEST_HOOKS
            if (before_operation) before_operation(failure_context, "modular_selection");
#endif
            result = select_modular_rows(J, rowsy, std::max(1, kernel_threads), p0, prime, false,
                                         workspace_limit);
          } catch (const sic::support::resources::AdmissionError& error) {
            memory_refusal = phase_memory::Refusal::helper;
            why = std::string("modular selection resource refusal: ") + error.what();
            return false;
          } catch (const std::system_error&) {
            memory_refusal = phase_memory::Refusal::helper;
            why = "modular selection worker startup refused";
            return false;
          }
          if (verbose)
            sic::support::progressf(
                "modular midpoint selection: rows=%ld rank=%ld/%ld prime=%lu finite=%d "
                "conditioning=unknown\n",
                R, result.rank, n2, result.prime, result.finite);
          if (!result.finite) {
            why = "modular selection has nonfinite input";
            return false;
          }
          if (result.rows.size() == (size_t)n2) {
            S = std::move(result.rows);
            selection_kind = SelectionKind::modular_midpoint;
            cond_est = std::numeric_limits<double>::quiet_NaN();
            selected = true;
            break;
          }
        }
      }
    }
    if (!selected) {
      why = "modular midpoint selection did not find full rank";
      return false;
    }
  }
  tmarks.push_back(clk() - tt0);
  // 4. approximate inverse Y of J_S at the centre (low precision)
  // The cond estimate is for the equilibrated system; the unknowns differ in size by many orders
  // of magnitude, and what the Krawczyk test sees is the componentwise amplification chi = max_r
  // (|Y| |J| w)_r / w_r for the box shape w (the seed radii). Y is computed at pl0 = log2 cond +
  // 72 bits, chi is measured (two O(n^2) matvecs), and if pl = log2 chi + 64 exceeds pl0, Y is
  // recomputed at pl. The seed-precision floor is enabled on retry/reference runs. A
  // cheaper inverse is accepted only if the same Krawczyk tests pass.
  const bool modular = selection_kind == SelectionKind::modular_midpoint;
  if (!modular && !std::isfinite(cond_est)) return precision_failure();
  const double log2cond =
      modular ? std::numeric_limits<double>::quiet_NaN() : std::log2(std::max(1.0, cond_est));
  slong condition_bits = 0;
  if (!modular && !measured_precision(log2cond, 72, condition_bits)) return false;
  slong pl = modular ? std::max<slong>({128, p0, accepted_modular_inverse_hint})
                     : std::max<slong>(std::max<slong>(seed_inverse && p0 > 200 ? p0 : 0, 128),
                                       condition_bits);
  inverse_precision_proposal = pl;
  sic::support::flint::AcbMat Y(n2, n2);
  std::vector<acb_struct> z(n2);
  for (auto& x : z) acb_init(&x);
  for (long q = 0; q < n2; ++q) acb_get_mid(&z[q], &Z0[q]);
  struct ClrM {
    acb_mat_struct* m;
    std::vector<acb_struct>& z;
    ~ClrM() {
      for (auto& x : z) acb_clear(&x);
    }
  } clrm{Y, z};
  double log2chi = 0;
  auto build_inverse = [&](bool measure_amplification) {
    const int attempts = modular ? 3 : 1;
    for (int attempt = 0; attempt < attempts; ++attempt) {
      inverse_precision_proposal = pl;
      if (pl <= 0 || pl > kPrecisionCeiling) return precision_failure();
      requested_phase = phase_memory::Phase::inverse;
      if (memory_trace && !phase_admission) memory_trace->inverse(pl);
      if (!workspace_fits(n2, pl, target, false)) return false;
      if (modular && (sic::support::kernel_setting("SIC_PROGRESS") || ghost_debug))
        sic::support::progressf(
            "modular inverse proposal: bits=%ld attempt=%d conditioning=unknown\n", (long)pl,
            attempt + 1);
      Ctx Cl(d, chi, pl);
      sic::support::flint::AcbMat J(n2, n2);
      jacobian(J, Cl, z, S);
      // The inverse owns a separate team after these rows are complete.
      release_vector_workers();
      for (long r = 0; r < n2; ++r)
        for (long c = 0; c < n2; ++c) acb_get_mid(acb_mat_entry(J, r, c), acb_mat_entry(J, r, c));
      if (memory_trace) memory_trace->matrix("inverse J", J, pl);
      int inverse_ok;
      try {
#ifdef SIC_ENABLE_TEST_HOOKS
        if (before_operation) before_operation(failure_context, "inverse");
#endif
        // Fixed dimension/declared-precision policy, independent of resources
        // and threads. Larger proposals retain Arb's range and precision.
        const auto arithmetic = short_inverse && n2 >= 254 && pl <= 144
                                    ? PointInverseArithmetic::double_double
                                    : PointInverseArithmetic::arf;
        used_short_inverse |= arithmetic == PointInverseArithmetic::double_double;
        inverse_ok = parallel_point_inverse(
            Y, J, pl, std::max(1, kernel_threads), workspace_limit,
            sic::support::kernel_setting("SIC_PROGRESS") || ghost_debug, {}, arithmetic);
      } catch (const sic::support::resources::AdmissionError& error) {
        memory_refusal = phase_memory::Refusal::helper;
        why = std::string("column inverse resource refusal: ") + error.what();
        return false;
      }
      if (!inverse_ok) {
        if (attempt + 1 == attempts) {
          why = "approximate inverse failed";
          return false;
        }
        if (pl > kPrecisionCeiling - 128) return precision_failure();
        pl += 128;
        continue;
      }
      if (memory_trace) memory_trace->matrix("inverse Y", Y, pl);
      if (!measure_amplification) return true;
      // chi in log space (entries may exceed the double range)
      std::vector<double> lw(n2), lt(n2, -1e300), lu(n2, -1e300);
      auto lse = [](double a, double b) {
        return a < b ? b + std::log2(1 + std::exp2(a - b)) : a + std::log2(1 + std::exp2(b - a));
      };
      sic::support::flint::Mag mg;
      for (long q = 0; q < n2; ++q) {
        mag_set(mg, arb_radref(acb_realref(&Z0[q])));
        mag_max(mg, mg, arb_radref(acb_imagref(&Z0[q])));
        double a = mag_is_zero(mg) ? -1e300 : mag_get_d_log2_approx(mg);
        acb_get_mag(mg, &z[q]);
        double b = mag_is_zero(mg) ? -1e300 : mag_get_d_log2_approx(mg) - (double)p0;
        lw[q] = std::max(std::max(a, b), -1e6);
      }
      for (long r = 0; r < n2; ++r)
        for (long c = 0; c < n2; ++c) {
          acb_get_mag(mg, acb_mat_entry(J, r, c));
          if (!mag_is_zero(mg)) lt[r] = lse(lt[r], mag_get_d_log2_approx(mg) + lw[c]);
        }
      for (long r = 0; r < n2; ++r)
        for (long c = 0; c < n2; ++c) {
          acb_get_mag(mg, acb_mat_entry(Y, r, c));
          if (!mag_is_zero(mg)) lu[r] = lse(lu[r], mag_get_d_log2_approx(mg) + lt[c]);
        }

      log2chi = 0;
      for (long r = 0; r < n2; ++r) log2chi = std::max(log2chi, lu[r] - lw[r]);
      return true;
    }
    return false;
  };
  for (int att = 0; att < 2; ++att) {
    if (!build_inverse(att == 0)) return false;
    if (att == 1) break;
    slong pc;
    if (!measured_precision(log2chi, 64, pc)) return false;
    if (pc <= pl) break;
    pl = pc;
  }
  tmarks.push_back(clk() - tt0);
  // largest |log2 V_y|: relative accuracy is lost on small values
  double lsize = 0;
  for (long y = 1; y < d * d; ++y) {
    sic::support::flint::Mag m;
    acb_get_mag(m, &V0[y]);
    lsize = std::max(lsize, std::fabs(mag_get_d_log2_approx(m)));
  }
  if (memory_trace) memory_trace->vector("reordered seed", V0, p0);
  release_vector(V0);
  slong extra_bits;
  // Unknown modular conditioning has a separate empirical precision
  // proposal, never a fabricated condition estimate. Certificates decide.
  const double guard_proposal =
      modular ? std::max((double)pl, log2chi) : std::max(log2cond, log2chi);
  if (!measured_precision(guard_proposal + lsize, 56, extra_bits)) return false;
  if (target > kPrecisionCeiling - extra_bits) return precision_failure();
  slong p_hi = target + extra_bits;
  requested_phase = phase_memory::Phase::high_newton;
  if (memory_trace && !phase_admission) {
    memory_trace->callback_slot(workspace_limit);
    memory_trace->high(std::max(pl, p_hi) + 64);
    memory_trace->enter(phase_memory::Phase::high_newton);
  }
  if (!guarded_precision(pl, std::max(pl, p_hi))) return false;
  std::vector<acb_struct> F(n2), dz(n2);
  for (auto& x : F) acb_init(&x);
  for (auto& x : dz) acb_init(&x);
  struct ClrV {
    std::vector<acb_struct>& a;
    std::vector<acb_struct>& b;
    ~ClrV() {
      for (auto& x : a) acb_clear(&x);
      for (auto& x : b) acb_clear(&x);
    }
  } clrv{F, dz};
  // 5. simplified Newton z <- z - Y F(z), precision increasing from p_from to p_to
  // Constants (roots of unity, and the K_y of the equations S) are computed once at the top
  // precision and rounded for each Newton step: e(k/d), e(k/2d) and 510 values e(K/(4d^2)) at
  // ~1800 bits cost a third of a step otherwise.
  std::unique_ptr<Ctx> Ctop;
  std::vector<acb_struct> Ktop;
  struct ClrK {
    std::vector<acb_struct>& v;
    ~ClrK() {
      for (auto& x : v) acb_clear(&x);
    }
  } clrk{Ktop};
  auto ctx_at = [&](slong p) -> std::unique_ptr<Ctx> {
    if (phase_admission && !guarded_precision(pl, std::max(p, p_hi)))
      throw phase_memory::CapacityRefusal{};
    if (!Ctop || Ctop->p < p) {
      const slong pt = std::max<slong>(p, p_hi) + 64;
      Ctop = std::make_unique<Ctx>(d, chi, pt);
      for (auto& x : Ktop) acb_clear(&x);
      Ktop.assign(S.size(), acb_struct());
      for (size_t r = 0; r < S.size(); ++r) {
        acb_init(&Ktop[r]);
        root_k(&Ktop[r], pt, Kexp[S[r]]);
      }
      Ctop->Kc = &Ktop;
      if (memory_trace) {
        memory_trace->high(pt);
        memory_trace->vector("top d roots", Ctop->wd, pt);
        memory_trace->vector("top 2d roots", Ctop->w2d, pt);
        memory_trace->vector("equation roots", Ktop, pt);
      }
    }
    return std::make_unique<Ctx>(*Ctop, p);
  };
  auto newton = [&](slong p_from, slong p_to, bool allow_refinement = false) {
    slong p = p_from;
    double prev_l2 = 1e300, gain = 16;
    int stall = 0;
    int previous_mode = -1;
    for (int it = 0; it < 200; ++it) {
      const auto residual_start = clk();
      auto Cp = ctx_at(p);
      residual(F, *Cp, z);
      if (allow_refinement) high_residual_seconds += clk() - residual_start;
      // the correction Y F only needs ~pl + 64 relative bits (F is tiny; the update of z keeps
      // precision p)
      const slong q = std::min<slong>(p, pl + 64);
      const auto correction_start = clk();
      sic::support::ComplexBallVector proposal;
      bool refined = false;
      if (allow_refinement && passes > 0 && pl <= kPrecisionCeiling - 512) {
        ++correction_attempts;
        sic::support::resources::Bytes used = workspace_limit;
        try {
#ifdef SIC_ENABLE_TEST_HOOKS
          if (before_operation) before_operation(failure_context, "correction_forecast");
#endif
          used = phase_admission
                     ? phase_memory::correction_baseline(phase_memory::forecast(*retained_memory))
                     : workspace_usage(
                           n2, pl, sic::support::resources::add(std::max<slong>({pl, p, p_hi}), 64),
                           false);
        } catch (const std::overflow_error&) {
        }
        if (used < workspace_limit)
          refined = refined_correction(proposal, Y, F, z, *Ctop, std::min<slong>(p, pl + 512),
                                       passes, workspace_limit - used, &correction_jacobian_seconds,
                                       &correction_inner_seconds);
        if (refined) ++correction_accepted;
      }
      const double l2 = refined ? commit_correction(proposal, z, p) : correction(Y, F, z, dz, p, q);
      if (allow_refinement) high_correction_seconds += clk() - correction_start;
      const int mode = refined ? passes : 0;
      if (previous_mode >= 0 && mode != previous_mode) {
        prev_l2 = 1e300;
        gain = 16;
        stall = 0;
      }
      previous_mode = mode;
      newton_iters += 1;
      if (sic::support::kernel_setting("SIC_GHOST_TRACE_ITERATIONS"))
        fprintf(stderr, "    newton it %d p %ld rel update 2^%.1f\n", newton_iters, (long)p, l2);
      // Precision schedule. The simplified Newton step contracts the error by ~2^-gain (measured
      // from successive updates), so after this step the iterate has ~ -l2 + gain correct bits
      // and the next residual must be resolved to ~ -l2 + 2 gain bits: the precision follows that
      // (plus 64 guard bits) instead of doubling, which saves most of the steps at the top
      // precision. At the noise floor (stagnation) it is doubled.
      const bool stagnant = l2 > prev_l2 - 8;
      if (!stagnant && prev_l2 < 1e299) gain = std::max(16.0, prev_l2 - l2);
      prev_l2 = std::min(prev_l2, l2);
      if (p >= p_to && (l2 < -(double)p_to + 4 || (stagnant && ++stall >= 2))) break;
      if (p < p_to) {
        slong want = (slong)std::ceil(std::min(1e9, -l2 + 2 * gain + 64 + log2chi));
        if (stagnant)
          want = std::max<slong>(want, 2 * p);
        else if (l2 < -(double)p + 24)
          want = std::max<slong>(want, p + (slong)(2 * gain) + 64);
        if (want > p) {
          p = std::min<slong>(p_to, want);
          if (stagnant) prev_l2 = 1e300;
        }
      }
    }
  };
  struct MagVec {
    std::vector<mag_struct> v;
    explicit MagVec(long n = 0) : v(n) {
      for (auto& x : v) mag_init(&x);
    }
    ~MagVec() {
      for (auto& x : v) mag_clear(&x);
    }
    mag_ptr operator[](long i) { return &v[i]; }
  };
  // Krawczyk test on the box zc +- rho at precision ph: K = z~ - Y F(z~) + (I - Y J(B))(B - z~)
  // with z~ = zc. If Mreuse is given (an enclosure of I - Y J over a box containing B), it is
  // used instead of J(B).
  sic::support::flint::AcbMat M0(n2, n2);
  // precision of the Krawczyk matrix I - Y J(B): it only has to be comfortably contractive on the
  // (tiny) radii, so it is first tried at about log2 chi + 48 bits instead of pl, and at
  // pl if that fails
  slong pm = pl;
  bool product_refused = false;
  auto krawczyk = [&](const std::vector<acb_struct>& zc, MagVec& rho, MagVec& out_dev, slong ph,
                      bool reuse, bool store) -> bool {
    // Tile-produced Mloc limbs remain live after the tile workers join. Trim
    // only after this call's matrix guards have cleared them; trimming at the
    // worker barrier alone leaves their arena pages pinned into later phases.
    struct ReleaseProductPages {
      bool tiled;
      ~ReleaseProductPages() {
        if (tiled) sic::support::resources::release_free_heap();
      }
    } release_product_pages{!reuse && n2 > product_tiles::kSmallMatrix};
    requested_phase = phase_memory::Phase::certification;
    if (memory_trace && !phase_admission) {
      memory_trace->high(ph + 64);
      memory_trace->enter(phase_memory::Phase::certification);
    }
    auto Chp = ctx_at(ph);
    residual(F, *Chp, zc);
    release_vector_workers();
    std::vector<acb_struct> YF(n2);
    for (auto& x : YF) acb_init(&x);
    Clr clear_yf{YF};
    for (long r = 0; r < n2; ++r)
      acb_dot(&YF[r], NULL, 0, acb_mat_entry(Y, r, 0), 1, F.data(), 1, n2, ph);
    sic::support::flint::AcbMat Mloc(n2, n2);
    if (memory_trace) memory_trace->matrix("empty Mloc", Mloc, pl);
    acb_mat_struct* M = reuse ? M0.get() : Mloc.get();
    if (!reuse) {
      {
        // J(B) at low precision over the box B = z~ +- rho (real and imaginary parts)
        std::vector<acb_struct> zb(n2);
        for (auto& x : zb) acb_init(&x);
        Clr clear_zb{zb};
        for (long q = 0; q < n2; ++q) {
          acb_set_round(&zb[q], &zc[q], pm);
          arb_add_error_mag(acb_realref(&zb[q]), rho[q]);
          arb_add_error_mag(acb_imagref(&zb[q]), rho[q]);
        }
        auto Clp = ctx_at(pm);
        sic::support::flint::AcbMat J(n2, n2);
        jacobian(J, *Clp, zb, S);
        release_vector_workers();
        if (memory_trace) memory_trace->matrix("Krawczyk J", J, pm);
        // Only bypass a provably impossible first low-precision product.
        // The caller immediately retries at pl and still populates M0
        // before any reshaping or B1 reuse. Never skip that final attempt.
        if (store && pm < pl) {
          long row = -1;
          try {
#ifdef SIC_ENABLE_TEST_HOOKS
            if (before_operation) before_operation(failure_context, "radius_gate");
#endif
            sic::support::resources::Budget().require_additional(
                "Krawczyk radius-gate vectors",
                sic::support::resources::mul(2 * n2 + 5, sizeof(mag_struct) + 64));
            row = sic::exact::krawczyk_exclusion::obstructed_row(Y, J, rho.v.data());
          } catch (const sic::support::resources::AdmissionError&) {
            // An optional shortcut never refuses the original product.
          }
          if (row >= 0) {
            if (sic::support::kernel_setting("SIC_PROGRESS"))
              sic::support::progressf(
                  "Krawczyk radius obstruction: row=%ld bits=%ld retry_bits=%ld; skipping "
                  "product\n",
                  row, (long)pm, (long)pl);
            return false;
          }
        }
        if (kThreadedProduct) {
          try {
#ifdef SIC_ENABLE_TEST_HOOKS
            if (before_operation) before_operation(failure_context, "ball_product");
#endif
            if (!parallel_ball_product(M, Y, J, pm, std::max(1, kernel_threads), workspace_limit,
                                       sic::support::kernel_setting("SIC_PROGRESS") != nullptr)) {
              why =
                  "Krawczyk product has a nonfinite input or output (quadrature fallback "
                  "required)";
              product_refused = true;
              return false;
            }
          } catch (const sic::support::resources::AdmissionError& error) {
            memory_refusal = phase_memory::Refusal::helper;
            why = std::string("Krawczyk product refused (quadrature fallback required): ") +
                  error.what();
            product_refused = true;
            return false;
          }
        }
        acb_mat_neg(M, M);
        for (long r = 0; r < n2; ++r)
          acb_add_ui(acb_mat_entry(M, r, r), acb_mat_entry(M, r, r), 1, pm);
        if (memory_trace) memory_trace->matrix("Krawczyk Mloc", M, pm);
      }  // Release J and its box before allocating stored M0 limb payloads.
      if (store) acb_mat_set(M0, M);
    }
    bool ok = true;
    sic::support::flint::Mag acc, m, tq, rs;
    for (long r = 0; r < n2; ++r) {
      // |(I - Y J(B)) (B - z~)|_r <= 2 sum_c |M_rc| rho_c  (2 >= sqrt 2: the box has half-side
      // rho in both parts)
      mag_zero(acc);
      mag_zero(rs);
      for (long c = 0; c < n2; ++c) {
        acb_get_mag(m, acb_mat_entry(M, r, c));
        mag_addmul(acc, m, rho[c]);
        mag_add(rs, rs, m);
      }
      mag_mul_2exp_si(acc, acc, 1);
      acb_get_mag(out_dev[r], &YF[r]);  // centre deviation |Y F(z~)|_r
      mag_add(tq, out_dev[r], acc);
      double ratio = mag_get_d_log2_approx(tq) - mag_get_d_log2_approx(rho[r]);
      kr_ratio = std::max(kr_ratio, ratio);
      kr_mnorm = std::max(kr_mnorm, mag_get_d(rs));
      kr_dev = std::max(kr_dev, mag_get_d_log2_approx(out_dev[r]));
      if (mag_cmp(tq, rho[r]) >= 0) ok = false;
    }

    if (sic::support::kernel_setting("SIC_PROGRESS"))
      sic::support::progressf(
          "Krawczyk certificate: rows=%ld bits=%ld reuse=%d accepted=%d max_row_sum=%.17g\n", n2,
          (long)pm, int(reuse), int(ok), kr_mnorm);
    return ok;
  };
  // 6a. Early test on B0 (unique root, hence the ghost, in a box containing the ball Z0 of z*):
  // Newton only to a
  //     moderate precision first, so that a seed too coarse is detected before the expensive
  //     iterations. Componentwise rho0_q = 2 sup |z~_q - Z0_q| (the coordinates differ in size by
  //     many orders of magnitude).
  // (the Newton correction is computed with an amplification ~chi: precision p resolves the root
  // to ~p - log2 chi
  //  bits, which must clearly exceed the seed accuracy for the B0 test)
  const slong p_b0 =
      std::min<slong>(p_hi, std::max<slong>(2 * pl + 64, p0 + (slong)std::ceil(log2chi) + 64));
  requested_phase = phase_memory::Phase::certification;
  if (memory_trace && !phase_admission) {
    memory_trace->high(p_b0 + 64);
    memory_trace->enter(phase_memory::Phase::certification);
  }
  newton(std::max<slong>(pl, p0),
         p_b0);  // (not below the seed precision: rounding would lose the seed accuracy)
  std::vector<acb_struct> zb0(n2);
  for (auto& x : zb0) acb_init(&x);
  struct ClrZ {
    std::vector<acb_struct>& a;
    ~ClrZ() {
      for (auto& x : a) acb_clear(&x);
    }
  } clrz{zb0};
  MagVec rho0(n2), dev(n2), rho1(n2);
  {
    double r0l2 = -1e300;
    for (long q = 0; q < n2; ++q) {
      acb_set(&zb0[q], &z[q]);
      sic::support::flint::Acb df;
      acb_sub(df, &z[q], &Z0[q], pl);
      acb_get_mag(rho0[q], df);
      mag_mul_2exp_si(rho0[q], rho0[q], 1);
      if (mag_is_zero(rho0[q])) mag_set_ui_2exp_si(rho0[q], 1, -(slong)p_b0);
      r0l2 = std::max(r0l2, mag_get_d_log2_approx(rho0[q]));
    }
    kr_ratio = kr_mnorm = 0;
    kr_dev = -1e300;
    pm = std::min<slong>(pl, std::max<slong>(80, (slong)std::ceil(log2chi) + 48));
    if (const char* hint = sic::support::kernel_setting("SIC_KRAWCZYK_FIRST_BITS")) {
      const auto proposed = sic::support::resources::integer(hint);
      if (proposed < 2 || proposed > (sic::support::resources::BitCount)kPrecisionCeiling)
        throw sic::Error(sic::ErrorKind::invalid_argument,
                         "SIC_KRAWCZYK_FIRST_BITS is outside the precision range");
      pm = std::min(pl, (slong)proposed);
    }
    bool b0ok = krawczyk(zb0, rho0, dev, p_b0, false, true);
    if (product_refused) return false;
    if (!b0ok && pm < pl) {
      pm = pl;
      kr_ratio = kr_mnorm = 0;
      kr_dev = -1e300;
      b0ok = krawczyk(zb0, rho0, dev, p_b0, false, true);
    }
    if (product_refused) return false;
    // Box shape. The seed box has the shape of the seed radii, which can be badly matched to I -
    // Y J: the test then fails componentwise although |I - Y J| is small. Any box containing the
    // seed box will do, so the radii are re-chosen as rho = (I - A)^{-1} u, A = 2 |M| (M = I - Y
    // J(B) on the current box), u = seed radii + 2 |Y F|, which satisfies (I - A) rho = u > |Y F|
    // whenever the spectral radius of A is below 1 (Neumann series, in floating point: only a
    // candidate). The test is then repeated rigorously on the new, larger box (with J over that
    // box).
    for (int rs = 0; rs < 3 && !b0ok; ++rs) {
      std::vector<double> A((size_t)n2 * n2), u(n2), r(n2), rn(n2);
      double umax = 0;
      for (long q = 0; q < n2; ++q) {
        u[q] = mag_get_d(rho0[q]) + 2 * mag_get_d(dev[q]);
        umax = std::max(umax, u[q]);
      }
      if (!(umax > 0) || !std::isfinite(umax)) break;
      for (long q = 0; q < n2; ++q) u[q] /= umax;
      sic::support::flint::Mag mg;
      for (long a = 0; a < n2; ++a)
        for (long c = 0; c < n2; ++c) {
          acb_get_mag(mg, acb_mat_entry(M0, a, c));
          A[(size_t)a * n2 + c] = 2 * mag_get_d(mg);
        }

      // (first with A doubled: the larger box enlarges J(B) and hence M, and a margin usually
      // saves a second round)
      bool conv = false;
      for (double fac : {2.0, 1.0}) {
        r = u;
        for (int it = 0; it < 2000; ++it) {
          double ch = 0;
          for (long a = 0; a < n2; ++a) {
            double sacc = u[a];
            const double* Ar = &A[(size_t)a * n2];
            for (long c = 0; c < n2; ++c) sacc += fac * Ar[c] * r[c];
            rn[a] = sacc;
            ch = std::max(ch, sacc / r[a]);
          }
          r.swap(rn);
          if (!std::isfinite(ch) || ch > 1e30) break;
          if (ch < 1 + 1e-9) {
            conv = true;
            break;
          }
        }
        if (conv) break;
      }
      if (!conv) break;
      double grow = 0;
      for (long q = 0; q < n2; ++q) {
        sic::support::flint::Mag nr;
        mag_set_d(nr, r[q] * umax * (1 + 1e-6));
        mag_max(rho0[q], rho0[q], nr);
        grow = std::max(grow, std::log2(r[q] / u[q]));
      }
      kr_ratio = kr_mnorm = 0;
      kr_dev = -1e300;
      b0ok = krawczyk(zb0, rho0, dev, p_b0, false, true);
      if (product_refused) return false;
      if (ghost_debug)
        fprintf(stderr, "  [ghost] B0 box reshaped (radii grown by up to 2^%.1f): %s\n", grow,
                b0ok ? "passed" : "failed");
    }
    if (!b0ok) {
      char buf[320];
      snprintf(
          buf, sizeof buf,
          "Krawczyk test on B0 failed (max rho0 = 2^%.1f, max |I-YJ| row sum %.3g, max |YF| "
          "2^%.1f, log2 ratio %.3g, cond ~ %.2g, chi ~ 2^%.0f, pl %ld, k = %d, %d Newton steps)",
          r0l2, kr_mnorm, kr_dev, kr_ratio, cond_est, log2chi, (long)pl, k, newton_iters);
      why = buf;
      if (ghost_debug) {
        fprintf(stderr, "  [ghost] B0 failed, times:");
        for (double x : tmarks) fprintf(stderr, " %.3f", x);
        fprintf(stderr, " %.3f (Newton steps %d)\n", clk() - tt0, newton_iters);
      }
      return false;
    }
    log2_rad0 = r0l2;
  }
  if (memory_trace) memory_trace->matrix("stored M0", M0, pl);
  if (after_b0) try {
      after_b0(Y, z, pl, p_b0, p_hi);
    } catch (...) {
      throw phase_memory::ObserverException{std::current_exception()};
    }
  tmarks.push_back(clk() - tt0);
  for (int round = 0;; ++round) {
    requested_phase = phase_memory::Phase::high_newton;
    if (memory_trace && !phase_admission) {
      memory_trace->callback_slot(workspace_limit);
      memory_trace->high(std::max(pl, p_hi) + 64);
      memory_trace->enter(phase_memory::Phase::high_newton);
    }
    if (!guarded_precision(pl, std::max(pl, p_hi))) return false;
    const auto high_start = clk();
    newton(round == 0 ? p_b0 : p_hi, p_hi, true);
    high_newton_seconds += clk() - high_start;
    tmarks.push_back(clk() - tt0);
    // 6b. B1: tight componentwise box around z~: rho1_q = max(4 |Y F|_q, 2^{8 - p_hi} |z_q|). B1
    // must lie in B0; the
    //     enclosure of I - Y J over B0 is reused (valid on the sub-box B1), and recomputed over
    //     B1 only if needed.
    {
      auto Chp = ctx_at(p_hi);
      residual(F, *Chp, z);
      for (long r = 0; r < n2; ++r) {
        sic::support::flint::Acb yf;
        acb_dot(yf, NULL, 0, acb_mat_entry(Y, r, 0), 1, F.data(), 1, n2, p_hi);
        acb_get_mag(dev[r], yf);
      }
    }
    double r1l2 = -1e300;
    for (long q = 0; q < n2; ++q) {
      sic::support::flint::Mag m, sh;
      acb_get_mag(m, &z[q]);
      mag_mul_2exp_si(m, m, 8 - (slong)p_hi);
      mag_mul_2exp_si(rho1[q], dev[q], 2);
      mag_max(rho1[q], rho1[q], m);
      if (mag_is_zero(rho1[q])) mag_set_ui_2exp_si(rho1[q], 1, -(slong)p_hi);
      sic::support::flint::Acb df;
      acb_sub(df, &z[q], &zb0[q], p_hi);
      acb_get_mag(sh, df);  // centre shift
      mag_mul_2exp_si(sh, sh, 1);
      mag_add(sh, sh, rho1[q]);
      const bool inside = mag_cmp(sh, rho0[q]) < 0;

      if (!inside) {
        why = "B1 not inside B0";
        return false;
      }
      r1l2 = std::max(r1l2, mag_get_d_log2_approx(rho1[q]));
    }
    kr_ratio = kr_mnorm = 0;
    kr_dev = -1e300;
    if (!krawczyk(z, rho1, dev, p_hi, true, false)) {
      kr_ratio = kr_mnorm = 0;
      kr_dev = -1e300;
      const bool b1ok = krawczyk(z, rho1, dev, p_hi, false, false);
      if (product_refused) return false;
      if (!b1ok) {
        why = "Krawczyk test on B1 failed";
        return false;
      }
    }
    log2_rad1 = r1l2;
    tmarks.push_back(clk() - tt0);
    // 7. certified z* and all V_y at p_hi
    release_vector(Vout);
    requested_phase = phase_memory::Phase::reconstruction;
    if (phase_admission && !guarded_precision(pl, std::max(pl, p_hi))) return false;
    if (memory_trace && !phase_admission) memory_trace->enter(phase_memory::Phase::reconstruction);
    const auto output_bytes =
        sic::support::resources::mul(d * d, sic::support::resources::complex_bytes(p_hi));
    bool parallel_output = parallel_reconstruction && kernel_threads > 1;
    const sic::support::resources::Budget reconstruction_budget;
    try {
#ifdef SIC_ENABLE_TEST_HOOKS
      if (before_operation) before_operation(failure_context, "reconstruction_admission");
#endif
      reconstruction_budget.require_additional(
          "certified ghost output and reconstruction",
          sic::support::resources::add(
              output_bytes, parallel_output ? sic::support::resources::ghost_reconstruction_scratch(
                                                  d, p_hi, kernel_threads)
                                            : 0));
    } catch (const sic::support::resources::AdmissionError&) {
      parallel_output = false;
    }
    if (!parallel_output) {
      try {
#ifdef SIC_ENABLE_TEST_HOOKS
        if (before_operation) before_operation(failure_context, "reconstruction_serial");
#endif
        reconstruction_budget.require_additional("certified ghost output", output_bytes);
      } catch (const sic::support::resources::AdmissionError& error) {
        memory_refusal = phase_memory::Refusal::helper;
        why = std::string("certified ghost output refused (quadrature fallback required): ") +
              error.what();
        return false;
      }
    }
    Vout.assign(d * d, acb_struct());
    for (auto& x : Vout) acb_init(&x);
    sic::support::ComplexBallVector certified_state(n2);
    slong constants_precision = 0;
    {
      for (long q = 0; q < n2; ++q) {
        acb_set(certified_state[q], &z[q]);
        arb_add_error_mag(acb_realref(certified_state[q]), rho1[q]);
        arb_add_error_mag(acb_imagref(certified_state[q]), rho1[q]);
      }
      auto Chp = ctx_at(p_hi);
      const Ctx& Ch = *Chp;
      constants_precision = Ctop->p;
      const GhostRootMetadata metadata{d, n, k, chi, p_hi, constants_precision};
      auto emit = [&](long original, acb_srcptr value) { acb_set(&Vout[original], value); };
      // Reconstruction owns its row team; no idle vector crew overlaps it.
      release_vector_workers();
      const bool reconstructed =
          parallel_output &&
          try_parallel_ghost_reconstruction(
              metadata, certified_state.v, Ch, emit, kernel_threads, workspace_limit,
              sic::support::kernel_setting("SIC_PROGRESS") != nullptr);
      if (!reconstructed) reconstruct_ghost_state(metadata, certified_state.v, Ch, emit);
    }
    // consistency with the low-precision balls
    for (long y = 1; y < d * d; ++y)
      if (!acb_overlaps(&Vout[y], &V0in[y])) {
        why = "refined value outside the low-precision ball";
        return false;
      }
    slong acc_bits = WORD_MAX;
    for (long y = 1; y < d * d; ++y) acc_bits = std::min(acc_bits, acb_rel_accuracy_bits(&Vout[y]));
    if (memory_trace) memory_trace->vector("reconstructed output", Vout, p_hi);
    tmarks.push_back(clk() - tt0);
    achieved_bits = acc_bits;
    final_prec = p_hi;
    if (ghost_debug)
      fprintf(stderr,
              "  [ghost] round %d: p_hi %ld, accuracy %ld bits (target %ld), Newton steps %d\n",
              round, (long)p_hi, (long)acc_bits, (long)target, newton_iters);
    if (acc_bits >= target) {
      if (retain_certified_root) {
        certified_root = std::make_unique<CertifiedGhostRoot>();
        certified_root->metadata = GhostRootMetadata{d, n, k, chi, p_hi, constants_precision};
        certified_root->state = std::move(certified_state);
      }
      break;
    }
    if (round >= 3) {
      why = "target accuracy not reached";
      return false;
    }
    if (acc_bits < target - kPrecisionCeiling) return precision_failure();
    const slong deficit = target - acc_bits;
    if (deficit > kPrecisionCeiling - 16 || p_hi > kPrecisionCeiling - deficit - 16)
      return precision_failure();
    // Only z, Y, M0 and the identification boxes feed the next round.
    // Discard this insufficient-accuracy reconstruction before Newton/B1
    // allocate their caches; the next reconstruction replaces it anyway.
    release_vector(Vout);
    p_hi += deficit + 16;
  }
  if (ghost_debug) {
    fprintf(stderr, "  [ghost] times:");
    for (double x : tmarks) fprintf(stderr, " %.3f", x);
    if (modular)
      fprintf(stderr,
              " (Newton steps %d, p_hi %ld, accuracy %ld bits, conditioning=unknown, chi 2^%.0f, "
              "pl %ld)\n",
              newton_iters, (long)final_prec, (long)achieved_bits, log2chi, (long)pl);
    else
      fprintf(stderr,
              " (Newton steps %d, p_hi %ld, accuracy %ld bits, cond 2^%.0f, chi 2^%.0f, pl %ld)\n",
              newton_iters, (long)final_prec, (long)achieved_bits,
              std::log2(std::max(1.0, cond_est)), log2chi, (long)pl);
  }
  if (passes && sic::support::kernel_setting("SIC_PROGRESS"))
    sic::support::progressf(
        "Newton correction refinement summary: attempts=%ld accepted=%ld passes=%d\n",
        correction_attempts, correction_accepted, passes);
  why.clear();
  return true;
}

}  // namespace sic::exact
