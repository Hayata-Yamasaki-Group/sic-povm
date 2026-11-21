// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decode/dag.hpp"

#include "decode/decoder.hpp"
#include "sic/error.hpp"
#include "sicx/codec/integer.hpp"
namespace sic::decode {
uint64_t next_graph_depth(uint64_t depth) { return sic::sicx::checked_add(depth, 1); }
}  // namespace sic::decode
#include "decode/input/element_source.hpp"
#include "sicx/model/branches.hpp"

namespace sic::decode {
Decoder::Impl::Graph Decoder::Impl::plan(const std::vector<Request>& requests) {
  std::vector<sic::decode::detail::WordView> words;
  for (const auto& r : requests) words.emplace_back(r.word);
  const sic::decode::detail::PrefixPlan prefix(words, meta_.smith);
  const auto nodes = checked_add(prefix.planned_calls, 1);
  if (nodes > options_.maximum_graph_nodes || nodes > SIZE_MAX / 128 ||
      checked_mul(nodes, 128 + checked_mul(meta_.levels(), 16)) > budget_.available())
    throw sic::Error(ErrorKind::resource, "decoder graph work/storage policy exceeded");
  Graph g;
  g.reservation = std::make_unique<ReserveGuard>(
      budget_, checked_mul(nodes, checked_add(128, checked_mul(meta_.levels(), 16))),
      "DAG persistent arrays");
  g.nodes.reserve(nodes);
  g.endpoints.resize(requests.size());
  sic::decode::detail::PrefixStatistics statistics;
  prefix.walk(
      uint64_t(0), statistics,
      [&](uint64_t source, unsigned axis) {
        g.nodes.push_back({source, axis});
        return uint64_t(g.nodes.size() - 1);
      },
      [&](size_t q, uint64_t node) { g.endpoints[q] = node; });
  return g;
}

void Decoder::Impl::quadratic_roots(ComplexBallVector& roots, acb_srcptr square, slong bits) {
  if (acb_is_zero(square))
    throw sic::Error(sic::ErrorKind::invalid_format, "exactly zero quadratic square");
  if (!acb_is_finite(square) || acb_contains_zero(square))
    throw PrecisionRefusal("unresolved quadratic square");
  if (arb_is_negative(acb_realref(square))) {
    acb_neg(roots[0], square);
    acb_sqrt(roots[0], roots[0], bits);
    acb_mul_onei(roots[0], roots[0]);
  } else
    acb_sqrt(roots[0], square, bits);
  acb_neg(roots[1], roots[0]);
  if (!acb_is_finite(roots[0]) || acb_contains_zero(roots[0]) || acb_overlaps(roots[0], roots[1]))
    throw PrecisionRefusal("quadratic roots not separated");
}

unsigned Decoder::Impl::member(const Family& family, acb_srcptr target, bool authoritative,
                               slong bits, acb_ptr selected, acb_ptr rounded) {
  acb_set_round(rounded, target, bits);
  sic::decode::kernels::select(selected, family.roots, authoritative ? target : rounded,
                               authoritative, bits);
  unsigned matches = 0, index = 0;
  for (unsigned j = 0; j < family.roots.size(); ++j)
    if (acb_overlaps(family.roots[j], authoritative ? target : rounded)) {
      ++matches;
      index = j;
    }
  if (matches != 1) throw PrecisionRefusal("ambiguous root label");
  return index;
}

Tree Decoder::Impl::prepare(const Graph& graph, slong bits, bool authoritative,
                            std::vector<std::vector<uint64_t>>* node_classes_out) {
  Tree tree{meta_.degrees, meta_.width, bits, {}, ++tree_serial_};
  const uint64_t nodes = graph.nodes.size();
  {
    std::vector<uint64_t> depth(nodes, 0);
    generations_.clear();
    for (uint64_t node = 1; node < nodes; ++node) {
      if (graph.nodes[node].source >= node) throw std::logic_error("graph is not parent-first");
      depth[node] = next_graph_depth(depth[graph.nodes[node].source]);
      if (generations_.size() < depth[node]) generations_.resize(depth[node]);
      generations_[depth[node] - 1].push_back(node);
    }
    stats_.generations += generations_.size();
  }
  const unsigned axes = static_cast<unsigned>(meta_.smith.size());
  std::vector<uint64_t> previous(nodes, 0), next;
  if (node_classes_out) node_classes_out->push_back(previous);
  for (unsigned i = 0; i < meta_.levels(); ++i) {
    const auto level_start = std::chrono::steady_clock::now();
    const unsigned p = meta_.degrees[i];
    const uint64_t parents = tree.count(i);
    rebase();
    // Level arrays besides the element values: root families, classes (at most
    // min(nodes, parents * p)), node indices. The element values are reserved
    // after their contraction (whose own reservation covers them until then).
    const uint64_t max_classes = std::min<uint64_t>(nodes, parents * p);
    ReserveGuard level_guard(
        budget_,
        pool_->size() * av_bytes(p + 3, bits) + parents * (av_bytes(2 * p + 2, bits) + 256) +
            max_classes * (av_bytes(p + 1, bits) + 64) + nodes * 32 + 8 * parents * p +
            (parents * p * axes <= nodes ? 8 * parents * p * axes : 0),
        "restriction tree level");
    // All elements of this level, contracted together.
    std::vector<size_t> ids;
    size_t coefficient_count = 0, delta_at = SIZE_MAX;
    std::vector<std::vector<size_t>> action_at(axes);
    if (p == 2)
      ids.push_back(element_id("wsq", i));
    else
      for (unsigned k = 0; k < p; ++k) ids.push_back(element_id("fcoef", i, -1, k));
    coefficient_count = ids.size();
    for (unsigned axis = 0; axis < axes; ++axis) {
      if (p == 2) {
        if (has_element("act", i, axis)) {
          action_at[axis].push_back(ids.size());
          ids.push_back(element_id("act", i, axis));
        }
      } else if (has_element("actodd", i, axis, 0)) {
        if (delta_at == SIZE_MAX) {
          delta_at = ids.size();
          ids.push_back(element_id("actdelta", i));
        }
        for (unsigned k = 0; k < p; ++k) {
          action_at[axis].push_back(ids.size());
          ids.push_back(element_id("actodd", i, axis, k));
        }
      }
    }
    const auto eval_start = std::chrono::steady_clock::now();
    std::vector<ComplexBallVector> values = contract_many(tree, ids, i);
    ReserveGuard values_guard(budget_, ids.size() * av_bytes(parents, bits),
                              "tower element values");
    const double eval_seconds = seconds(eval_start);
    stats_.seconds_tower_eval += eval_seconds;
    // Stage-local scratch: one slot per pool worker, charged above, destroyed
    // after every level (also on refusal). No retry/tree state is retained.
    std::vector<ComplexBallVector> scratch;
    scratch.reserve(pool_->size());
    for (unsigned w = 0; w < pool_->size(); ++w) scratch.emplace_back(p + 3);
    auto families = std::make_unique<Family[]>(parents);
    const auto roots_start = std::chrono::steady_clock::now();
    pool_->run(
        parents,
        [&](size_t parent) {
          Family& f = families[parent];
          if (p == 2) {
            f.roots = ComplexBallVector(2);
            quadratic_roots(f.roots, values[0][parent], bits);
          } else {
            acb_poly_set_coeff_si(f.polynomial.get(), p, 1);
            for (unsigned k = 0; k < p; ++k)
              acb_poly_set_coeff_acb(f.polynomial.get(), k, values[k][parent]);
            f.roots = isolate_roots(f.polynomial, bits, options_.root_iterations);
          }
        },
        2, true);
    const double roots_seconds = seconds(roots_start);
    stats_.seconds_roots += roots_seconds;
    (void)coefficient_count;
    const auto edges_start = std::chrono::steady_clock::now();
    // Root index of every node at this level. A node's index depends only on
    // (source lower class, source index, axis, node lower class); identical
    // inputs are computed once per pass of a level (memo), which gives the
    // same index as computing each node.
    std::vector<unsigned> index(nodes, 0);
    {
      ComplexBallVector branch(1);
      if (authoritative) {
        if (arb_load_str(acb_realref(branch[0]), meta_.branches[i][0].c_str()) ||
            arb_load_str(acb_imagref(branch[0]), meta_.branches[i][1].c_str()))
          fail(sic::ErrorKind::invalid_format, "branch rectangle does not load");
        index[0] =
            member(families[previous[0]], branch[0], true, bits, scratch[0][1], scratch[0][2]);
      } else
        index[0] = member(families[previous[0]], base_roots_.at(i)[0], false, bits, scratch[0][1],
                          scratch[0][2]);
    }
    auto node_root = [&](uint64_t node) -> acb_srcptr {
      return families[previous[node]].roots[index[node]];
    };
    auto compute = [&](uint64_t node) {
      const auto [source, axis] = graph.nodes[node];
      const uint64_t source_lower = previous[source];
      acb_srcptr old_root = node_root(source);
      auto& workspace = scratch[Pool::worker_id()];
      acb_ptr target = workspace[0];
      const auto& at = action_at[axis];
      if (at.empty())
        acb_set(target, old_root);
      else if (p == 2) {
        if (acb_contains_zero(old_root))
          throw PrecisionRefusal("source quadratic root contains zero");
        acb_div(target, values[at[0]][source_lower], old_root, bits);
      } else {
        ComplexBallVector coefficients(p);
        for (unsigned k = 0; k < p; ++k) acb_set(coefficients[k], values[at[k]][source_lower]);
        transport_odd_value(target, coefficients, p, values[delta_at][source_lower], old_root,
                            bits);
      }
      index[node] =
          member(families[previous[node]], target, false, bits, workspace[1], workspace[2]);
    };
    const uint64_t slots = parents * p * axes;
    const bool memo = slots <= nodes;
    std::vector<uint64_t> owner;
    if (memo) owner.assign(slots, UINT64_MAX);
    std::vector<uint64_t> todo;
    std::vector<std::pair<uint64_t, uint64_t>> follow;  // (node, owner)
    for (const auto& generation : generations_) {
      if (!memo) {
        pool_->run_grouped(
            generation.size(), [&](size_t q) { compute(generation[q]); }, 16);
        stats_.edge_targets += generation.size();
        continue;
      }
      todo.clear();
      follow.clear();
      for (auto node : generation) {
        const auto [source, axis] = graph.nodes[node];
        const uint64_t s = (previous[source] * p + index[source]) * axes + axis;
        if (owner[s] == UINT64_MAX) {
          owner[s] = node;
          todo.push_back(node);
        } else if (previous[owner[s]] != previous[node])
          todo.push_back(node);
        else
          follow.push_back({node, owner[s]});
      }
      pool_->run_grouped(
          todo.size(), [&](size_t q) { compute(todo[q]); }, 16);
      for (const auto& [node, from] : follow) index[node] = index[from];
      stats_.edge_targets += todo.size();
      stats_.edge_targets_reused += follow.size();
    }
    const double targets_seconds = seconds(edges_start);
    // Classes = distinct (lower class, root index) pairs, labelled in the
    // order node 0, then generation by generation (node order within each).
    const auto labels_start = std::chrono::steady_clock::now();
    std::vector<Class> classes;
    next.assign(nodes, UINT64_MAX);
    {
      std::vector<uint64_t> class_of(parents * p, UINT64_MAX);
      std::vector<uint64_t> keys;
      auto label = [&](uint64_t node) {
        const uint64_t key = previous[node] * p + index[node];
        if (class_of[key] == UINT64_MAX) {
          class_of[key] = keys.size();
          keys.push_back(key);
        }
        next[node] = class_of[key];
      };
      label(0);
      for (const auto& generation : generations_)
        for (auto node : generation) label(node);
      classes.resize(keys.size());
      pool_->run(
          keys.size(),
          [&](size_t c) {
            Class& k = classes[c];
            k.parent = keys[c] / p;
            k.index = static_cast<unsigned>(keys[c] % p);
            k.dual = ComplexBallVector(p);
          },
          4096, true);
    }
    const double labels_seconds = seconds(labels_start);
    const auto duals_start = std::chrono::steady_clock::now();
    pool_->run(
        classes.size(),
        [&](size_t c) {
          Class& k = classes[c];
          const acb_srcptr root = families[k.parent].roots[k.index];
          if (p == 2) {
            if (acb_contains_zero(root))
              throw PrecisionRefusal("quadratic dual denominator contains zero");
            acb_one(k.dual[0]);
            acb_mul_2exp_si(k.dual[0], k.dual[0], -1);
            acb_inv(k.dual[1], root, bits);
            acb_mul_2exp_si(k.dual[1], k.dual[1], -1);
          } else
            k.dual = euler_duals(families[k.parent].polynomial, root, bits);
        },
        256, true);
    const double duals_seconds = seconds(duals_start);
    stats_.seconds_edges += seconds(edges_start);
    stats_.seconds_edges_targets += targets_seconds;
    stats_.seconds_edges_labels += labels_seconds;
    stats_.seconds_edges_duals += duals_seconds;
    release_parallel(values);
    tree.levels.push_back(std::move(classes));
    previous = next;
    if (node_classes_out) node_classes_out->push_back(previous);
    progress() << "SICX-DECODE level " << i + 1 << "/" << meta_.levels() << " p=" << p
               << " classes=" << tree.count(i + 1) << " precision=" << bits << std::endl;
    if (options_.profile)
      progress() << "SICX-PROFILE level " << i + 1 << " elements=" << ids.size()
                 << " eval=" << eval_seconds << " roots=" << roots_seconds
                 << " targets=" << targets_seconds << " labels=" << labels_seconds
                 << " duals=" << duals_seconds << " memo=" << memo
                 << " total=" << seconds(level_start) << " rss=" << resident_bytes() << std::endl;
  }
  return tree;
}

void Decoder::Impl::base_path() {
  if (!base_roots_.empty()) return;
  const auto start = std::chrono::steady_clock::now();
  // Canonical rectangles can have singleton components. General numerical root
  // isolation need not prove their exact zero parts, even at higher precision.
  // Reuse the exact-axis/absolute-grid proof, then label the main DAG by overlap
  // with these certified base roots. Never clip an uncertain ball to a singleton.
  const auto canonical = canonical_branches(
      meta_,
      [&](size_t id, auto visit) {
        for (auto li : reader_.element_blocks(id)) {
          uint64_t base = 0;
          stream_leaf_block(reader_, li, base,
                            [&](uint64_t position, const fmpz_t x, const fmpz_t y) {
                              visit(base + position, x, y);
                            });
        }
      },
      options_.maximum_precision, options_.base_bits);
  if (canonical.bits != meta_.branch_bits || canonical.rectangles != meta_.branches)
    fail(sic::ErrorKind::invalid_format, "noncanonical base branch rectangles");
  base_roots_ = canonical.selected_roots;
  base_prec_ = canonical.precision;
  stats_.seconds_base += seconds(start);
  progress() << "SICX-DECODE base path certified at " << base_prec_ << " bits" << std::endl;
}

Tree Decoder::Impl::prune(Tree& full, const std::vector<std::vector<uint64_t>>& node_classes,
                          const Graph& graph, std::vector<uint64_t>& endpoint_class) {
  const unsigned height = static_cast<unsigned>(full.levels.size());
  std::vector<std::vector<char>> wanted(height + 1);
  wanted[0].assign(1, 1);
  for (unsigned i = 1; i <= height; ++i) wanted[i].assign(full.levels[i - 1].size(), 0);
  for (auto node : graph.endpoints) wanted[height][node_classes[height][node]] = 1;
  for (unsigned i = height; i; --i)
    for (uint64_t id = 0; id < wanted[i].size(); ++id)
      if (wanted[i][id]) wanted[i - 1][full.levels[i - 1][id].parent] = 1;
  Tree out{full.degrees, full.width, full.prec, {}, 0};
  std::vector<std::vector<uint64_t>> labels(height + 1);
  labels[0].assign(1, 0);
  for (unsigned i = 1; i <= height; ++i) {
    labels[i].assign(wanted[i].size(), UINT64_MAX);
    std::vector<Class> level;
    for (uint64_t old = 0; old < wanted[i].size(); ++old) {
      if (!wanted[i][old]) continue;
      auto& src = full.levels[i - 1][old];
      labels[i][old] = level.size();
      Class c;
      c.parent = labels[i - 1].at(src.parent);
      c.index = src.index;
      c.dual = std::move(src.dual);
      level.push_back(std::move(c));
    }
    out.levels.push_back(std::move(level));
  }
  endpoint_class.clear();
  for (auto node : graph.endpoints)
    endpoint_class.push_back(labels[height].at(node_classes[height][node]));
  return out;
}

}  // namespace sic::decode
