// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/flint_raii.hpp"

namespace sic::support::flint {
void StringFree::operator()(char* text) const noexcept { flint_free(text); }

void ArbOps::init(Value* value) { arb_init(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void ArbOps::clear(Value* value) { arb_clear(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void AcbOps::init(Value* value) { acb_init(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void AcbOps::clear(Value* value) { acb_clear(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void ArfOps::init(Value* value) { arf_init(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void ArfOps::clear(Value* value) { arf_clear(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void MagOps::init(Value* value) { mag_init(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void MagOps::clear(Value* value) { mag_clear(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void FmpzOps::init(Value* value) { fmpz_init(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void FmpzOps::clear(Value* value) { fmpz_clear(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void FmpqOps::init(Value* value) { fmpq_init(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void FmpqOps::clear(Value* value) { fmpq_clear(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void ArbPolyOps::init(Value* value) { arb_poly_init(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void ArbPolyOps::clear(Value* value) { arb_poly_clear(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void AcbPolyOps::init(Value* value) { acb_poly_init(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void AcbPolyOps::clear(Value* value) { acb_poly_clear(value); }
}  // namespace sic::support::flint

namespace sic::support::flint {
void FmpzMatOps::init(Value* value, slong rows, slong columns) {
  fmpz_mat_init(value, rows, columns);
}
void FmpzMatOps::clear(Value* value) { fmpz_mat_clear(value); }
void FmpqMatOps::init(Value* value, slong rows, slong columns) {
  fmpq_mat_init(value, rows, columns);
}
void FmpqMatOps::clear(Value* value) { fmpq_mat_clear(value); }
void AcbMatOps::init(Value* value, slong rows, slong columns) {
  acb_mat_init(value, rows, columns);
}
void AcbMatOps::clear(Value* value) { acb_mat_clear(value); }

WorkerCleanup::~WorkerCleanup() { flint_cleanup(); }
}  // namespace sic::support::flint
