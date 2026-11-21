// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::crt::sicx {

Uint64 add(Uint64 a, Uint64 b);

Uint64 mul(Uint64 a, Uint64 b);

Uint64 le64(const Uint8* p);

uint32_t le32(const Uint8* p);

uint32_t le16(const Uint8* p);

// ------------------------------------------------------------------------------------------------
// varints, ints
struct Cur {
  const Uint8 *p, *e;
};

Uint64 rd_varint(Cur& c);

void rd_int(Cur& c, fmpz_t out);

// pair transform (spec 4.4) for D0 > 1, D0 = 1 mod 4: x = r + k(y), k(y) = floor(y (sqrt(D0) -
// 1)/2)
void kfun(fmpz_t k, const fmpz_t y, const fmpz_t D0);

void put64(vector<Uint8>& b, Uint64 v);

void wr_varint(vector<Uint8>& b, Uint64 v);

void wr_int(vector<Uint8>& b, const fmpz_t z);
}  // namespace sic_check::crt::sicx
