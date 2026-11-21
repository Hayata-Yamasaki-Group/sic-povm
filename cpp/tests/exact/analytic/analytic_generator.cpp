// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/storage/analytic_generator.hpp"

#include "assertions.hpp"

int main() {
  for (bool imaginary : {false, true}) {
    sic::support::ComplexBallVector values(16);
    for (long c = 0; c < 8; ++c) {
      arb_ptr component = imaginary ? acb_imagref(values[2 * c]) : acb_realref(values[2 * c]);
      arb_set_si(component, c - 3);
      arb_add_error_2exp_si(component, -80);
      acb_neg(values[2 * c + 1], values[2 * c]);
    }
    sic::support::ComplexBallVector reference(values), output(1);
    sic::exact::AnalyticGenerator generator;
    generator.assign(std::move(values), true, true, imaginary);
    SIC_CHECK(generator.size() == 16);
    SIC_CHECK(generator.full.size() == 0);
    SIC_CHECK(generator.scalar.size() == 8);
    for (long c = 0; c < 16; ++c) {
      generator.get(output[0], c);
      SIC_CHECK(acb_equal(output[0], reference[c]));
    }
    for (long c = 0; c < 8; ++c) {
      if (imaginary) {
        generator.imaginary_even(acb_imagref(output[0]), c);
        SIC_CHECK(arb_equal(acb_imagref(output[0]), acb_imagref(reference[2 * c])));
      } else
        SIC_CHECK(arb_equal(generator.real_even(c), acb_realref(reference[2 * c])));
    }
    if (!imaginary) {
      generator.spill(3);
      SIC_CHECK(generator.backing && generator.scalar.size() == 0);
      SIC_CHECK(generator.pieces.size() == 3);
      bool refused = false;
      try {
        generator.get(output[0], 0);
      } catch (const std::logic_error&) {
        refused = true;
      }
      SIC_CHECK(refused);
      for (size_t first = 0; first < 8; first += 3) {
        const size_t size = std::min<size_t>(3, 8 - first);
        generator.prepare_piece(first, size);
        for (size_t c = 0; c < size; ++c)
          SIC_CHECK(
              arb_equal(generator.real_even(first + c), acb_realref(reference[2 * (first + c)])));
        generator.park();
      }
      for (int retry = 0; retry < 3; ++retry) {
        generator.prepare();
        for (long c = 0; c < 16; ++c) {
          generator.get(output[0], c);
          SIC_CHECK(acb_equal(output[0], reference[c]));
        }
        generator.park();
        SIC_CHECK(generator.scalar.size() == 0);
      }
    }
  }
  sic::support::ComplexBallVector values(5), output(1);
  for (long c = 0; c < 5; ++c) acb_set_si(values[c], c + 1);
  sic::support::ComplexBallVector reference(values);
  sic::exact::AnalyticGenerator general;
  general.assign(std::move(values), false, true, false);
  SIC_CHECK(general.full.size() == 5);
  for (long c = 0; c < 5; ++c) {
    general.get(output[0], c);
    SIC_CHECK(acb_equal(output[0], reference[c]));
  }
  sic::exact::AnalyticGenerator symbolic;
  sic::support::RealBallVector paired(4);
  symbolic.assign_scalar(std::move(paired), true);
  sic::support::ComplexBallVector root(1);
  arb_sqrt_ui(acb_imagref(root[0]), 2, 128);
  const long norms[] = {1, 7, 3, 5, 5, 3, 7, 1};
  symbolic.assign_character(root[0],
                            [&](long c) { return sic::exact::eighth_character(norms[c]); });
  SIC_CHECK(symbolic.scalar.size() == 0);
  SIC_CHECK(symbolic.character);
  for (long c = 0; c < 8; ++c) {
    symbolic.get(output[0], c);
    if (sic::exact::eighth_character(norms[c]) < 0) acb_neg(output[0], output[0]);
    SIC_CHECK(acb_equal(output[0], root[0]));
  }
  bool rejected = false;
  try {
    sic::exact::eighth_character(2);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  SIC_CHECK(rejected);
  sic::support::RealBallVector periodic_values(3);
  for (int q = 0; q < 3; ++q) arb_set_si(periodic_values[q], q + 1);
  sic::exact::AnalyticGenerator periodic;
  periodic.assign_periodic(std::move(periodic_values), 32, [](long pair) { return pair % 3; });
  periodic.spill(3);
  periodic.prepare();
  periodic.park();
  SIC_CHECK(!periodic.backing && periodic.scalar.size() == 0 && periodic.periodic.size() == 3);
  for (int label = 0; label < 32; ++label) {
    periodic.get(output[0], label);
    const int expected = (label & 1 ? -1 : 1) * (label / 2 % 3 + 1);
    SIC_CHECK(acb_equal_si(output[0], expected));
    SIC_CHECK(arb_equal_si(periodic.real_even(label / 2), label / 2 % 3 + 1));
  }
  return sic_test::report();
}
