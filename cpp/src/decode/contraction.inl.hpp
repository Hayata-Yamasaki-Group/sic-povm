// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::decode {
[[gnu::always_inline]] inline void combine_class(acb_ptr out, const Class& cls,
                                                 const acb_srcptr* child, unsigned p, slong prec) {
  acb_struct xs[kMaxDegree], ys[kMaxDegree];  // shallow copies, read only by acb_dot
  slong n = 0;
  for (unsigned d = 0; d < p; ++d)
    if (child[d]) {
      xs[n] = child[d][cls.parent];
      ys[n] = cls.dual.v[d];
      ++n;
    }
  acb_dot(out, nullptr, 0, xs, 1, ys, 1, n, prec);
}

inline uint64_t Carry::rel(unsigned i) const { return t_.width[i] / t_.width[low_]; }

inline void Carry::value(uint64_t position, acb_srcptr lower) {
  if (complete_ || position != visited_ || position >= total_)
    throw sic::Error(sic::ErrorKind::invalid_format, "carry: nonsequential input");
  feed(position, low_, lower, false);
  ++visited_;
}

}  // namespace sic::decode
