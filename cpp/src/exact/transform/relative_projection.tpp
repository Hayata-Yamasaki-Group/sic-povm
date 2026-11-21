// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact {
template <class Value>
void relative_projection_at(acb_ptr out, const Group& group, std::span<const long> subgroup,
                            long tau, long anchor, slong precision, const Value& value) {
  sic::support::ComplexBallVector work(2);
  acb_zero(out);
  const long other = group.mul(anchor, tau);
  for (long h : subgroup) {
    value(group.mul(anchor, h), work[0]);
    value(group.mul(other, h), work[1]);
    acb_sub(work[0], work[0], work[1], precision);
    acb_add(out, out, work[0], precision);
  }
}

}  // namespace sic::exact
