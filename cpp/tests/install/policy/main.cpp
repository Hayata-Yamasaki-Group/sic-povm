// SPDX-License-Identifier: AGPL-3.0-or-later
#include <sched.h>

#include <fstream>
#include <thread>
#include <type_traits>

#include "sic/access/remote.hpp"
#include "sic/bounds/overlap_bounds.hpp"
#include "sic/data/layout.hpp"
#include "sic/decode/decoder.hpp"
#include "sic/error.hpp"
#include "sic/sicx/exact.hpp"
#include "sic/sicx/finalize.hpp"
#include "sic/sicx/format.hpp"
#include "sic/support/ball_vectors.hpp"
#include "sic/support/flint_raii.hpp"
#include "sic/transport/parts.hpp"
#ifdef SIC_GROUP_SOURCE_ID
#error "Producer source identity leaked into the consumer"
#endif
struct Memory final : sic::support::ByteSource {
  std::vector<uint8_t> bytes;
  explicit Memory(const char* path) {
    std::ifstream in(path, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(in), {});
  }
  uint64_t size() const override { return bytes.size(); }
  void read(uint64_t offset, std::span<uint8_t> out) const override {
    check({offset, out.size()});
    std::copy_n(bytes.data() + offset, out.size(), out.data());
  }
};
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    sic::sicx::fail(sic::ErrorKind::io, "consumer");
  } catch (const sic::Error& e) {
    if (e.kind() != sic::ErrorKind::io) return 11;
  }
  flint_set_num_threads(1);
  sic::support::ComplexBallVector balls(2);
  sic::support::RealBallVector real_balls(2);
  acb_one(balls.data());
  arb_one(real_balls.span().data());
  const auto& borrowed = balls;
  if (borrowed.span().size() != 2 || borrowed.data() != balls[0] || !acb_is_one(borrowed.data()) ||
      !arb_is_one(real_balls.data()))
    return 12;
  sic::support::flint::FmpqMat rational_matrix(1, 2);
  sic::support::flint::AcbMat complex_matrix(2, 1);
  if (rational_matrix.columns() != 2 || complex_matrix.rows() != 2) return 13;
  sic::sicx::Reader reader(std::make_shared<Memory>(argv[1]));
  (void)sic::sicx::verify_all(reader, 1);
  sic::decode::Decoder::Options o;
  o.pin = false;
  using Decoder = sic::decode::Decoder;
  static_assert(!std::is_copy_constructible_v<Decoder>);
  static_assert(!std::is_copy_assignable_v<Decoder>);
  static_assert(std::is_move_constructible_v<Decoder>);
  static_assert(std::is_move_assignable_v<Decoder>);
  static_assert(!std::is_constructible_v<Decoder, sic::sicx::Reader&&, Decoder::Options>);
  Decoder initial(reader, o);
  const auto* stats = &initial.stats();
  const auto* placement = &initial.placement();
  Decoder moved(std::move(initial));
  Decoder d(reader, o);
  d = std::move(moved);
  if (&d.stats() != stats || &d.placement() != placement) return 3;
  bool empty_refused = false;
  try {
    (void)initial.stats();
  } catch (const sic::Error& e) {
    empty_refused = e.kind() == sic::ErrorKind::invalid_argument;
  }
  if (!empty_refused) return 4;
  bool owner_refused = false;
  std::thread wrong_owner([&] {
    try {
      Decoder wrong(std::move(d));
    } catch (const sic::Error& e) {
      owner_refused = e.kind() == sic::ErrorKind::invalid_argument;
    }
  });
  wrong_owner.join();
  if (!owner_refused) return 5;
  // A nested pinned pool saves the outer pool's mask. Replacing that outer
  // owner must refuse before either pool restores its saved affinity.
  cpu_set_t original, outer_mask, current;
  if (sched_getaffinity(0, sizeof original, &original)) return 6;
  {
    auto pinned = o;
    pinned.pin = true;
    Decoder outer(reader, pinned);
    if (sched_getaffinity(0, sizeof outer_mask, &outer_mask)) return 7;
    {
      Decoder inner(reader, pinned);
      bool pinned_refused = false;
      try {
        outer = std::move(inner);
      } catch (const sic::Error& e) {
        pinned_refused = e.kind() == sic::ErrorKind::invalid_argument;
      }
      if (!pinned_refused || sched_getaffinity(0, sizeof current, &current) ||
          !CPU_EQUAL(&current, &outer_mask))
        return 8;
      Decoder moved_pinned(std::move(inner));
      // Assignment into an empty owner retains the existing pinned pool.
      inner = std::move(moved_pinned);
    }
    if (sched_getaffinity(0, sizeof current, &current) || !CPU_EQUAL(&current, &outer_mask))
      return 9;
  }
  if (sched_getaffinity(0, sizeof current, &current) || !CPU_EQUAL(&current, &original)) return 10;
  const auto value =
      d.decode({{reader.meta().representative.begin()->first, reader.meta().rays.front().word}});
  // Take an actual remote-library symbol as well as exporting the value types.
  bool called = false;
  try {
    (void)sic::access::remote_byte_source("/nonexistent/b2-consumer-manifest.json");
  } catch (const std::exception&) {
    called = true;
  }
  return value.size() != 1 || !called;
}
