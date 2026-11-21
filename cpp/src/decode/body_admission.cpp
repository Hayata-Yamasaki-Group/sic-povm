// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decode/decoder.hpp"
namespace sic::decode {
Decoder::Impl::BodyAdmission Decoder::Impl::plan_body_admission(const BodyPlan& body,
                                                                const Tree& tree,
                                                                size_t first) const {
  const uint64_t room = budget_.available();
  BodyAdmission result{first + 1, body.reserve[first], 0, 0};
  while (result.last < body.reps.size() &&
         checked_add(checked_add(result.reserve, body.reserve[result.last]), body.largest) <= room)
    result.reserve = checked_add(result.reserve, body.reserve[result.last++]);
  if (result.reserve > room || body.largest > room - result.reserve)
    throw sic::Error(ErrorKind::resource, "memory budget too small for one BODY chunk");
  uint64_t jobs = 0, carry = 0;
  for (size_t r = first; r < result.last; ++r) {
    jobs = checked_add(jobs, body.jobs[r].size());
    const auto& e = meta_.elements[body.rep_ids[r]];
    const uint64_t scratch = checked_add(
        4ull << 20,
        checked_mul(256, (checked_add(e.max_bits, static_cast<uint64_t>(tree.prec)) + 127) / 64));
    carry = std::max(carry, checked_add(carry_bytes(tree, body.level[r]), scratch));
  }
  result.scheduling = checked_mul(jobs, 4 * sizeof(BodyJob) + 2 * sizeof(PhysicalTask) + 128);
  if (result.scheduling > room - result.reserve - body.largest)
    throw sic::Error(ErrorKind::resource, "memory budget too small for BODY scheduling metadata");
  result.carry = checked_mul(carry, std::min<uint64_t>(pool_->physical_parallelism(), jobs));
  return result;
}
}  // namespace sic::decode
