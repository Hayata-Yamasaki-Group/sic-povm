// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decode/contraction.hpp"

#include "decode/decoder.hpp"
#include "decode/input/element_source.hpp"
#include "sic/error.hpp"
#include "support/resources/platform_snapshot.hpp"

namespace sic::decode {
void Carry::feed(uint64_t position, unsigned first, acb_srcptr lower, bool zero) {
  for (unsigned i = first; i < high_; ++i) {
    const unsigned k = i - low_, p = t_.degrees[i];
    const unsigned digit = static_cast<unsigned>((position / rel(i)) % p);
    if (digit && !open_[k]) throw std::logic_error("carry: higher digit without an open block");
    if (!digit) std::fill(present_[k].begin(), present_[k].end(), 0);
    if (!zero) {
      ComplexBallVector& kid = kids_[k][digit];
      if (i > first)
        std::swap(kid, sums_[k - 1]);  // the vector just produced for level i
      else {
        const uint64_t n = t_.count(i);
        if (kid.size() != n) kid = ComplexBallVector(n);
        for (uint64_t j = 0; j < n; ++j) acb_set(kid[j], lower + j);
      }
      present_[k][digit] = 1;
    }
    open_[k] = 1;
    if (digit + 1 < p) return;
    open_[k] = 0;
    acb_srcptr child[kMaxDegree];
    bool any = false;
    for (unsigned d = 0; d < p; ++d) {
      child[d] = present_[k][d] ? kids_[k][d].data() : nullptr;
      any = any || present_[k][d];
    }
    if (!any) {
      zero = true;
      continue;
    }
    const auto& classes = t_.levels[i];
    ComplexBallVector& out = sums_[k];
    if (out.size() != classes.size()) out = ComplexBallVector(classes.size());
    for (size_t j = 0; j < classes.size(); ++j)
      combine_class(out[j], classes[j], child, p, t_.prec);
    zero = false;
  }
  const uint64_t n = t_.count(high_);
  if (zero) {
    result_ = ComplexBallVector(n);
  } else if (first < high_)
    result_ = std::move(sums_[high_ - 1 - low_]);
  else {
    result_ = ComplexBallVector(n);
    for (uint64_t j = 0; j < n; ++j) acb_set(result_[j], lower + j);
  }
  complete_ = true;
}

Carry::Carry(const Tree& tree, unsigned low, unsigned high)
    : t_(tree),
      low_(low),
      high_(high),
      open_(high - low, 0),
      sums_(high - low),
      total_(tree.width[high] / tree.width[low]) {
  for (unsigned i = low; i < high; ++i) {
    if (tree.degrees[i] > kMaxDegree)
      throw sic::Error(sic::ErrorKind::invalid_format,
                       "tower degree above 31 (docs/sicx.md#spec-31-the-tower)");
    kids_.emplace_back(tree.degrees[i]);
    present_.emplace_back(tree.degrees[i], 0);
  }
}

void Carry::zeros_until(uint64_t position) {
  if (position < visited_ || position > total_)
    throw sic::Error(sic::ErrorKind::invalid_format, "carry: invalid zero run");
  while (visited_ < position) {
    unsigned s = low_;
    while (s < high_ && rel(s + 1) <= position - visited_ && visited_ % rel(s + 1) == 0) ++s;
    for (unsigned i = low_; i < s; ++i) {
      if (open_[i - low_]) throw std::logic_error("carry: zero run crosses an open block");
    }
    feed(visited_, s, nullptr, true);
    visited_ += rel(s);
  }
}

const ComplexBallVector& Carry::finish() {
  zeros_until(total_);
  if (!complete_) throw std::logic_error("carry: incomplete");
  for (const auto& v : result_.span())
    if (!acb_is_finite(&v)) throw PrecisionRefusal("nonfinite contraction");
  return result_;
}

void Decoder::Impl::lift(const Tree& tree, std::vector<Sparse>& v, std::vector<unsigned>& level,
                         unsigned height) {
  unsigned low = height;
  for (auto l : level) low = std::min(low, l);
  for (unsigned j = low; j < height; ++j) {
    const unsigned p = tree.degrees[j];
    const uint64_t classes = tree.count(j + 1);
    if (p > kMaxDegree)
      throw sic::Error(sic::ErrorKind::invalid_format,
                       "tower degree above 31 (docs/sicx.md#spec-31-the-tower)");
    std::vector<size_t> active;
    for (size_t e = 0; e < v.size(); ++e)
      if (level[e] == j) active.push_back(e);
    if (active.empty()) continue;
    struct Out {
      size_t a;
      size_t slot;
      size_t lo, hi;
    };  // children [lo, hi) of next[a].pos[slot]
    std::vector<Out> outs;
    std::vector<Sparse> next(active.size());
    for (size_t a = 0; a < active.size(); ++a) {
      const auto& children = v[active[a]].pos;
      for (size_t lo = 0; lo < children.size();) {
        const uint64_t q = children[lo] / p;
        size_t hi = lo + 1;
        while (hi < children.size() && children[hi] / p == q) ++hi;
        outs.push_back({a, next[a].pos.size(), lo, hi});
        next[a].pos.push_back(q);
        lo = hi;
      }
      next[a].val.resize(next[a].pos.size());
    }
    pool_->run(
        outs.size(),
        [&](size_t k) { next[outs[k].a].val[outs[k].slot] = ComplexBallVector(classes); }, 2, true);
    constexpr uint64_t kChunk = 256;
    const uint64_t per = (classes + kChunk - 1) / kChunk;
    const auto& level_classes = tree.levels[j];
    pool_->run(
        outs.size() * per,
        [&](size_t k) {
          const Out& o = outs[k / per];
          const uint64_t c0 = (k % per) * kChunk, c1 = std::min<uint64_t>(classes, c0 + kChunk);
          const Sparse& children = v[active[o.a]];
          acb_srcptr child[kMaxDegree] = {};
          for (size_t x = o.lo; x < o.hi; ++x) child[children.pos[x] % p] = children.val[x].data();
          ComplexBallVector& out = next[o.a].val[o.slot];
          for (uint64_t c = c0; c < c1; ++c)
            combine_class(out[c], level_classes[c], child, p, tree.prec);
        },
        2, true);
    for (size_t a = 0; a < active.size(); ++a) release_parallel(v[active[a]].val);
    for (size_t a = 0; a < active.size(); ++a) {
      v[active[a]] = std::move(next[a]);
      level[active[a]] = j + 1;
    }
  }
}

ComplexBallVector Decoder::Impl::top_value(Sparse& v, uint64_t classes) {
  if (!v.pos.empty() && (v.pos.size() != 1 || v.pos[0] != 0))
    throw std::logic_error("lift did not reach the top");
  ComplexBallVector out = v.pos.empty() ? ComplexBallVector(classes) : std::move(v.val[0]);
  for (const auto& z : out.span())
    if (!acb_is_finite(&z)) throw PrecisionRefusal("nonfinite contraction");
  return out;
}

uint64_t Decoder::Impl::lift_bytes(const Tree& tree, uint64_t occupied, size_t elements, unsigned L,
                                   unsigned height) const {
  auto positions = [&](unsigned j) {
    return std::min<uint64_t>(elements * (tree.width[height] / tree.width[j]), occupied);
  };
  uint64_t worst = 0;
  for (unsigned j = L; j <= height; ++j) {
    const uint64_t here = positions(j) * av_bytes(tree.count(j), tree.prec);
    const uint64_t up = j < height ? positions(j + 1) * av_bytes(tree.count(j + 1), tree.prec) : 0;
    worst = std::max(worst, here + up);
  }
  return worst + 2 * positions(L) * (sizeof(ComplexBallVector) + 16);
}

uint64_t Decoder::Impl::carry_bytes(const Tree& tree, unsigned L) const {
  uint64_t n = 2;
  for (unsigned i = 0; i < L; ++i)
    n += tree.count(i + 1) + static_cast<uint64_t>(tree.degrees[i]) * tree.count(i);
  return av_bytes(n + tree.count(L), tree.prec) + 4096;
}

uint64_t Decoder::Impl::nonempty_blocks(const std::vector<size_t>& ids, size_t first, size_t group,
                                        uint64_t width, uint64_t cap) const {
  uint64_t blocks = 0;
  for (size_t e = first; e < first + group; ++e) {
    const auto& leaves = tower_[ids[e]];
    if (!leaves.loaded) {
      blocks += reader_.element_blocks(ids[e]).size();
      continue;
    }  // streamed: chunk count
    // Jump from block to block (stops once `cap` blocks are found).
    for (auto it = leaves.pos.begin(); it != leaves.pos.end() && blocks < cap;) {
      ++blocks;
      it = std::lower_bound(it, leaves.pos.end(), (*it / width + 1) * width);
    }
  }
  return blocks;
}

uint64_t Decoder::Impl::occupied(const std::vector<size_t>& ids, size_t first, size_t group,
                                 uint64_t width) const {
  for (size_t e = first; e < first + group; ++e)
    if (!tower_[ids[e]].loaded) return UINT64_MAX;  // streamed, not yet parsed: dense bound
  constexpr uint64_t kCap = uint64_t(1) << 20;
  const uint64_t n = nonempty_blocks(ids, first, group, width, kCap);
  return n >= kCap ? UINT64_MAX : n;
}

std::vector<ComplexBallVector> Decoder::Impl::contract_many(const Tree& tree,
                                                            const std::vector<size_t>& ids,
                                                            unsigned height) {
  const unsigned workers = pool_->size();
  std::vector<ComplexBallVector> result(ids.size());
  // Results of finished groups stay reserved until the caller takes them over.
  std::vector<std::unique_ptr<ReserveGuard>> finished;
  size_t first = 0;
  // Elements together: all of them unless the arrays do not fit (then fewer).
  while (first < ids.size()) {
    size_t group = ids.size() - first;
    unsigned L = height;
    auto streamed_bytes = [&](size_t g) {
      uint64_t bytes = 0;
      if (!stats_.tower_resident)
        for (size_t e = first; e < first + g; ++e)
          bytes += kParsedPerPayload * tower_[ids[e]].payload_bytes +
                   64 * reader_.element_blocks(ids[e]).size();
      return bytes;
    };
    // Streamed tower: parse the group first (reserved), then plan on the real
    // occupancy; a plan that does not fit releases the group and halves it.
    std::unique_ptr<ReserveGuard> stream_guard;
    std::vector<size_t> group_ids;
    struct StreamedGroup {
      std::vector<ElementLeaves>& tower;
      const std::vector<size_t>& ids;
      const std::unique_ptr<ReserveGuard>& reservation;
      void clear() const {
        if (reservation)
          for (size_t id : ids) tower[id].clear();
      }
      ~StreamedGroup() { clear(); }
    } streamed_group{tower_, group_ids, stream_guard};
    for (;;) {
      group_ids.assign(ids.begin() + static_cast<long>(first),
                       ids.begin() + static_cast<long>(first + group));
      if (!stats_.tower_resident) {
        const uint64_t streamed = streamed_bytes(group);
        if (streamed > budget_.available() && group > 1) {
          group = std::max<size_t>(1, group / 2);
          ++stats_.budget_fallbacks;
          continue;
        }
        stream_guard = std::make_unique<ReserveGuard>(budget_, streamed, "streamed tower elements");
        load_elements(group_ids);
      }
      // Lowest cut level with enough nonempty subtree tasks for the workers
      // (sub-layout elements occupy only some subtrees).
      L = height;
      // Extra workers share the default-width coarse subtrees; admission still
      // bounds the full requested concurrency below.
      const unsigned planning_workers = std::min(workers, 32u);
      while (L > 0 && nonempty_blocks(ids, first, group, tree.width[L], 8ull * planning_workers) <
                          8ull * planning_workers)
        --L;
      if (workers == 1) L = height;
      const uint64_t room = budget_.available();
      // If the arrays plus one task per worker do not fit at the preferred cut,
      // take the fitting cut nearest to it (higher cuts first: fewer arrays, but
      // larger carry tasks); with no fitting cut, fewer elements together.
      auto need = [&](unsigned l, uint64_t tasks) {
        return lift_bytes(tree, occupied(ids, first, group, tree.width[l]), group, l, height) +
               carry_bytes(tree, l) * tasks;
      };
      if (need(L, workers) > room) {
        const unsigned preferred = L;
        bool found = false;
        for (unsigned distance = 1; distance <= height && !found; ++distance) {
          if (preferred + distance <= height && need(preferred + distance, workers) <= room) {
            L = preferred + distance;
            found = true;
          } else if (distance <= preferred && need(preferred - distance, workers) <= room) {
            L = preferred - distance;
            found = true;
          }
        }
        if (!found) {  // one task at a time (admission serializes them)
          for (unsigned distance = 0; distance <= height && !found; ++distance) {
            if (preferred + distance <= height && need(preferred + distance, 1) <= room) {
              L = preferred + distance;
              found = true;
            } else if (distance && distance <= preferred && need(preferred - distance, 1) <= room) {
              L = preferred - distance;
              found = true;
            }
          }
        }
        if (found) ++stats_.budget_fallbacks;
      }
      if (need(L, 1) <= room || group == 1) break;
      if (!stats_.tower_resident) {
        streamed_group.clear();
        stream_guard.reset();
      }
      group = std::max<size_t>(1, group / 2);
      ++stats_.budget_fallbacks;
    }
    std::vector<Sparse> v(group);
    std::vector<unsigned> level(group, L);
    struct Job {
      size_t e;
      size_t slot;
      uint64_t b;
      size_t q0, q1;
    };
    std::vector<Job> jobs;
    const uint64_t wL = tree.width[L];
    for (size_t e = 0; e < group; ++e) {
      const auto& leaves = tower_[group_ids[e]];
      if (!leaves.pos.empty() && leaves.pos.back() >= tree.width[height])
        fail(sic::ErrorKind::invalid_format, "element leaf beyond its field");
      for (auto it = leaves.pos.begin(); it != leaves.pos.end();) {
        const uint64_t b = *it / wL;
        const auto hi = std::lower_bound(it, leaves.pos.end(), (b + 1) * wL);
        jobs.push_back({e, v[e].pos.size(), b, static_cast<size_t>(it - leaves.pos.begin()),
                        static_cast<size_t>(hi - leaves.pos.begin())});
        v[e].pos.push_back(b);
        it = hi;
      }
      v[e].val.resize(v[e].pos.size());
    }
    const uint64_t arrays =
        lift_bytes(tree, occupied(ids, first, group, tree.width[L]), group, L, height);
    if (options_.profile || arrays > budget_.available())
      progress() << "SICX-PROFILE   plan elements=" << group << "/" << ids.size()
                 << " height=" << height << " cut=" << L << " arrays=" << (arrays >> 20)
                 << "MiB available=" << (budget_.available() >> 20) << "MiB" << std::endl;
    auto guard = std::make_unique<ReserveGuard>(budget_, arrays, "level-parallel arrays");
    const uint64_t task = carry_bytes(tree, L);
    const size_t grain = pool_->compute_group_size(jobs.size(), 4);
    const size_t count = (jobs.size() + grain - 1) / grain;
    // One bounded carry per concurrent group; tight budgets still serialize
    // groups through admission. No reservation grows with sequential job count.
    pool_->run(
        count,
        [&](size_t group_job) {
          Admission admission(budget_, task);
          const size_t end_job = std::min(jobs.size(), (group_job + 1) * grain);
          for (size_t k = group_job * grain; k < end_job; ++k) {
            sic::support::check_stop();
            const Job& job = jobs[k];
            const auto& leaves = tower_[group_ids[job.e]];
            LeafConverter convert(meta_.D0, tree.prec, meta_.elements[group_ids[job.e]].max_bits);
            Carry carry(tree, 0, L);
            ComplexBallVector value(1);
            const uint64_t base = job.b * wL;
            for (size_t q = job.q0; q < job.q1; ++q) {
              carry.zeros_until(leaves.pos[q] - base);
              convert(value[0], leaves.value[q]->x, leaves.value[q]->y);
              carry.value(leaves.pos[q] - base, value[0]);
            }
            v[job.e].val[job.slot] = carry.release();
          }
        },
        2, true);
    lift(tree, v, level, height);
    for (size_t e = 0; e < group; ++e) result[first + e] = top_value(v[e], tree.count(height));
    v.clear();
    guard.reset();  // the arrays are gone; only the results remain
    if (!stats_.tower_resident) {
      streamed_group.clear();
      stream_guard.reset();
      sic::support::resources::release_free_heap();
    }
    if (first + group < ids.size())
      finished.push_back(std::make_unique<ReserveGuard>(
          budget_, group * av_bytes(tree.count(height), tree.prec), "finished element values"));
    if (options_.profile)
      progress() << "SICX-PROFILE   contract elements=" << group << " height=" << height
                 << " cut=" << L << " tasks=" << jobs.size() << " classes=" << tree.count(height)
                 << std::endl;
    first += group;
  }
  return result;
}

}  // namespace sic::decode
