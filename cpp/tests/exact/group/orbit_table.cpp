// SPDX-License-Identifier: AGPL-3.0-or-later
#include "assertions.hpp"
#include "exact/analytic/table.hpp"
#include "exact/group/group.hpp"

int main() {
  for (int exponent : {3, 4}) {
    sic::exact::Arithmetic ar(exponent);
    sic::exact::Group group(ar);
    auto layout = std::make_shared<sic::exact::OrbitTableLayout>(ar.d, ar.h(), group.orbit);
    sic::exact::Table dense(ar, 96), compact(ar, 96);
    compact.enable_compact(layout);
    dense.build_batched();
    compact.build_batched();
    SIC_CHECK(compact.compact());
    sic::support::ComplexBallVector value(1);
    for (int c = 0; c < ar.h(); ++c) {
      SIC_CHECK(compact.val[c].empty());
      SIC_CHECK(compact.packed[c].size() == (size_t)((ar.d * ar.d - 1) / 3));
      for (long q = 1; q < ar.d * ar.d; ++q) {
        const long point = c * ar.d * ar.d + q;
        compact.read(value[0], point);
        const long canonical = group.orbit[point];
        SIC_CHECK(acb_equal(value[0], &dense.val[c][canonical % (ar.d * ar.d)]));
        SIC_CHECK(acb_overlaps(value[0], &dense.val[c][q]));
      }
    }
    compact.build_class(0);
    compact.read(value[0], 1);
    SIC_CHECK(acb_equal(value[0], &dense.val[0][1]));

    sic::exact::Table sparse(ar, 96);
    sparse.enable_compact(layout);
    sparse.build_class(0);
    bool rejected = false;
    try {
      sparse.read(value[0], ar.d * ar.d + 1);
    } catch (const std::out_of_range&) {
      rejected = true;
    }
    SIC_CHECK(rejected);
    std::vector<char> mask(ar.h(), 0);
    mask[1] = 1;
    auto source = [&](int point) { return group.orbit[point % (ar.d * ar.d)]; };
    layout->pair_classes(mask, 123, source);
    sparse.paired_values = true;
    sic::support::ComplexBallVector expected(1);
    for (long q = 1; q < ar.d * ar.d; ++q) {
      const long point = ar.d * ar.d + q;
      sparse.read(value[0], point);
      const long mapped = source(group.orbit[point]);
      acb_conj(expected[0], &dense.val[0][mapped]);
      SIC_CHECK(acb_equal(value[0], expected[0]));
    }
    SIC_CHECK(sparse.packed[1].size() == 0);
  }
  return sic_test::report();
}
