// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/model/session_state.hpp"
#include "construct/runtime/resources/construction_model.hpp"
#include "construct/stages/table/analytic_table.hpp"
#include "support/resources/platform_snapshot.hpp"
namespace sic::construct {
void Construction::prepare_completed_cache(slong precision, bool stream) {
  if (!completed_cache_root) return;
  if (!stream || !analytic_layout || verified_group_digest.empty() ||
      verified_coarse.digest.empty() || verified_coarse.source.empty())
    throw sic::Error(
        sic::ErrorKind::invalid_argument,
        "completed-class cache requires streamed compact classes and verified coarse/group inputs");
  if (checkpoint_digest(verified_coarse.manifest) != verified_coarse.digest)
    throw sic::Error(sic::ErrorKind::integrity,
                     "coarse origin changed before completed-class reuse");
  if (completed_elements.empty()) {
    const sic::support::Index anchor = (*group_).orbit[(*group_).pidx(0, 0, 1)];
    completed_elements.assign((*ar_).h(), -1);
    for (long g = 0; g < N; ++g) {
      const sic::support::Index c = (*group_).act(g, anchor) / (d * d);
      if (completed_elements[c] < 0) completed_elements[c] = g;
    }
  }
  if (completed_producer.empty()) completed_producer = checkpoint_digest("/proc/self/exe");
  CompletedClassIdentity identity;
  identity.s = s;
  identity.d = d;
  identity.delta = (*ar_).Delta;
  identity.order = (*ar_).order_index;
  identity.degree = N;
  identity.slots = analytic_layout->slots;
  identity.cc = cc_pre;
  identity.precision = precision;
  identity.paired = std::count(derived.begin(), derived.end(), 1) != 0;
  identity.producer = completed_producer;
  identity.group_digest = verified_group_digest;
  identity.coarse_digest = verified_coarse.digest;
  identity.coarse_source = verified_coarse.source;
  identity.derived = derived;
  identity.elements = completed_elements;
  completed_cache =
      std::make_unique<CompletedClassCache>(completed_cache_root, std::move(identity), (*group_));
  completed_precision = precision;
  completed_inventory = completed_cache->inventory(nth);
  (*model_).table_reload_workspace = completed_inventory.replay_workspace;
  sic::support::progressf(
      "completed-class cache inventory: precision=%ld reused=%zu missing=%zu "
      "replay_workspace=%llu\n",
      (long)precision, completed_inventory.hits, completed_inventory.missing,
      (unsigned long long)completed_inventory.replay_workspace);
  // Validate every native record before enforcing complete hits,
  // and repeat on each requested precision before any target allocation.
  require_completed_cache.enforce(precision, completed_inventory.hits, completed_inventory.missing,
                                  std::count(derived.begin(), derived.end(), 0));
  if (require_completed_cache.enabled)
    sic::support::progressf(
        "cache-only consumer inventory accepted: precision=%ld reused=%zu missing=0\n",
        (long)precision, completed_inventory.hits);
}
}  // namespace sic::construct

namespace sic::construct {
void Construction::load_table_class(sic::support::Index c) {
  if (!table_backing.at(c)) throw std::logic_error("missing physical table class backing");
  budget.require_additional("analytic table class reload",
                            sic::support::resources::mul(
                                T->layout->slots, sic::support::resources::complex_bytes(T->prec)));
  T->packed[c] = table_backing[c].ghost
                     ? table_backing[c].ghost->load((*group_), *T, c, budget, nth)
                     : table_backing[c].values->complex();
}

void Construction::drop_table_class(sic::support::Index c) {
  T->packed[c] = sic::support::ComplexBallVector();
#if defined(__GLIBC__)
  sic::support::resources::release_free_heap();
#endif
}

void Construction::park_table() {
  if (!pending_table_backing.empty()) {
    if (pending_table_backing.size() != (size_t)(*ar_).h())
      throw std::logic_error("wrong streamed table backing count");
    for (sic::support::Index c = 0; c < (*ar_).h(); ++c)
      if (T->packed[c].size() || bool(pending_table_backing[c]) != !bool(derived[c]))
        throw std::logic_error("incomplete streamed analytic table backing");
    table_backing = std::move(pending_table_backing);
    pending_table_backing.clear();
  } else {
    table_backing.assign((*ar_).h(), {});
    for (sic::support::Index c = 0; c < (*ar_).h(); ++c)
      if (T->packed[c].size()) {
        if (completed_cache && completed_precision == T->prec && completed_inventory.backing[c]) {
          table_backing[c] = completed_inventory.backing[c];
          sic::support::progressf(
              "completed-class cache adopted without serialization: class=%ld precision=%ld\n",
              (long)c, (long)T->prec);
        } else {
          table_backing[c] = BallFile::save(T->packed[c]);
          if (completed_cache && completed_precision == T->prec) {
            std::string options = "complete coarse table retained at final precision";

            table_backing[c] =
                completed_cache->publish(c, std::move(table_backing[c]), "quadrature", options);
            completed_inventory.backing[c] = table_backing[c];
          }
        }
        T->packed[c] = sic::support::ComplexBallVector();
      }
  }
  for (auto& entry : T->gl_cache)
    for (auto& node : entry.second) {
      arb_clear(&node.abscissa);
      arb_clear(&node.weight);
    }
  T->gl_cache.clear();
  table_parked = true;
  sic::support::resources::Bytes serialized = 0;
  size_t root_classes = 0, full_classes = 0;
  (*model_).table_reload_workspace = 0;
  for (const auto& backing : table_backing)
    if (backing) {
      serialized = sic::support::resources::add(serialized, backing.bytes());
      (*model_).table_reload_workspace =
          std::max((*model_).table_reload_workspace, backing.workspace(nth));
      if (backing.ghost)
        ++root_classes;
      else
        ++full_classes;
    }
  budget.require(
      "actual analytic replay/final live set",
      (*model_).final_buffers(T->prec, (*model_).decoder_hint ? (*model_).decoder_hint : T->prec));
#if defined(__GLIBC__)
  sic::support::resources::release_free_heap();
#endif
  sic::support::progressf(
      "analytic table parked exactly; generators and representatives reload one physical class at "
      "a time   [%.1fs]\n",
      stamp());
  sic::support::progressf(
      "analytic backing: root_classes=%zu full_classes=%zu serialized_bytes=%llu "
      "replay_workspace=%llu\n",
      root_classes, full_classes, (unsigned long long)serialized,
      (unsigned long long)(*model_).table_reload_workspace);
}
}  // namespace sic::construct
