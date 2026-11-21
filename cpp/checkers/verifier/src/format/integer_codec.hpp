// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::verifier {

Uint64 le64(const Uint8* p);

uint32_t le32(const Uint8* p);

uint32_t le16(const Uint8* p);

// ------------------------------------------------------------------------------------------------
// varints, ints, blocks
struct Cur {
  const Uint8 *p, *e;
};

Uint64 rd_varint(Cur& c);

inline const Uint64 kIntegerLimit = 1u << 20;

void rd_int(Cur& c, fmpz_t out);

void enc_varint(vector<Uint8>& o, Uint64 v);

void enc_int(vector<Uint8>& o, const fmpz_t x);

// k(y) = floor(y (sqrt(D0)-1)/2) in integers (spec 4.4)
// pair transform (spec 4.4): mode 0 identity, 1 real (D0 > 1, = 1 mod 4), 2 balanced (D0 < 0, = 1
// mod 4)
int xmode(const fmpz_t D0);

void kfun(fmpz_t k, const fmpz_t y, const fmpz_t D0);

void x_from_r(fmpz_t x, const fmpz_t r, const fmpz_t y, const fmpz_t D0);

void r_from_x(fmpz_t r, const fmpz_t x, const fmpz_t y, const fmpz_t D0);

void kfun(fmpz_t k, const fmpz_t y, const fmpz_t D0);

// ------------------------------------------------------------------------------------------------
// block parsing Sink: (s, x, y) for every nonzero coordinate; x is the ORIGINAL-basis x if xform.
void parse_block(const Uint8* b, size_t n, Uint64 exp_elem, Uint64 exp_first, Uint64 exp_count,
                 const fmpz_t D0, bool xform,
                 const std::function<void(Uint64, const fmpz_t, const fmpz_t)>& sink,
                 const std::function<void(Uint64 /*s0*/, Uint64 /*len*/)>* zsink = nullptr,
                 size_t* consumed = nullptr);

// ----- element accumulators and encoder
struct Encoder {
  vector<Uint8> out, data;
  Uint64 pos = 0, ndata = 0;

  void flush_data();

  void start(Uint64 el, Uint64 first, Uint64 count);

  void nonzero(Uint64 srel, const fmpz_t x, const fmpz_t y, const fmpz_t D0);

  void finish(Uint64 count);
};

string hexz(const fmpz_t x);

Uint64 checked_add(Uint64 a, Uint64 b);

Uint64 checked_mul(Uint64 a, Uint64 b);
}  // namespace sic_check::verifier
