// SPDX-License-Identifier: AGPL-3.0-or-later
#include "self_test.hpp"

#include "../decode/decoder.hpp"
#include "../format/ball_codec.hpp"
#include "../format/canonical_json.hpp"
#include "../format/container.hpp"
#include "../format/integer_codec.hpp"
#include "../format/metadata.hpp"
#include "../format/sha256.hpp"
#include "../runtime/diagnostics.hpp"
#include "../text/text_check.hpp"
namespace sic_check::verifier {
int selftest() {
  int bad = 0;
  auto expect_reject = [&](const char* what, std::function<void()> fn) {
    try {
      fn();
      say("  SELFTEST FAIL (accepted): %s\n", what);
      bad++;
    } catch (Reject& e) {
      say("  selftest ok: %-48s -> %s\n", what, e.what());
    }
  };
  auto vi = [](vector<Uint8> b) {
    return [b]() {
      Cur c{b.data(), b.data() + b.size()};
      rd_varint(c);
    };
  };
  expect_reject("varint 0x80 0x00 (redundant group)", vi({0x80, 0x00}));
  expect_reject("varint 0xff 0x00", vi({0xff, 0x00}));
  expect_reject("varint 11 bytes",
                vi({0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01}));
  expect_reject("varint 10th byte > 1 (>= 2^64)",
                vi({0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02}));
  expect_reject("varint truncated", vi({0x80}));
  {
    Cur c;
    vector<Uint8> g = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01};
    c = {g.data(), g.data() + 10};
    if (rd_varint(c) != ~0ull) {
      say("  SELFTEST FAIL: 2^64-1\n");
      bad++;
    } else
      say("  selftest ok: varint 2^64-1 accepted\n");
  }
  auto ii = [](vector<Uint8> b) {
    return [b]() {
      Cur c{b.data(), b.data() + b.size()};
      fmpz_t x;
      fmpz_init(x);
      rd_int(c, x);
      fmpz_clear(x);
    };
  };
  expect_reject("int negative zero (code 1)", ii({0x01}));
  expect_reject("int zero-length magnitude with n>0, msb byte 0", ii({0x04, 0x01, 0x00}));
  expect_reject("int truncated magnitude", ii({0x06, 0x01}));
  expect_reject("int non-canonical length varint", ii({0x82, 0x00, 0x01}));
  expect_reject("JSON bare number", [] { parse_canonical((const Uint8*)"{\"a\":1}", 7, "t"); });
  expect_reject("JSON whitespace",
                [] { parse_canonical((const Uint8*)"{\"a\": \"1\"}", 10, "t"); });
  expect_reject("JSON unsorted keys",
                [] { parse_canonical((const Uint8*)"{\"b\":\"1\",\"a\":\"1\"}", 17, "t"); });
  expect_reject("JSON duplicate keys",
                [] { parse_canonical((const Uint8*)"{\"a\":\"1\",\"a\":\"1\"}", 17, "t"); });
  expect_reject("JSON uppercase \\u00XX",
                [] { parse_canonical((const Uint8*)"{\"a\":\"\\u001F\"}", 14, "t"); });
  expect_reject("JSON \\/ escape",
                [] { parse_canonical((const Uint8*)"{\"a\":\"\\/\"}", 10, "t"); });
  expect_reject("JSON true", [] { parse_canonical((const Uint8*)"[true]", 6, "t"); });
  {
    vector<Uint8> a = {'{', '"', 'a', '"', ':', '"', '\\', 'u', '0', '0', '1', 'f', '"', '}'};
    try {
      parse_canonical(a.data(), a.size(), "t");
      say("  selftest ok: JSON \\u001f accepted\n");
    } catch (Reject& e) {
      say("  SELFTEST FAIL: %s\n", e.what());
      bad++;
    }
  }
  {
    Uint8 d[3] = {0};
    Hash h = sha("abc", 3);
    (void)d;
    string want = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    if (hex(h) != want) {
      say("  SELFTEST FAIL: sha256(abc)\n");
      bad++;
    } else
      say("  selftest ok: SHA-256(abc)\n");
  }
  {
    Hash h = sha("", 0);
    if (hex(h) != "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") {
      say("  SELFTEST FAIL: sha256('')\n");
      bad++;
    }
  }
  {  // pair transform of the spec's worked example: D0=5, y=-1 -> k=-1; r = x-k = 1
    fmpz_t D0, y, k;
    fmpz_init_set_ui(D0, 5);
    fmpz_init_set_si(y, -1);
    fmpz_init(k);
    kfun(k, y, D0);
    if (fmpz_cmp_si(k, -1) != 0) {
      say("  SELFTEST FAIL: k(-1)\n");
      bad++;
    } else
      say("  selftest ok: k(-1)=-1 for D0=5 (spec 12)\n");
  }
  {  // spec 4.4 test vectors (D0 = 5): k(4)=2 k(2)=1 k(-1)=-1 k(-2)=-2 k(-8)=-5; pairs -> (r, y)
    fmpz_t D0, y, k, x, r;
    fmpz_init_set_ui(D0, 5);
    fmpz_init(y);
    fmpz_init(k);
    fmpz_init(x);
    fmpz_init(r);
    struct V {
      long y, k;
    } kv[] = {{4, 2}, {2, 1}, {-1, -1}, {-2, -2}, {-8, -5}};
    int ok = 1;
    for (auto& v : kv) {
      fmpz_set_si(y, v.y);
      kfun(k, y, D0);
      if (fmpz_cmp_si(k, v.k) != 0) ok = 0;
    }
    struct P {
      long x, y, r;
    } pv[] = {{0, 4, -2}, {2, -2, 4}, {8, -8, 13}, {0, -1, 1}, {0, -8, 5}, {-2, 2, -3}};
    for (auto& v : pv) {
      fmpz_set_si(x, v.x);
      fmpz_set_si(y, v.y);
      r_from_x(r, x, y, D0);
      if (fmpz_cmp_si(r, v.r) != 0) ok = 0;
      x_from_r(k, r, y, D0);
      if (fmpz_cmp(k, x) != 0) ok = 0;
    }
    if (!ok) {
      say("  SELFTEST FAIL: spec 4.4 pair-transform vectors\n");
      bad++;
    } else
      say("  selftest ok: spec 4.4 pair-transform test vectors (D0=5)\n");
    // balanced rule D0 = -3: r = x + floor(y/2)
    fmpz_set_si(D0, -3);
    ok = 1;
    long xs[] = {0, 3, -5, 7}, ys[] = {1, -1, -3, 4};
    for (int i = 0; i < 4; i++) {
      fmpz_set_si(x, xs[i]);
      fmpz_set_si(y, ys[i]);
      r_from_x(r, x, y, D0);
      long fl = ys[i] >= 0 ? ys[i] / 2 : -((-ys[i] + 1) / 2);
      if (fmpz_cmp_si(r, xs[i] + fl) != 0) ok = 0;
      x_from_r(k, r, y, D0);
      if (fmpz_cmp(k, x) != 0) ok = 0;
    }
    if (!ok) {
      say("  SELFTEST FAIL: balanced rule\n");
      bad++;
    } else
      say("  selftest ok: balanced pair rule for D0 = -3\n");
  }
  {
    fmpz_t M, E, m, e;
    fmpz_init(M);
    fmpz_init(E);
    fmpz_init(m);
    fmpz_init(e);
    parse_arbdump("16a09e667f3bcc908b 0 1 -1fd", M, E, m, e);
    say("  selftest ok: Arb dump grammar accepts a canonical string\n");
    const char* badd[] = {"16a09e 0 1", "16a09e 0 1 -1fd ", " 1 0 1 0", "16A 0 1 0",
                          "-0 0 0 0",   "02 0 0 0",         "1 0 2 0",  "1 0 40000001 0",
                          "0 1 0 0",    "1 0 0 5",          "1 00 1 0", "1 +0 1 0",
                          "1 0x1 1 0",  "2 0 1 0"};
    for (const char* b2 : badd) {
      string t = b2;
      expect_reject(("Arb dump: '" + t + "'").c_str(),
                    [t, &M, &E, &m, &e] { parse_arbdump(t, M, E, m, e); });
    }
  }
  {  // spec 12 block of eta_2: 0d 00 02 | 02 | 03 | 02 01 | 03 01
    Uint8 b[9] = {0x0d, 0x00, 0x02, 0x02, 0x03, 0x02, 0x01, 0x03, 0x01};
    fmpz_t D0;
    fmpz_init_set_ui(D0, 5);
    int n = 0;
    string out;
    parse_block(b, 9, 13, 0, 2, D0, true, [&](Uint64 s, const fmpz_t x, const fmpz_t y) {
      n++;
      out = std::to_string(s) + ":" + dec(x) + "," + dec(y);
    });
    if (n != 1 || out != "1:0,-1") {
      say("  SELFTEST FAIL: spec 12 block decodes to '%s'\n", out.c_str());
      bad++;
    } else
      say("  selftest ok: spec 12 block -> coordinate %s\n", out.c_str());
    Encoder e;
    e.start(13, 0, 2);
    fmpz_t x, y;
    fmpz_init_set_ui(x, 0);
    fmpz_init_set_si(y, -1);
    e.nonzero(1, x, y, D0);
    e.finish(2);
    if (e.out.size() != 9 || memcmp(e.out.data(), b, 9) != 0) {
      say("  SELFTEST FAIL: re-encode\n");
      bad++;
    } else
      say("  selftest ok: spec 12 block re-encodes byte-exactly\n");
  }
  return bad;
}
}  // namespace sic_check::verifier
