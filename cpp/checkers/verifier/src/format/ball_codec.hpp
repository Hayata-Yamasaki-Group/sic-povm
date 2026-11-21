// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::verifier {

// Arb dump form (spec 7a): "M E m e" in lowercase hex, canonical; returns fmpz fields, rejects
// non-canonical.
bool hex_nat(const string& t, fmpz_t out, bool allow_neg);

void parse_arbdump(const string& s, fmpz_t M, fmpz_t E, fmpz_t m, fmpz_t e);

// ----- BALL record parsing
struct BallPart {
  bool neg = false;
  fmpz_t M;
  Int64 E = 0;
  Uint64 m = 0;
  Int64 e = 0;

  BallPart();

  ~BallPart();
};

void parse_ball_part(const Uint8* p, Uint64 limbs, BallPart& o);

void ball_to_arb(arb_t x, const BallPart& b);

// Conservative retained-storage admission: object/vector storage and two midpoint
// blocks per coordinate, with capacity/allocation headroom. Other reader buffers
// are separate; this is not a bound on total process RSS.
void admit_ball_memory(Uint64 rays, Uint64 coordinates, Uint64 limbs);
}  // namespace sic_check::verifier
