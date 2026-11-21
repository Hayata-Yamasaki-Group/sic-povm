// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/transform/quadratic_transform.hpp"

#include "assertions.hpp"
#include "sic/support/ball_vectors.hpp"

struct Reals {
  std::vector<arb_struct> value;
  explicit Reals(long size) : value(size) {
    for (auto& x : value) arb_init(&x);
  }
  ~Reals() {
    for (auto& x : value) arb_clear(&x);
  }
};

int main() {
  for (int levels = 0; levels <= 10; ++levels) {
    const long block = 1L << levels, count = 3 * block;
    sic::support::ComplexBallVector reference(count), scratch(count), actual(count), duals(block),
        work(2);
    for (long c = 0; c < block; ++c) {
      acb_set_si(duals[c], c % 13 + 1);
      arb_set_si(acb_imagref(duals[c]), c % 7 - 3);
      acb_div_si(duals[c], duals[c], 17, 96);
    }
    auto dual = [&](acb_ptr out, int i, long c) { acb_set(out, duals[(1L << i) + c]); };
    for (long q = 0; q < count; ++q) {
      acb_set_si(reference[q], q % 29 - 13);
      arb_set_si(acb_imagref(reference[q]), q % 19 - 7);
      acb_div_si(reference[q], reference[q], 13, 96);
      arb_add_error_2exp_si(acb_realref(reference[q]), -82);
      arb_add_error_2exp_si(acb_imagref(reference[q]), -82);
      acb_set(actual[q], reference[q]);
    }
    for (int i = 0; i < levels; ++i) {
      const long ni = 1L << i;
      for (long base = 0; base < count; base += 2 * ni)
        for (long c = 0; c < ni; ++c) {
          acb_mul_2exp_si(work[0], reference[base + c], -1);
          dual(work[1], i, c);
          acb_mul(work[1], reference[base + ni + c], work[1], 96);
          acb_add(scratch[base + 2 * c], work[0], work[1], 96);
          acb_sub(scratch[base + 2 * c + 1], work[0], work[1], 96);
        }
      reference.v.swap(scratch.v);
    }
    sic::exact::quadratic_complex_inverse(actual.v, levels, 96, 2, dual);
    for (long q = 0; q < count; ++q) SIC_CHECK(acb_equal(actual[q], reference[q]));
  }
  for (int levels = 0; levels <= 10; ++levels) {
    const long block = 1L << levels, count = 3 * block;
    Reals reference(count), scratch(count), fused(count), separate(count), resident_result(count);
    Reals generators(block);
    for (long j = 0; j < block; ++j) {
      arb_set_si(&generators.value[j], j % 11 - 5);
      arb_div_ui(&generators.value[j], &generators.value[j], 7, 96);
      arb_add_error_2exp_si(&generators.value[j], -80);
    }
    auto generator = [&](int i, long c) { return &generators.value[(1L << i) + c]; };
    for (long q = 0; q < count; ++q) {
      arb_set_si(&reference.value[q], q % 29 - 13);
      arb_div_ui(&reference.value[q], &reference.value[q], 13, 96);
      arb_add_error_2exp_si(&reference.value[q], -82);
      arb_set(&fused.value[q], &reference.value[q]);
      arb_set(&separate.value[q], &reference.value[q]);
      arb_set(&resident_result.value[q], &reference.value[q]);
    }
    for (int i = levels - 1; i >= 0; --i) {
      const long ni = 1L << i;
      arb_t difference;
      arb_init(difference);
      for (long base = 0; base < count; base += 2 * ni)
        for (long c = 0; c < ni; ++c) {
          arb_add(&scratch.value[base + c], &reference.value[base + 2 * c],
                  &reference.value[base + 2 * c + 1], 96);
          arb_sub(difference, &reference.value[base + 2 * c], &reference.value[base + 2 * c + 1],
                  96);
          arb_mul(&scratch.value[base + ni + c], difference, generator(i, c), 96);
        }
      arb_clear(difference);
      reference.value.swap(scratch.value);
    }
    sic::exact::quadratic_real_forward(fused.value, levels, 96, 2, true, generator);
    sic::exact::quadratic_real_forward(separate.value, levels, 96, 2, false, generator);
    std::vector<bool> loaded(levels, false);
    long live = 0, peak = 0;
    std::function<void(int, bool)> residency = [&](int i, bool load) {
      SIC_CHECK(loaded[i] != load);
      loaded[i] = load;
      live += (load ? 1 : -1) * (1L << i);
      peak = std::max(peak, live);
    };
    sic::exact::quadratic_real_forward(
        resident_result.value, levels, 96, 2, true,
        [&](int i, long c) {
          if (!loaded[i]) throw std::logic_error("read from a parked generator level");
          return generator(i, c);
        },
        residency);
    SIC_CHECK(live == 0);
    SIC_CHECK(peak <= (levels ? 1L << (levels - 1) : 0));
    for (long q = 0; q < count; ++q) {
      SIC_CHECK(arb_equal(&reference.value[q], &fused.value[q]));
      SIC_CHECK(arb_equal(&reference.value[q], &separate.value[q]));
      SIC_CHECK(arb_equal(&reference.value[q], &resident_result.value[q]));
    }
    for (size_t window : {1, 3, 31, 1024}) {
      Reals blocked(count);
      // The input is recreated independently of any transform layout.
      for (long q = 0; q < count; ++q) {
        arb_set_si(&blocked.value[q], q % 29 - 13);
        arb_div_ui(&blocked.value[q], &blocked.value[q], 13, 96);
        arb_add_error_2exp_si(&blocked.value[q], -82);
      }
      int active_level = -1;
      size_t first_label = 0, label_count = 0;
      sic::exact::quadratic_real_forward_windowed(
          blocked.value, levels, 96, 2, window,
          [&](int i, long c) {
            if (i != active_level || c < 0 || (size_t)c < first_label ||
                (size_t)c - first_label >= label_count)
              throw std::logic_error("read outside a resident generator window");
            return generator(i, c);
          },
          [&](int i, size_t first, size_t size, bool load) {
            SIC_CHECK(size <= window);
            if (load) {
              SIC_CHECK(active_level == -1);
              active_level = i;
              first_label = first;
              label_count = size;
            } else {
              SIC_CHECK(active_level == i);
              active_level = -1;
            }
          });
      SIC_CHECK(active_level == -1);
      for (long q = 0; q < count; ++q) SIC_CHECK(arb_equal(&reference.value[q], &blocked.value[q]));
    }
  }
  return sic_test::report();
}
