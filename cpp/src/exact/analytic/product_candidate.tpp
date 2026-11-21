// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact {
template <class BaseValue>
void ProductCandidate::evaluate(acb_ptr out, slong wp, BaseValue&& base_value) const {
  sic::support::ComplexBallVector work(2);
  acb_one(out);
  for (const auto& factor : factors) {
    acb_set_si(work[0], factor[0]);
    for (size_t j = 1; j < factor.size(); ++j) {
      base_value(j - 1, work[1]);
      acb_addmul_si(work[0], work[1], factor[j], wp);
    }
    acb_mul(out, out, work[0], wp);
  }
}

}  // namespace sic::exact
