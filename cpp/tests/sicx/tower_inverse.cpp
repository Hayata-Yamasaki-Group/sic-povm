// SPDX-License-Identifier: AGPL-3.0-or-later
#include <stdexcept>
#include <string>

#include "assertions.hpp"
#include "sicx/exact/tower.hpp"

namespace {
using sic::sicx::exact::Elt;
using sic::sicx::exact::Tower;

void success_and_refusal() {
  sic::sicx::Meta meta;
  meta.D0 = 5;
  Tower tower(meta);
  Elt x = tower.one(0);
  fmpq_set_si(x[0].a, 2, 1);
  fmpq_one(x[0].b);
  const Elt inverse = tower.inverse_general(x, 0);
  Elt expected(1);
  fmpq_set_si(expected[0].a, 3, 5);
  fmpq_set_si(expected[0].b, -1, 5);
  SIC_CHECK(inverse == expected);
  SIC_CHECK(tower.mul(x, inverse, 0) == tower.one(0));

  bool refused = false;
  try {
    (void)tower.inverse_general(tower.zero(0), 0);
  } catch (const std::logic_error& e) {
    refused = std::string(e.what()) == "SICX: element is not invertible";
  }
  SIC_CHECK(refused);
}

void cubic_success() {
  sic::sicx::Meta meta;
  meta.D0 = 5;
  meta.degrees = {3};
  meta.width = {1, 3};
  Tower tower(meta);
  std::vector<Elt> coefficients(3, tower.zero(0));
  fmpq_set_si(coefficients[0][0].a, -2, 1);
  tower.set_level(0, std::move(coefficients));
  Elt x = tower.generator(0);
  fmpq_one(x[0].a);
  const Elt inverse = tower.inverse_general(x, 1);
  SIC_CHECK(tower.mul(x, inverse, 1) == tower.one(1));
}

void multiplication_exception() {
  sic::sicx::Meta meta;
  meta.D0 = 5;
  meta.degrees = {1};
  // Deliberately inconsistent low-level input: keep the inverse matrices 2x2,
  // but make the multiplication scratch exceed vector::max_size(). Both first
  // coefficients are nonzero, so block_zero returns before traversing the
  // oversized lower width. The scratch constructor throws before any reduction.
  meta.width = {static_cast<uint64_t>(Elt{}.max_size()) + 1, 1};
  Tower tower(meta);
  const Elt x = tower.one(1);
  bool refused = false;
  try {
    (void)tower.inverse_general(x, 1);
  } catch (const std::length_error&) {
    refused = true;
  }
  SIC_CHECK(refused);
}
}  // namespace

int main() {
  success_and_refusal();
  cubic_success();
  multiplication_exception();
  // LeakSanitizer observes the three exit paths without FLINT cache retention.
  flint_cleanup_master();
  return sic_test::report();
}
