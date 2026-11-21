// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/stages/table/analytic_table.hpp"

#include "construct/model/failure_test_hooks.hpp"
#include "construct/model/session_state.hpp"
#include "construct/runtime/resources/construction_model.hpp"
#include "construct/runtime/session/field_values.inl.hpp"
#include "sic/support/flint_raii.hpp"
#include "support/resources/platform_snapshot.hpp"
namespace sic::construct {
void propose_quadrature_window(Table& table, const sic::support::resources::Budget& budget,
                               const ProducerOptions& producer_options) {
  // The full-cycle reference retains several states. Serialize their kernels
  // so separate fresh RSS checks cannot admit simultaneous workspaces.
  if (!table.stream_states) table.nthreads = 1;
  if (table.node_slab <= 1) return;
  const auto target = std::max<sic::support::resources::Bytes>(64 * sic::support::resources::kMib,
                                                               budget.remaining() / 4);
  const slong wp = table.prec + 40;
  const long requested = table.node_slab, rows = table.d / 2 + 1;
  // This proposes a window, not admission. The fresh kernel gate remains authoritative.
  while (table.node_slab > 1 && table.kernel_bytes(table.node_slab, rows, 2, wp) > target)
    table.node_slab = (table.node_slab + 1) / 2;
  if (table.node_slab != requested && producer_options.progress)
    sic::support::progressf(
        "quadrature node-window proposal reduced from %ld to %ld at %ld bits (fresh kernel "
        "admission remains mandatory)\n",
        requested, table.node_slab, (long)table.prec);
}

}  // namespace sic::construct
namespace sic::construct {
void Construction::configure_table(Table& table, bool early_finite) {
  if (analytic_layout && !table.compact()) table.enable_compact(analytic_layout);
  propose_quadrature_window(table, budget, producer_options);
  table.admit_workspace = [&](sic::support::resources::Bytes bytes) {
    budget.require_additional("quadrature nodes/matrix workspace", bytes);
  };
  table.class_completed = {};
  if (early_finite)
    table.class_completed = [pointer = &table](sic::support::Index c) {
      const auto& values = pointer->compact() ? pointer->packed[c].v : pointer->val[c];
      for (size_t q = 0; q < values.size(); ++q)
        if (!acb_is_finite(&values[q])) throw TablePrecisionError(c, q, pointer->prec);
    };
}
void Construction::derive_classes(Table& Tb) {
  if (Tb.compact()) {
    Tb.layout->pair_classes(
        derived, cc_pre, [&](sic::support::Index point) { return (*group_).act(cc_pre, point); });
    Tb.paired_values = true;
    return;
  }
  for (sic::support::Index c = 0; c < (*ar_).h(); ++c)
    if (derived[c])
      parallel_for(d * d, nth, [&](long q) {
        if (q == 0) return;
        const sic::support::Index point = (*group_).orbit[(long)c * d * d + q],
                                  Y = (*group_).act(cc_pre, point);
        acb_conj(&Tb.val[c][q], &Tb.val[Y / (d * d)][Y % (d * d)]);
      });
}
void Construction::initialize_pending(bool stream, slong precision) {
  pending_table_backing.clear();
  if (stream) {
    pending_table_backing.resize((*ar_).h());
    if (completed_cache && completed_precision == precision)
      pending_table_backing = completed_inventory.backing;
  }
}
void Construction::park_completed_class(Table& table, sic::support::Index c,
                                        std::unique_ptr<CertifiedGhostRoot> root, long element,
                                        const std::string& method) {
  if (!table.compact() || table.packed[c].size() != (size_t)table.layout->slots)
    throw std::logic_error("cannot park an incomplete analytic class");
  TableClassBacking backing;
  if (root)
    backing.ghost = GhostClassBacking::save(*root, element, table.prec);
  else
    backing.values = BallFile::save(table.packed[c]);
  if (completed_cache && completed_precision == table.prec) {
    std::ostringstream options;
    for (const char* flag :
         {"SIC_ROOT_BACKING", "SIC_NEWTON_MATRIX_BITS", "SIC_SELECTION_THREADS", "SIC_THREADS",
          "SIC_CLASS_THREADS", "SIC_SEED_A", "SIC_KRAWCZYK_FIRST_BITS", "SIC_NEWTON_MEMORY_OWNERS",
          "SIC_NEWTON_PHASE_MEMORY"
#ifdef SIC_ENABLE_TEST_HOOKS
          ,
          "SIC_TEST_NEWTON_DEFER_CLASS", "SIC_TEST_NEWTON_DEFER_ENTRY_AFTER_HINT"
#endif
         }) {
      const char* value = producer_options.raw(flag);
      options << flag << '=' << std::quoted(value ? value : "<default>") << '\n';
    }

    backing = completed_cache->publish(c, std::move(backing), method, options.str());
    completed_inventory.backing[c] = backing;
    sic::support::progressf(
        "completed-class cache published: class=%ld kind=%s method=%s precision=%ld bytes=%llu\n",
        (long)c, backing.ghost ? "root" : "full", method.c_str(), (long)table.prec,
        (unsigned long long)backing.bytes());
  }
  table.packed[c] = sic::support::ComplexBallVector();
  pending_table_backing.at(c) = std::move(backing);
#if defined(__GLIBC__)
  sic::support::resources::release_free_heap();
#endif
}
std::unique_ptr<Table> Construction::build_table(slong precision, bool early_finite, bool stream) {
  if (require_completed_cache.enabled) {
    if (!completed_cache || !stream || completed_precision != precision + 16)
      throw std::runtime_error(
          "cache-only consumer requires a validated current-precision inventory before table "
          "allocation");
    require_completed_cache.enforce(precision + 16, completed_inventory.hits,
                                    completed_inventory.missing,
                                    std::count(derived.begin(), derived.end(), 0));
  }
  initialize_pending(stream, precision + 16);
  std::vector<char> missing((*ar_).h());
  for (sic::support::Index c = 0; c < (*ar_).h(); ++c)
    missing[c] = !derived[c] && !(stream && pending_table_backing[c]);
  if (completed_cache && stream && completed_precision == precision + 16 &&
      std::count(missing.begin(), missing.end(), 1) == 0) {
    auto table = std::make_unique<Table>((*ar_), precision + 16, nth);
    configure_table(*table);
    table->alloc_values();
    if (std::count(derived.begin(), derived.end(), 1)) derive_classes(*table);
    sic::support::progressf(
        "table: all physical classes reused from completed cache; no quadrature worker admitted\n");
    return table;
  }
  if (completed_cache && stream && completed_precision == precision + 16)
    sic::support::progressf("completed-class cache quadrature fallback: reused=%zu missing=%zu\n",
                            (size_t)std::count(derived.begin(), derived.end(), 0) -
                                std::count(missing.begin(), missing.end(), 1),
                            (size_t)std::count(missing.begin(), missing.end(), 1));
  if (stream) {
    if (!analytic_layout)
      throw sic::Error(sic::ErrorKind::invalid_argument,
                       "streamed quadrature requires compact Arb tables");
    budget.require_additional(
        "streamed table quadrature/retry",
        sic::support::resources::add(
            (*model_).quadrature_workspace(precision + 16, nth),
            sic::support::resources::mul((d * d - 1) / 3,
                                         sic::support::resources::complex_bytes(precision + 16))));
  } else
    budget.require("table quadrature/retry", (*model_).quadrature(precision + 16, nth, 1));
  auto table = std::make_unique<Table>((*ar_), precision + 16, nth);
  configure_table(*table, early_finite);
  if (stream)
    table->class_completed = [&, pointer = table.get()](sic::support::Index c) {
      park_completed_class(*pointer, c);
    };
  bool any = completed_cache && stream && completed_precision == precision + 16;
  for (char c : derived) any = any || c;
  if (any) {
    table->build_selected(missing);
    if (std::count(derived.begin(), derived.end(), 1)) derive_classes(*table);
  } else
    table->build_batched();
  table->release_states();
  return table;
}
sic::support::ComplexBallVector Construction::gen_vector(const Table& Tb, int j, slong wp,
                                                         const sic::support::PeriodicGrid* grid) {
  const long count = grid ? grid->count : N;
  sic::support::ComplexBallVector values(count);
  if (j < s) {
    parallel_for(count, nth, [&](long q) {
      conj_f_at(Tb, 0, d >> (j + 1), grid ? grid->element(q) : q, values[q]);
    });
  } else if (j == s) {
    sic::support::ComplexBallVector roots(2 * d);
    for (long k = 0; k < 2 * d; ++k) root_2d(k, wp, roots[k]);
    parallel_for(count, nth, [&](long q) {
      acb_set(values[q], roots[(*group_).nn[grid ? grid->element(q) : q]]);
    });
  } else {
    sic::support::flint::Arb e;
    to_arb(e, (*ar_).kc, (*ar_).eps, 1, wp);
    arb_sqrt(e, e, wp);
    parallel_for(count, nth, [&](long q) {
      const long g = grid ? grid->element(q) : q;
      acb_set_arb(values[q], e);
      if ((*group_).chi[g] < 0) acb_neg(values[q], values[q]);
    });
  }
  return values;
}
NativeAnalyticIdentity Construction::inspect_coarse_identity(
    const std::filesystem::path& checkpoint, const std::string& source) {
  if (!pinned_inputs) return {};
  const auto digest = checkpoint_digest(checkpoint / "manifest");
  if (digest != cache_checksum_file(checkpoint / "manifest.sha256"))
    throw sic::Error(sic::ErrorKind::integrity,
                     "coarse checkpoint identity changed while loading/publishing");
  return {checkpoint / "manifest", digest, source};
}
std::unique_ptr<Table> Construction::build_pass1(slong precision) try {
#ifdef SIC_ENABLE_TEST_HOOKS
  if (failure_test_hooks::coarse_table) failure_test_hooks::coarse_table();
#endif
  std::filesystem::path checkpoint;
  if (const char* root = producer_options.analytic_cache) {
    if (!analytic_layout)
      throw sic::Error(sic::ErrorKind::invalid_argument,
                       "analytic checkpoint requires compact Arb tables");
    checkpoint = TableCheckpoint::directory(root, (*ar_), precision + 16);
    if (std::filesystem::exists(checkpoint)) {
      const auto origin = pinned_inputs ? cache_checksum_file(checkpoint / "manifest.sha256") : "";
      auto loaded = TableCheckpoint::load(
          (*ar_), (*group_), analytic_layout, precision + 16, nth, checkpoint, cc_pre, derived,
          [&](size_t physical) {
            (*model_).computed_classes = physical;
            budget.require("analytic checkpoint load", (*model_).quadrature(precision + 16, nth));
          });
      auto verified = inspect_coarse_identity(checkpoint, TableCheckpoint::source_id());
      if (pinned_inputs && origin != verified.digest)
        throw sic::Error(sic::ErrorKind::integrity,
                         "coarse checkpoint manifest changed while loading");
      configure_table(*loaded);
      verified_coarse = std::move(verified);
      sic::support::progressf(
          "analytic checkpoint loaded with source/precision/SHA-256 checks: %s [%.1fs]\n",
          checkpoint.c_str(), stamp());
      return loaded;
    }
  }
  if (require_completed_cache.enabled)
    throw sic::Error(
        sic::ErrorKind::io,
        "cache-only consumer lost its native analytic checkpoint; input construction is forbidden");
  auto finish = [&](std::unique_ptr<Table> completed) {
    size_t nonfinite = 0, first_slot = 0;
    sic::support::Index first_class = -1;
    for (sic::support::Index c = 0; c < (*ar_).h(); ++c) {
      const auto& values = completed->compact() ? completed->packed[c].v : completed->val[c];
      for (size_t q = 0; q < values.size(); ++q)
        if (!acb_is_finite(&values[q])) {
          if (!nonfinite) {
            first_class = c;
            first_slot = q;
          }
          ++nonfinite;
        }
    }
    if (nonfinite) {
      sic::support::progressf(
          "coarse analytic table has %zu nonfinite enclosures (first class=%ld slot=%zu); no "
          "checkpoint published\n",
          nonfinite, (long)first_class, first_slot);
      throw TablePrecisionError(first_class, first_slot, precision + 16);
    }
    if (!checkpoint.empty()) {
      TableCheckpoint::save(*completed, (*group_), cc_pre, derived, checkpoint);
      verified_coarse = inspect_coarse_identity(checkpoint, TableCheckpoint::source_id());
      sic::support::progressf("completed analytic enclosure checkpoint saved: %s [%.1fs]\n",
                              checkpoint.c_str(), stamp());
    }
    return completed;
  };
  (*model_).computed_classes = (*ar_).h();
  budget.require("pass-1 table/retry", (*model_).quadrature(precision + 16, nth));
  const sic::support::Index h = (*ar_).h();
  std::vector<sic::support::Index> partner(h, -1);
  bool pairing = true;
  try {
#ifdef SIC_ENABLE_TEST_HOOKS
    if (failure_test_hooks::pairing) failure_test_hooks::pairing();
#endif
    const QuadraticNumber al = sub(QuadraticNumber(1), (*ar_).eps);
    for (sic::support::Index c = 0; c < h && pairing; ++c)
      partner[c] =
          (*ar_).identify(lattice_scale((*ar_).kc, al, (*ar_).class_involution(c))).class_index;
  } catch (const sic::Error& error) {
    if (error.kind() != sic::ErrorKind::construction && error.kind() != sic::ErrorKind::precision)
      throw;
    // Arithmetic identification or its precision ceiling selects the full quadrature table.
    pairing = false;
  } catch (const std::overflow_error&) {
    throw;
  } catch (const std::system_error&) {
    throw;
  } catch (const std::runtime_error&) {
    // Keep the arithmetic helper's untyped identification refusal recoverable.
    pairing = false;
  }
  for (sic::support::Index c = 0; c < h && pairing; ++c)
    if (partner[c] == c || partner[partner[c]] != c) pairing = false;
  derived.assign(h, 0);
  if (!pairing) return finish(build_table(precision, true));
  std::vector<char> cheap(h, 0), expv(h, 0);
  long ncheap = 0;
  for (sic::support::Index c = 0; c < h; ++c) {
    const sic::support::Index o = partner[c];
    const size_t sc = (*ar_).cls[c].digit.size(), so = (*ar_).cls[o].digit.size();
    cheap[c] = sc < so || (sc == so && c < o);
    expv[c] = !cheap[c];
    if (cheap[c]) ncheap += (long)sc;
  }
  auto Tm = std::make_unique<Table>((*ar_), precision + 16, nth);
  configure_table(*Tm, true);
  Tm->build_selected(cheap);
  // Identification of cc. By class field theory cc is the Artin symbol of the real place
  // infinity_1, whose image in the narrow class group is s; so cc maps the class cE = partner(E)
  // onto E for every class E. Take E the derived class with the shortest cycle and compute it at a
  // low precision only. Among the candidates g (nn = -1, chi = +1, g(cE) = E), cc satisfies T[g(x)]
  // = conj(T[x]) for all x in cE; if exactly one candidate passes this test (overlap of the balls
  // at all d^2 - 1 points), it is cc.
  sic::support::Index E = -1;
  for (sic::support::Index c = 0; c < h; ++c)
    if (expv[c] && (E < 0 || (*ar_).cls[c].digit.size() < (*ar_).cls[E].digit.size())) E = c;
  const sic::support::Index cE = partner[E];
  const slong Pid = 48;
  long g0 = -1;
  int nh = 0;
  {
    Table Tid((*ar_), Pid, 1 < nth ? nth : 1);
    configure_table(Tid);
    Tid.build_class(E);
    std::vector<long> cand;
    const sic::support::Index xref = (*group_).orbit[(long)cE * d * d + 1];
    for (long g = 0; g < N; ++g)
      if ((*group_).nn[g] == 2 * d - 1 && (*group_).chi[g] > 0 &&
          (*group_).act(g, xref) / (d * d) == E)
        cand.push_back(g);
    std::vector<char> hit(cand.size(), 0);
    parallel_for((long)cand.size(), nth, [&](long q) {
      sic::support::flint::Acb cj, value;
      bool all = true;
      for (long p = 1; p < d * d && all; ++p) {
        const sic::support::Index point = (*group_).orbit[(long)cE * d * d + p],
                                  Y = (*group_).act(cand[q], point);
        Tm->read(cj, point);
        acb_conj(cj, cj);
        Tid.read(value, Y);
        all = acb_overlaps(cj, value);
      }
      hit[q] = all;
    });
    for (size_t q = 0; q < cand.size(); ++q)
      if (hit[q]) {
        ++nh;
        g0 = cand[q];
      }
  }
  bool ok = nh == 1;
  for (sic::support::Index c = 0; c < h && ok; ++c)
    if (expv[c]) {  // cc must map every derived class into a computed one
      const sic::support::Index Y = (*group_).act(g0, (*group_).orbit[(long)c * d * d + 1]);
      if (!cheap[Y / (d * d)]) ok = false;
    }
  if (!ok) {
    sic::support::progressf(
        "conjugate class pairs: complex conjugation not identified (%d candidates); full table\n",
        nh);
    return finish(build_table(precision, true));
  }
  cc_pre = g0;
  derived = expv;
  (*model_).computed_classes = std::count(cheap.begin(), cheap.end(), 1);
  derive_classes(*Tm);
  Tm->release_states();
  sic::support::progressf(
      "table (pass 1): %d of %ld classes by quadrature (%ld of %ld states), %d by complex "
      "conjugation (cc = element %ld, identified on class %ld at %ld bits)\n",
      (int)std::count(cheap.begin(), cheap.end(), 1), (long)h, ncheap, (long)Tm->nstates(),
      (int)std::count(expv.begin(), expv.end(), 1), cc_pre, (long)E, (long)Pid);
  return finish(std::move(Tm));
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::construct

namespace sic::construct {
TablePrecisionError::TablePrecisionError(sic::support::Index cls, size_t slot, slong precision)
    : sic::Error(sic::ErrorKind::precision, "nonfinite analytic class " + std::to_string(cls) +
                                                " slot " + std::to_string(slot) + " at " +
                                                std::to_string(precision) + " bits") {}
}  // namespace sic::construct
