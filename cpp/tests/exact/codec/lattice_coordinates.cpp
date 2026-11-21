// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/lattice_coordinates.hpp"

#include "assertions.hpp"
#include "exact/storage/leaf_store.hpp"
#include "sicx/codec/integer.hpp"

int main() {
  sic::exact::Leaf original, encoded, shift, opposite;
  for (long kDiscriminant : {5, 13, 221, 41861, 4190205}) {
    sic::support::LatticeCoordinates coordinates(kDiscriminant);
    for (long bits : {0, 1, 7, 64, 512, 17000})
      for (int sign : {-1, 1}) {
        fmpz_one(original.y);
        fmpz_mul_2exp(original.y, original.y, bits);
        fmpz_add_ui(original.y, original.y, 3);
        if (sign < 0) fmpz_neg(original.y, original.y);
        coordinates.shift(shift.x, original.y);
        fmpz_neg(opposite.y, original.y);
        coordinates.shift(opposite.x, opposite.y);
        fmpz_add(opposite.x, opposite.x, shift.x);
        SIC_CHECK(fmpz_equal_si(opposite.x, -1));
        fmpz_add_si(original.x, shift.x, sign * 17);
        fmpz_set(encoded.x, original.x);
        fmpz_set(encoded.y, original.y);
        coordinates.encode(encoded.x, encoded.y);
        SIC_CHECK(fmpz_equal_si(encoded.x, sign * 17));
        coordinates.decode(encoded.x, encoded.y);
        SIC_CHECK(fmpz_equal(encoded.x, original.x));
        arb_t value, integer;
        arb_init(value);
        arb_init(integer);
        const slong precision = 2 * fmpz_bits(original.y) + 128;
        arb_sqrt_ui(value, kDiscriminant, precision);
        arb_sub_ui(value, value, 1, precision);
        arb_mul_fmpz(value, value, original.y, precision);
        arb_mul_2exp_si(value, value, -1);
        arb_floor(integer, value, precision);
        SIC_CHECK(arb_get_unique_fmpz(encoded.x, integer));
        SIC_CHECK(fmpz_equal(encoded.x, shift.x));
        arb_clear(value);
        arb_clear(integer);
      }
    fmpz_zero(original.y);
    fmpz_set_si(original.x, -123);
    coordinates.encode(original.x, original.y);
    SIC_CHECK(fmpz_equal_si(original.x, -123));
    coordinates.decode(original.x, original.y);
    SIC_CHECK(fmpz_equal_si(original.x, -123));
    bool alias_rejected = false;
    try {
      coordinates.shift(original.y, original.y);
    } catch (const std::invalid_argument&) {
      alias_rejected = true;
    }
    SIC_CHECK(alias_rejected);
  }
  for (long invalid : {0, 1, 4, 9, 12, -3}) {
    bool rejected = false;
    try {
      sic::support::LatticeCoordinates invalid_coordinates(invalid);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    SIC_CHECK(rejected);
  }
  constexpr long kDiscriminant = 4190205;
  sic::support::LatticeCoordinates coordinates(kDiscriminant);
  std::vector<sic::exact::Leaf> values(64);
  slong original_bits = 0;
  for (size_t q = 0; q < values.size(); ++q) {
    fmpz_one(values[q].y);
    fmpz_mul_2exp(values[q].y, values[q].y, 17000);
    fmpz_add_ui(values[q].y, values[q].y, q * 17 + 1);
    if (q & 1) fmpz_neg(values[q].y, values[q].y);
    coordinates.shift(values[q].x, values[q].y);
    fmpz_add_ui(values[q].x, values[q].x, q + 1);
    original_bits =
        std::max(original_bits, (slong)std::max(fmpz_bits(values[q].x), fmpz_bits(values[q].y)));
  }
  const char* previous = std::getenv("SIC_SPOOL_LATTICE");
  const bool had_previous = previous != nullptr;
  const std::string previous_value = previous ? previous : "";
  unsetenv("SIC_SPOOL_LATTICE");
  sic::exact::LeafSpool raw_spool(true, false, kDiscriminant);
  const auto raw_ticket = raw_spool.append(values);
  setenv("SIC_SPOOL_LATTICE", "1", 1);
  sic::exact::LeafSpool packed(true, true, kDiscriminant), uncompressed(true, false, kDiscriminant);
  const auto packed_ticket = packed.append(values),
             uncompressed_ticket = uncompressed.append(values);
  std::vector<sic::exact::Leaf> growth(4);
  for (auto& leaf : growth) {
    fmpz_one(leaf.y);
    fmpz_mul_2exp(leaf.y, leaf.y, 17000);
  }
  const auto growth_ticket = packed.append(growth);
  SIC_CHECK(growth_ticket.max_bits == 17001);
  const auto restored_growth = packed.load(growth_ticket);
  for (const auto& leaf : restored_growth)
    SIC_CHECK(fmpz_is_zero(leaf.x) && fmpz_equal(leaf.y, growth[0].y));
  unsetenv("SIC_SPOOL_LATTICE");
  SIC_CHECK(packed_ticket.max_bits == original_bits &&
            packed_ticket.lattice_discriminant == kDiscriminant);
  SIC_CHECK(packed.bytes() < raw_spool.bytes() * 3 / 5);
  const auto joined = packed.concatenate({packed_ticket, packed_ticket});
  for (size_t start : {0UL, 31UL, 63UL, 64UL, 127UL}) {
    sic::exact::LeafSpool::Cursor cursor(packed, joined, start);
    const auto& leaf = cursor.next();
    SIC_CHECK(fmpz_equal(leaf.x, values[start % 64].x));
  }
  const auto decoded = uncompressed.load(uncompressed_ticket),
             raw_values = raw_spool.load(raw_ticket);
  for (size_t q = 0; q < values.size(); ++q) SIC_CHECK(fmpz_equal(decoded[q].x, raw_values[q].x));
  const auto sequence = packed.append_generated(
      70005,
      [&](size_t first, std::span<sic::exact::Leaf> block) {
        for (size_t q = 0; q < block.size(); ++q) {
          const slong y = (slong)(first + q) - 35002;
          fmpz_set_si(block[q].y, y);
          coordinates.shift(block[q].x, block[q].y);
          fmpz_add_si(block[q].x, block[q].x, (slong)((first + q) % 7) - 3);
        }
      },
      10000);
  const auto scattered = packed.concatenate({sequence, packed_ticket, sequence});
  for (size_t start : {65534UL, 65535UL, 65536UL, 70004UL, 70005UL, 70068UL, 70069UL}) {
    sic::exact::LeafSpool::Cursor cursor(packed, scattered, start);
    const auto& leaf = cursor.next();
    if (start >= 70005 && start < 70069)
      SIC_CHECK(fmpz_equal(leaf.x, values[start - 70005].x));
    else {
      const size_t ordinal = start < 70005 ? start : start - 70069;
      SIC_CHECK(fmpz_equal_si(leaf.y, (slong)ordinal - 35002));
    }
  }
  if (had_previous) setenv("SIC_SPOOL_LATTICE", previous_value.c_str(), 1);
  return sic_test::report();
}
