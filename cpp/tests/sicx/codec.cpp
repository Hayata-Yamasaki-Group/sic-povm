// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/block.hpp"
#include "support/sha256.hpp"
// SICX primitive and fault-control tests:
//   sic_test_sicx_sicx FIXTURE.sicx WORKDIR
// Primitives: canonical varints, integer code, canonical JSON, block runs.
// Faults on FIXTURE: a flipped byte in every chunk, truncation, a broken
// footer, and a mutated text-form line must each be refused (fail-closed).
// Streaming writer: kill/resume, corruption, parts, finalization.
#include <sys/wait.h>

#include <fstream>
#include <iostream>
#include <random>
#include <thread>

#include "fixtures/fixture.hpp"
#include "sic/sicx/finalize.hpp"
#include "sic/sicx/text.hpp"
namespace sicx_tests {
void codec_cases() {
  // Varints.
  for (uint64_t v : {0ull, 1ull, 127ull, 128ull, 300ull, ~0ull}) {
    sic::sicx::ByteBuffer b;
    sic::sicx::put_varint(b, v);
    sic::sicx::Cursor c(b.data(), b.size());
    check(c.varint() == v && !c.remaining(), "varint round trip");
  }
  {
    const sic::sicx::ByteBuffer b{0x80, 0x00};
    sic::sicx::Cursor c(b.data(), b.size());
    check(throws([&] { c.varint(); }), "non-minimal varint refused");
  }
  {
    const sic::sicx::ByteBuffer b(10, 0xff);
    sic::sicx::Cursor c(b.data(), b.size());
    check(throws([&] { c.varint(); }), "overlong varint refused");
  }
  // Integers.
  sic::sicx::IntCodec codec;
  fmpz_t a, b;
  fmpz_init(a);
  fmpz_init(b);
  flint_rand_t state;
  flint_rand_init(state);
  for (int i = 0; i < 2000; ++i) {
    fmpz_randtest(a, state, 1 + i % 3000);
    sic::sicx::ByteBuffer out;
    codec.put(out, a);
    sic::sicx::Cursor c(out.data(), out.size());
    codec.get(c, b);
    check(fmpz_equal(a, b) && !c.remaining(), "integer round trip");
  }
  flint_rand_clear(state);
  {
    const sic::sicx::ByteBuffer neg{0x01};
    sic::sicx::Cursor c(neg.data(), neg.size());
    check(throws([&] { codec.get(c, b); }), "negative zero refused");
  }
  {
    const sic::sicx::ByteBuffer lead{0x04, 0x05, 0x00};
    sic::sicx::Cursor c(lead.data(), lead.size());
    check(throws([&] { codec.get(c, b); }), "leading zero byte refused");
  }
  fmpz_clear(a);
  fmpz_clear(b);
}
}  // namespace sicx_tests
