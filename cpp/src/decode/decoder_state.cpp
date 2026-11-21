// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/resources/platform_snapshot.hpp"
#include "decode/decoder.hpp"
#include "decode/input/element_source.hpp"

namespace sic::decode {
void Decoder::Impl::check_owner() const {
  if (owner_ != std::this_thread::get_id())
    throw sic::Error(ErrorKind::invalid_argument, "decoder owner-thread violation");
}

std::ostream& Decoder::Impl::progress() const {
  static thread_local std::ostringstream discarded;
  discarded.str({});
  return options_.progress ? *options_.progress : discarded;
}
}  // namespace sic::decode

namespace sic::decode {
const Stats& Decoder::Impl::stats() const {
  check_owner();
  stats_.budget_peak = budget_.peak();
  stats_.budget_waits = budget_.waits();
  stats_.budget_peak_per_worker = budget_.peak_per_worker();
  stats_.budget_rss_waits = budget_.rss_waits();
  stats_.bind_failures = pool_->bind_failures();
  return stats_;
}

const Placement& Decoder::Impl::placement() const { return place_; }

slong Decoder::Impl::base_precision() const { return base_prec_; }

unsigned Decoder::Impl::pool_groups() const { return pool_->groups(); }

bool Decoder::Impl::accurate(acb_srcptr v) const {
  return acb_is_finite(v) && (acb_rel_accuracy_bits(v) >= options_.target ||
                              (mag_cmp_2exp_si(arb_radref(acb_realref(v)), -options_.target) <= 0 &&
                               mag_cmp_2exp_si(arb_radref(acb_imagref(v)), -options_.target) <= 0));
}

double Decoder::Impl::seconds(std::chrono::steady_clock::time_point s) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - s).count();
}

void Decoder::Impl::rebase() {
  const auto start = std::chrono::steady_clock::now();
  uint64_t rss = resident_bytes();
  if (rss > budget_.budget() / 2) {
    sic::support::resources::release_free_heap();
    rss = resident_bytes();
  }
  budget_.rebase(checked_add(rss, reader_.source_memory_reservation()));
  stats_.seconds_rebase += seconds(start);
}

void Decoder::Impl::load_elements(const std::vector<size_t>& ids) {
  std::vector<std::pair<size_t, size_t>> jobs;  // (element, block)
  for (auto id : ids) {
    if (tower_[id].loaded) continue;
    tower_[id].blocks.clear();
    tower_[id].blocks.resize(reader_.element_blocks(id).size());
    for (size_t b = 0; b < reader_.element_blocks(id).size(); ++b) jobs.push_back({id, b});
  }
  pool_->run(jobs.size(), [&](size_t q) {
    const auto [id, b] = jobs[q];
    tower_[id].blocks[b] = decode_leaf_block(reader_, reader_.element_blocks(id)[b]);
  });
  for (auto id : ids) {
    if (tower_[id].loaded) continue;
    tower_[id].index();
    for (const auto& b : tower_[id].blocks) stats_.tower_bytes_read += b.bytes;
  }
}

uint64_t Decoder::Impl::payload_of(size_t id) const {
  uint64_t bytes = 0;
  for (auto c : reader_.element_blocks(id)) bytes += reader_.leaf_entry(c).length;
  return bytes;
}

void Decoder::Impl::load_tower() {
  const auto start = std::chrono::steady_clock::now();
  tower_.resize(meta_.elements.size());
  std::vector<size_t> ids;
  uint64_t payload = 0;
  for (size_t id = 0; id < meta_.elements.size(); ++id) {
    const auto& e = meta_.elements[id];
    element_id_[{e.kind, e.level, e.axis, e.k}] = id;
    if (e.kind == "rep") continue;
    ids.push_back(id);
    tower_[id].payload_bytes = payload_of(id);
    payload += tower_[id].payload_bytes;
  }
  rebase();
  // Tight budget (below twice the measured unbudgeted peak, about 17x the tower
  // payload at d256-d1024): at most 8 glibc arenas, since per-thread free lists
  // otherwise hold memory outside the accounting (measured overshoot 0.8-8 % with
  // 64 arenas). Not by default: the cap costs 2.5x wall time at 64 workers.
  if (options_.tune_process_allocator && budget_.limit() < 34 * payload) {
    mallopt(M_ARENA_MAX, 8);
    stats_.arenas_capped = true;
  }
  const bool fits = kParsedPerPayload * payload <= budget_.available() / 2;
  stats_.tower_resident = options_.tower_resident < 0 ? fits : options_.tower_resident == 1;
  if (stats_.tower_resident)
    load_elements(ids);
  else
    ++stats_.budget_fallbacks;
  stats_.seconds_tower_load += seconds(start);
  if (options_.profile)
    progress() << "SICX-PROFILE tower payload=" << payload << " resident=" << stats_.tower_resident
               << " rss=" << resident_bytes() << " t=" << seconds(start) << std::endl;
}

bool Decoder::Impl::has_element(const std::string& kind, unsigned level, int64_t axis,
                                int64_t k) const {
  return element_id_.contains({kind, level, axis, k});
}

size_t Decoder::Impl::element_id(const std::string& kind, unsigned level, int64_t axis,
                                 int64_t k) const {
  return element_id_.at({kind, level, axis, k});
}

void Decoder::Impl::release_parallel(const std::vector<ComplexBallVector*>& vectors) {
  struct Range {
    ComplexBallVector* v;
    size_t a, b;
  };
  std::vector<Range> ranges;
  for (auto* v : vectors)
    for (size_t a = 0; a < v->size(); a += 8192)
      ranges.push_back({v, a, std::min(v->size(), a + 8192)});
  pool_->run(
      ranges.size(),
      [&](size_t k) {
        for (size_t z = ranges[k].a; z < ranges[k].b; ++z) {
          acb_clear(&ranges[k].v->v[z]);
          acb_init(&ranges[k].v->v[z]);
        }
      },
      2, true);
}

void Decoder::Impl::release_parallel(std::vector<ComplexBallVector>& vectors) {
  std::vector<ComplexBallVector*> big;
  for (auto& v : vectors)
    if (v.size() >= 8192) big.push_back(&v);
  release_parallel(big);
}

void Decoder::Impl::release_tree(Tree& tree) {
  for (auto& level : tree.levels) {
    pool_->run((level.size() + 4095) / 4096,
               [&](size_t k) {
                 for (size_t c = k * 4096; c < std::min(level.size(), (k + 1) * 4096); ++c) {
                   level[c].dual = ComplexBallVector();
                 }
               },
               2, true);
  }
  tree.levels.clear();
}
}  // namespace sic::decode
