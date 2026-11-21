// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decode/decoder.hpp"
#include "decode/input/element_source.hpp"
namespace sic::decode {
Decoder::Impl::BodyPlan Decoder::Impl::plan_body(const std::vector<Request>& requests,
                                                 const Tree& out_tree, slong wp) {
  const unsigned height = meta_.levels();
  BodyPlan result;
  auto& reps = result.reps;
  auto& rep_ids = result.rep_ids;
  for (const auto& [position, id] : meta_.representative) {
    bool used = false;
    for (const auto& r : requests) used = used || r.representative == position;
    if (used) {
      reps.push_back(position);
      rep_ids.push_back(id);
    }
  }
  result.parts.resize(reps.size());
  auto& parts = result.parts;
  result.level.resize(reps.size());
  auto& level = result.level;
  result.jobs.resize(reps.size());
  auto& rep_jobs = result.jobs;
  result.reserve.resize(reps.size());
  auto& rep_reserve = result.reserve;

  for (size_t r = 0; r < reps.size(); ++r) {
    const auto& e = meta_.elements[rep_ids[r]];
    level[r] = e.block_level;
    std::vector<std::pair<uint64_t, size_t>> order;  // (position at the block level, chunk)
    for (auto c : reader_.element_blocks(rep_ids[r]))
      order.push_back(
          {e.layout.position(reader_.leaf_entry(c).first) / out_tree.width[e.block_level], c});
    std::sort(order.begin(), order.end());
    for (const auto& [b, c] : order) {
      if (!parts[r].pos.empty() && parts[r].pos.back() == b)
        fail(sic::ErrorKind::invalid_format, "two BODY chunks for one block");
      const auto& owner = reader_.index()[reader_.leaf_owner(c)];
      const uint64_t framing = checked_add(
          256, checked_mul(owner.leaf_count, 2 * sizeof(LeafSpan) + 2 * sizeof(LogicalEntry)));
      const uint64_t scratch = checked_add(
          4ull << 20,
          checked_mul(256,
                      (checked_add(e.max_bits, static_cast<uint64_t>(out_tree.prec)) + 127) / 64));
      const uint64_t bytes = checked_add(
          checked_mul(4, owner.length),
          checked_add(framing, checked_add(carry_bytes(out_tree, e.block_level), scratch)));
      rep_jobs[r].push_back({r, c, parts[r].pos.size(), b, bytes});
      parts[r].pos.push_back(b);
      result.largest = std::max(result.largest, bytes);
    }
    result.chunks += order.size();
    rep_reserve[r] = parts[r].pos.size() * av_bytes(out_tree.count(e.block_level), wp) +
                     lift_bytes(out_tree, parts[r].pos.size(), 1, level[r], height);
  }
  return result;
}
}  // namespace sic::decode

namespace sic::decode {
std::vector<Decoder::Impl::PhysicalTask> Decoder::Impl::physical_tasks(
    const std::vector<BodyJob>& jobs) {
  std::vector<PhysicalTask> physical;
  std::map<size_t, size_t> task_at;
  for (const auto& job : jobs) {
    const auto owner = reader_.leaf_owner(job.chunk);
    auto [it, inserted] = task_at.emplace(owner, physical.size());
    if (inserted) physical.push_back({owner, {}, 0});
    auto& task = physical[it->second];
    task.leaves.push_back(job);
    task.bytes = std::max(task.bytes, job.bytes);
  }
  return physical;
}
}  // namespace sic::decode
