// SPDX-License-Identifier: AGPL-3.0-or-later
#include <new>
#include <string>
#include <type_traits>

#include "assertions.hpp"
#include "sic/support/ball_vectors.hpp"
#include "sic/support/flint_raii.hpp"

namespace {
using namespace sic::support;

template <class T>
concept RvalueGet = requires(T&& value) {
  std::move(value).get();
};
template <class T>
concept RvalueArrow = requires(T&& value) {
  std::move(value).operator->();
};
template <class T>
concept RvalueData = requires(T&& value) {
  std::move(value).data();
};
template <class T>
concept RvalueSpan = requires(T&& value) {
  std::move(value).span();
};
static_assert(!RvalueGet<flint::AcbMat> && !RvalueGet<const flint::FmpqMat>);
static_assert(!RvalueArrow<flint::AcbMat> && !RvalueArrow<const flint::FmpqMat>);
static_assert(!std::is_convertible_v<flint::AcbMat&&, acb_mat_struct*>);
static_assert(!std::is_convertible_v<const flint::FmpqMat&&, const fmpq_mat_struct*>);
static_assert(!RvalueData<ComplexBallVector> && !RvalueData<const RealBallVector>);
static_assert(!RvalueSpan<RealBallVector> && !RvalueSpan<const ComplexBallVector>);

void matrix_owners() {
  static_assert(!std::is_copy_constructible_v<flint::FmpqMat>);
  static_assert(!std::is_copy_assignable_v<flint::AcbMat>);
  static_assert(std::is_nothrow_move_constructible_v<flint::FmpqMat>);
  static_assert(std::is_nothrow_move_assignable_v<flint::AcbMat>);
  flint::FmpzMat integers(2, 3);
  fmpz_set_si(fmpz_mat_entry(integers, 1, 2), 42);
  flint::FmpzMat moved_integers(std::move(integers));
  SIC_CHECK(fmpz_equal_si(fmpz_mat_entry(moved_integers, 1, 2), 42));
  flint::FmpqMat rational(2, 3);
  fmpq_set_si(fmpq_mat_entry(rational, 1, 2), 7, 3);
  const auto* storage = rational.get()->entries;
  flint::FmpqMat moved(std::move(rational));
  SIC_CHECK(rational.rows() == 0 && rational.columns() == 0);
  SIC_CHECK(moved.rows() == 2 && moved.columns() == 3 && moved.get()->entries == storage);
  flint::FmpqMat destination(1, 1);
  destination = std::move(moved);
  SIC_CHECK(destination.rows() == 2 && destination.columns() == 3);
  SIC_CHECK(moved.rows() == 1 && moved.columns() == 1);
  SIC_CHECK(fmpz_equal_si(fmpq_numref(fmpq_mat_entry(destination, 1, 2)), 7));
  SIC_CHECK(fmpz_equal_si(fmpq_denref(fmpq_mat_entry(destination, 1, 2)), 3));

  flint::AcbMat complex(3, 2);
  acb_set_si(acb_mat_entry(complex, 2, 1), 9);
  const auto* complex_storage = complex.get()->entries;
  flint::AcbMat transferred(std::move(complex));
  SIC_CHECK(complex.rows() == 0 && complex.columns() == 0);
  flint::AcbMat assigned(1, 1);
  assigned = std::move(transferred);
  SIC_CHECK(assigned.rows() == 3 && assigned.columns() == 2);
  SIC_CHECK(assigned.get()->entries == complex_storage);
  SIC_CHECK(acb_equal_si(acb_mat_entry(assigned, 2, 1), 9));
  SIC_CHECK(transferred.rows() == 1 && transferred.columns() == 1);
  const auto& const_matrix = assigned;
  static_assert(std::is_same_v<decltype(const_matrix.get()), const acb_mat_struct*>);
  SIC_CHECK(acb_mat_nrows(const_matrix) == 3);
  for (bool complex_kind : {false, true}) {
    bool refused = false;
    try {
      if (complex_kind) {
        flint::AcbMat invalid(1, -1);
      } else {
        flint::FmpqMat invalid(-1, 1);
      }
    } catch (const sic::Error& e) {
      refused = e.kind() == sic::ErrorKind::invalid_argument;
    }
    SIC_CHECK(refused);
  }
}

void rational_unwind() {
  bool unwound = false;
  try {
    flint::FmpzMat integers(8, 8);
    fmpz_one(fmpz_mat_entry(integers, 0, 0));
    fmpz_mul_2exp(fmpz_mat_entry(integers, 0, 0), fmpz_mat_entry(integers, 0, 0), 4096);
    flint::FmpqMat input(8, 8), product(8, 8);
    for (slong row = 0; row < input.rows(); ++row)
      for (slong column = 0; column < input.columns(); ++column) {
        auto* entry = fmpq_mat_entry(input, row, column);
        fmpq_one(entry);
        fmpq_mul_2exp(entry, entry, 1024 + row + column);
      }
    fmpq_mat_mul(product, input, input);
    SIC_CHECK(fmpz_bits(fmpq_numref(fmpq_mat_entry(product, 0, 0))) > 2000);
    throw std::bad_alloc();  // Heap-backed entries must unwind after computation.
  } catch (const std::bad_alloc&) {
    unwound = true;
  }
  SIC_CHECK(unwound);
}

void complex_unwind() {
  bool unwound = false;
  try {
    flint::AcbMat input(8, 8), product(8, 8);
    for (slong row = 0; row < input.rows(); ++row)
      for (slong column = 0; column < input.columns(); ++column)
        arb_const_pi(acb_realref(acb_mat_entry(input, row, column)), 1024);
    acb_mat_mul(product, input, input, 1024);
    SIC_CHECK(acb_is_finite(acb_mat_entry(product, 0, 0)));
    throw std::bad_alloc();
  } catch (const std::bad_alloc&) {
    unwound = true;
  }
  SIC_CHECK(unwound);
}

void ball_borrows() {
  ComplexBallVector complex(3);
  RealBallVector real(3);
  static_assert(std::is_same_v<decltype(complex.data()), acb_ptr>);
  static_assert(std::is_same_v<decltype(std::as_const(complex).data()), acb_srcptr>);
  static_assert(std::is_same_v<decltype(std::as_const(real).span()), std::span<const arb_struct>>);
  static_assert(!std::is_copy_constructible_v<RealBallVector>);
  SIC_CHECK(complex.span().size() == 3 && real.span().size() == 3);
  SIC_CHECK(complex.span().data() == complex[0] && real.span().data() == real[0]);
  arb_const_pi(real.data(), 1024);
  acb_set_arb(complex.span().data(), real.data());
  ComplexBallVector copy = complex;
  SIC_CHECK(copy.data() != complex.data() && acb_equal(copy[0], complex[0]));
  acb_zero(complex[0]);
  SIC_CHECK(!acb_is_zero(copy[0]));
  complex = copy;
  SIC_CHECK(complex.data() != copy.data() && acb_equal(complex[0], copy[0]));
  auto* storage = complex.data();
  ComplexBallVector moved(std::move(complex));
  SIC_CHECK(moved.data() == storage && complex.size() == 0);
  complex = std::move(moved);
  SIC_CHECK(complex.data() == storage && moved.size() == 0);
  auto* real_storage = real.data();
  RealBallVector moved_real(std::move(real));
  SIC_CHECK(moved_real.data() == real_storage && real.size() == 0);
  real = std::move(moved_real);
  SIC_CHECK(real.data() == real_storage && moved_real.size() == 0);
  SIC_CHECK(ComplexBallVector().size() == 0);
  SIC_CHECK(RealBallVector().size() == 0);
  try {
    ComplexBallVector temporary = copy;
    RealBallVector temporary_real(3);
    arb_set(temporary_real[0], real[0]);
    throw std::bad_alloc();
  } catch (const std::bad_alloc&) {
  }
}
}  // namespace

int main(int argc, char** argv) {
  flint::WorkerCleanup cleanup;
  SIC_CHECK(argc == 2);
  if (argc == 2) {
    const std::string mode(argv[1]);
    if (mode == "matrix")
      matrix_owners();
    else if (mode == "rational")
      rational_unwind();
    else if (mode == "complex")
      complex_unwind();
    else if (mode == "balls")
      ball_borrows();
    else
      SIC_CHECK(false);
  }
  return sic_test::report();
}
