// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/runtime/memory/ball_storage.hpp"

namespace sic::exact::phase_memory {
void BallStorage::append(const BallStorage& other) {
  values = sic::support::resources::add(values, other.values);
  header_bytes = sic::support::resources::add(header_bytes, other.header_bytes);
  heap_bytes = sic::support::resources::add(heap_bytes, other.heap_bytes);
  largest_mantissa = std::max(largest_mantissa, other.largest_mantissa);
  finite_lagom = finite_lagom && other.finite_lagom;
}

void BallStorage::observe(acb_srcptr value) {
  const slong allocated = acb_allocated_bytes(value);
  if (allocated < 0) throw std::overflow_error("negative FLINT allocation count");
  heap_bytes = sic::support::resources::add(heap_bytes,
                                            static_cast<sic::support::resources::Bytes>(allocated));
  if (!acb_is_finite(value) || !ARB_IS_LAGOM(acb_realref(value)) ||
      !ARB_IS_LAGOM(acb_imagref(value)))
    finite_lagom = false;
  if (acb_is_finite(value)) largest_mantissa = std::max(largest_mantissa, acb_bits(value));
}

BallStorage inspect_storage(std::span<const acb_struct> values) {
  BallStorage result;
  result.values = values.size();
  result.header_bytes = sic::support::resources::mul(values.size(), sizeof(acb_struct));
  for (const auto& value : values) result.observe(&value);
  return result;
}

BallStorage inspect_storage(const std::vector<acb_struct>& values) {
  auto result = inspect_storage(std::span<const acb_struct>(values));
  result.header_bytes = sic::support::resources::mul(values.capacity(), sizeof(acb_struct));
  return result;
}

BallStorage inspect_storage(const acb_mat_struct* matrix) {
  const slong rows = acb_mat_nrows(matrix), columns = acb_mat_ncols(matrix);
  if (rows < 0 || columns < 0) throw std::invalid_argument("negative matrix dimensions");
  BallStorage result;
  result.values = sic::support::resources::mul(rows, columns);
  result.header_bytes = sic::support::resources::add(
      sizeof(acb_mat_struct), sic::support::resources::add(
                                  sic::support::resources::mul(rows, sizeof(acb_ptr)),
                                  sic::support::resources::mul(result.values, sizeof(acb_struct))));
  for (slong row = 0; row < rows; ++row)
    for (slong column = 0; column < columns; ++column)
      result.observe(acb_mat_entry(matrix, row, column));
  return result;
}

}  // namespace sic::exact::phase_memory
