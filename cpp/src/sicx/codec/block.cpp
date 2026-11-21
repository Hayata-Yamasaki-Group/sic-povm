// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/block.hpp"

namespace sic::sicx {
PairTransform::PairTransform(int64_t D0) {
  if (D0 > 1 && D0 % 4 == 1)
    lattice_.emplace(static_cast<long>(D0));
  else if (D0 < 0 && ((D0 % 4) + 4) % 4 == 1)
    balanced_ = true;
}
}  // namespace sic::sicx

namespace sic::sicx {
void PairTransform::encode(fmpz_t x, const fmpz_t y) {
  if (lattice_)
    lattice_->encode(x, y);
  else if (balanced_) {
    fmpz_t h;
    fmpz_init(h);
    fmpz_fdiv_q_2exp(h, y, 1);
    fmpz_add(x, x, h);
    fmpz_clear(h);
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
void PairTransform::decode(fmpz_t r, const fmpz_t y) {
  if (lattice_)
    lattice_->decode(r, y);
  else if (balanced_) {
    fmpz_t h;
    fmpz_init(h);
    fmpz_fdiv_q_2exp(h, y, 1);
    fmpz_sub(r, r, h);
    fmpz_clear(h);
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
FmpzPair::FmpzPair() {
  fmpz_init(x);
  fmpz_init(y);
}
}  // namespace sic::sicx

namespace sic::sicx {
FmpzPair::~FmpzPair() {
  fmpz_clear(x);
  fmpz_clear(y);
}
}  // namespace sic::sicx

namespace sic::sicx {
BlockEncoder::BlockEncoder(int64_t D0) : transform(D0) { fmpz_init(r); }
}  // namespace sic::sicx

namespace sic::sicx {
BlockEncoder::~BlockEncoder() { fmpz_clear(r); }
}  // namespace sic::sicx
