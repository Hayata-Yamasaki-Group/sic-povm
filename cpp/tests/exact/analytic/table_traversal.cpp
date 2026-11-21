// SPDX-License-Identifier: AGPL-3.0-or-later
#include "assertions.hpp"
#include "exact/analytic/table.hpp"

static void node_slabs() {
  sic::exact::Arithmetic ar(3);
  {
    sic::exact::Table reference(ar, 96, 1);
    reference.node_slab = 0;
    reference.sd.resize(reference.nstates());
    reference.batch_state(0);
    for (long slab : {1L, 7L, 113L, 1000000L}) {
      sic::exact::Table split(ar, 96, 1);
      split.node_slab = slab;
      split.sd.resize(split.nstates());
      split.batch_state(0);
      SIC_CHECK(split.sd[0].nodes == reference.sd[0].nodes);
      SIC_CHECK(split.sd[0].log2err == reference.sd[0].log2err);
      for (size_t q = 0; q < split.sd[0].L.size(); ++q) {
        SIC_CHECK(acb_is_finite(&split.sd[0].L[q]));
        SIC_CHECK(acb_overlaps(&split.sd[0].L[q], &reference.sd[0].L[q]));
        SIC_CHECK(acb_equal(&split.sd[0].S[q], &reference.sd[0].S[q]));
      }
    }
    sic::exact::Table planned(ar, 96, 1);
    planned.node_slab = 7;
    SIC_CHECK(planned.kernel_bytes(100000, 5, 2, 136) < reference.kernel_bytes(100000, 5, 2, 136));
    SIC_CHECK(planned.kernel_bytes(100000, 5, 2, 136) > planned.kernel_bytes(1000, 5, 2, 136));
  }
}

// SPDX-License-Identifier: AGPL-3.0-or-later
#include <array>

#include "assertions.hpp"
#include "exact/analytic/table.hpp"
#include "exact/group/cycle_labels.hpp"

static void compare_labels(const std::vector<long>& digits, long d) {
  const auto period = sic::exact::cycle_matrix(digits, d);
  for (long i = 0; i < d; ++i)
    for (long j = 0; j < d; ++j) {
      std::vector<sic::exact::GridPoint> sequential;
      long x = i, y = j;
      for (size_t visit = 0; visit < 3 * digits.size(); ++visit) {
        sequential.emplace_back(x, y);
        const long next = ((digits[visit % digits.size()] * x + y) % d + d) % d;
        y = (d - x) % d;
        x = next;
      }
      sic::exact::GridMatrix prefix;
      for (size_t l = 0; l < digits.size(); ++l) {
        const auto visits = sic::exact::cycle_visits(prefix, period, d);
        for (size_t k = 0; k < 3; ++k)
          SIC_CHECK(visits[k].apply(i, j, d) == sequential[k * digits.size() + l]);
        prefix =
            sic::exact::GridMatrix::multiply(sic::exact::GridMatrix::step(digits[l], d), prefix, d);
      }
    }
}

static void compare_tables(int exponent) {
  sic::exact::Arithmetic ar(exponent);
  sic::exact::Table streamed(ar, 96, 2), reference(ar, 96, 2);
  streamed.stream_states = true;
  reference.stream_states = false;
  reference.build_batched();
  for (int c = 0; c < ar.h(); ++c) {
    streamed.build_class(c);
    for (long q = 1; q < ar.d * ar.d; ++q)
      SIC_CHECK(acb_overlaps(&streamed.val[c][q], &reference.val[c][q]));
    for (int l = 0; l < (int)ar.cls[c].digit.size(); ++l) {
      SIC_CHECK(streamed.sd[streamed.soff[c] + l].L.empty());
      SIC_CHECK(streamed.sd[streamed.soff[c] + l].S.empty());
    }
    // Repeated builds must reset both accumulators, not multiply a prior value.
    acb_t previous;
    acb_init(previous);
    acb_set(previous, &streamed.val[c][1]);
    streamed.build_class(c);
    SIC_CHECK(acb_equal(previous, &streamed.val[c][1]));
    acb_clear(previous);
  }
}

static void streamed_cycle() {
  {
    sic::exact::Arithmetic ar(3);
    sic::exact::Table refused(ar, 96, 1);
    size_t proposed = 0;
    refused.admit_workspace = [&](sic::support::resources::Bytes bytes) {
      proposed = bytes;
      throw std::runtime_error("test kernel admission refusal");
    };
    bool rejected = false;
    try {
      refused.build_class(0);
    } catch (const std::runtime_error& error) {
      rejected = std::string(error.what()) == "test kernel admission refusal";
    }
    SIC_CHECK(rejected);
    SIC_CHECK(proposed > 0);
    SIC_CHECK(refused.maximum_nodes == 0);
  }
  compare_labels({2, 3, 5}, 8);  // noncommuting prefixes; M^3 need not be identity
  compare_labels({7, 2, 4, 3}, 16);
  for (int exponent : {3, 4, 5}) {
    sic::exact::Arithmetic ar(exponent);
    for (const auto& cls : ar.cls) compare_labels(cls.digit, ar.d);
  }
  compare_tables(3);
  compare_tables(4);
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  if (std::string(argv[1]) == "nodes")
    node_slabs();
  else if (std::string(argv[1]) == "cycles")
    streamed_cycle();
  else
    return 2;
  return sic_test::report();
}
