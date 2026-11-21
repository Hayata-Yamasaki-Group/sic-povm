// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/storage/leaf_store.hpp"

#include <stdexcept>
#include <string>
#include <vector>

#include "assertions.hpp"
#include "sicx/codec/integer.hpp"

using sic::exact::Leaf;
using sic::exact::LeafSpool;

static bool set_decimal(fmpz_t value, const char* text) {
  return fmpz_set_str(value, text, 10) == 0;
}

static bool same_leaves(const std::vector<Leaf>& left, const std::vector<Leaf>& right) {
  if (left.size() != right.size()) return false;
  for (size_t i = 0; i < left.size(); ++i)
    if (!fmpz_equal(left[i].x, right[i].x) || !fmpz_equal(left[i].y, right[i].y)) return false;
  return true;
}

int main() {
  std::vector<Leaf> first(3), second(2);
  SIC_CHECK(set_decimal(first[0].x, "0"));
  SIC_CHECK(set_decimal(first[0].y, "0"));
  SIC_CHECK(set_decimal(first[1].x, "1234567890123456789012345678901234567890"));
  SIC_CHECK(set_decimal(first[1].y, "-987654321098765432109876543210987654321"));
  SIC_CHECK(set_decimal(first[2].x, "-17"));
  SIC_CHECK(set_decimal(first[2].y, "29"));
  SIC_CHECK(set_decimal(second[0].x, "-1"));
  SIC_CHECK(set_decimal(second[0].y, "-314159265358979323846264338327950288419716939937510"));
  SIC_CHECK(set_decimal(second[1].x, "271828182845904523536028747135266249775724709369995"));
  SIC_CHECK(set_decimal(second[1].y, "0"));

  LeafSpool spool(true);
  SIC_CHECK(spool.enabled());
  const auto first_ticket = spool.append(first);
  const auto second_ticket = spool.append(second);
  SIC_CHECK(first_ticket.offset >= 0 && first_ticket.count == first.size());
  SIC_CHECK(second_ticket.offset > first_ticket.offset && second_ticket.count == second.size());

  const auto second_roundtrip = spool.load(second_ticket);
  const auto first_roundtrip = spool.load(first_ticket);
  SIC_CHECK(same_leaves(second_roundtrip, second));
  SIC_CHECK(same_leaves(first_roundtrip, first));
  SIC_CHECK(first_ticket.max_bits > 64);
  size_t visited = 0, blocks = 0;
  spool.read_blocks(
      first_ticket,
      [&](size_t offset, std::span<Leaf> block) {
        SIC_CHECK(offset == visited);
        SIC_CHECK(block.size() <= 2);
        ++blocks;
        for (const auto& leaf : block) {
          SIC_CHECK(fmpz_equal(leaf.x, first[visited].x));
          SIC_CHECK(fmpz_equal(leaf.y, first[visited].y));
          ++visited;
        }
      },
      2);
  SIC_CHECK(visited == first.size());
  SIC_CHECK(blocks == 2);
  const auto reordered = spool.concatenate({second_ticket, first_ticket, second_ticket});
  SIC_CHECK(reordered.count == 7 && reordered.max_bits >= first_ticket.max_bits);
  const auto reordered_values = spool.load(reordered);
  for (size_t q = 0; q < reordered.count; ++q) {
    const auto& expected = q < 2 ? second[q] : (q < 5 ? first[q - 2] : second[q - 5]);
    SIC_CHECK(fmpz_equal(reordered_values[q].x, expected.x));
    SIC_CHECK(fmpz_equal(reordered_values[q].y, expected.y));
    LeafSpool::Cursor logical(spool, reordered, q);
    const auto& actual = logical.next();
    SIC_CHECK(fmpz_equal(actual.x, expected.x) && fmpz_equal(actual.y, expected.y));
  }
  LeafSpool::Cursor boundary(spool, reordered, 1);
  for (size_t q = 1; q < reordered.count; ++q) {
    const auto& actual = boundary.next();
    SIC_CHECK(fmpz_equal(actual.x, reordered_values[q].x));
    SIC_CHECK(fmpz_equal(actual.y, reordered_values[q].y));
  }
  const auto generated = spool.append_generated(
      70005,
      [&](size_t first_index, std::span<Leaf> block) {
        for (size_t q = 0; q < block.size(); ++q) {
          fmpz_set_si(block[q].x, -(slong)(first_index + q + 1));
          fmpz_set_ui(block[q].y, 2 * (first_index + q));
        }
      },
      10000);
  LeafSpool::Cursor cursor(spool, generated, 65530), interleaved(spool, generated, 70000);
  for (size_t q = 65530; q < 65545; ++q) {
    const auto& leaf = cursor.next();
    SIC_CHECK(fmpz_equal_si(leaf.x, -(slong)(q + 1)));
    SIC_CHECK(fmpz_equal_ui(leaf.y, 2 * q));
    if (q < 65535) {
      const auto& other = interleaved.next();
      SIC_CHECK(fmpz_equal_ui(other.y, 2 * (70000 + q - 65530)));
    }
  }
  const auto rollback_start = spool.append(std::vector<Leaf>());
  bool producer_failed = false;
  try {
    spool.append_generated(
        4,
        [&](size_t offset, std::span<Leaf>) {
          if (offset) throw std::runtime_error("test producer failure");
        },
        2);
  } catch (const std::runtime_error&) {
    producer_failed = true;
  }
  SIC_CHECK(producer_failed);
  SIC_CHECK(spool.append(first).offset == rollback_start.offset);
  LeafSpool packed_zeros(true), raw_zeros(true, false);
  auto zeros = [](size_t first_index, std::span<Leaf> block) {
    for (size_t q = 0; q < block.size(); ++q) {
      fmpz_zero(block[q].x);
      fmpz_zero(block[q].y);
      if (first_index + q == 65535 || first_index + q == 65536)
        fmpz_set_si(block[q].y, -(slong)(first_index + q));
    }
  };
  const auto packed_ticket = packed_zeros.append_generated(70000, zeros);
  const auto raw_ticket = raw_zeros.append_generated(70000, zeros);
  SIC_CHECK(packed_ticket.compressed && !raw_ticket.compressed);
  SIC_CHECK(packed_zeros.bytes() < raw_zeros.bytes() / 32);
  const auto packed_roundtrip = packed_zeros.load(packed_ticket),
             raw_roundtrip = raw_zeros.load(raw_ticket);
  SIC_CHECK(same_leaves(packed_roundtrip, raw_roundtrip));
  for (size_t start : {0UL, 65534UL, 65535UL, 65536UL, 69999UL}) {
    LeafSpool::Cursor reader(packed_zeros, packed_ticket, start);
    const auto& leaf = reader.next();
    SIC_CHECK(fmpz_equal(leaf.y, raw_roundtrip[start].y));
  }

  FILE* text = tmpfile();
  SIC_CHECK(text != nullptr);
  if (text != nullptr) {
    spool.write_text(text, first_ticket);
    rewind(text);
    std::string actual;
    char buffer[256];
    size_t read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), text)) != 0) actual.append(buffer, read);
    const std::string expected =
        "0 0\n"
        "1234567890123456789012345678901234567890 -987654321098765432109876543210987654321\n"
        "-17 29\n";
    SIC_CHECK(actual == expected);
    SIC_CHECK(fclose(text) == 0);
  }

  LeafSpool disabled(false);
  SIC_CHECK(!disabled.enabled());
  bool append_rejected = false, load_rejected = false, text_rejected = false;
  try {
    disabled.append(first);
  } catch (const std::logic_error&) {
    append_rejected = true;
  }
  try {
    disabled.load(first_ticket);
  } catch (const std::logic_error&) {
    load_rejected = true;
  }
  FILE* ignored = tmpfile();
  if (ignored != nullptr) {
    try {
      disabled.write_text(ignored, first_ticket);
    } catch (const std::logic_error&) {
      text_rejected = true;
    }
    fclose(ignored);
  } else {
    SIC_CHECK(false);
  }
  SIC_CHECK(append_rejected && load_rejected && text_rejected);
  return sic_test::report();
}
