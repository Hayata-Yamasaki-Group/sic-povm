// SPDX-License-Identifier: AGPL-3.0-or-later
#include "container.hpp"

#include "../runtime/diagnostics.hpp"
#include "canonical_json.hpp"
#include "integer_codec.hpp"
#include "metadata.hpp"
#include "sicx_input.hpp"
namespace sic_check::crt::sicx {
void sha_message_size(Uint64 size) {
  if (size > UINT64_MAX / 8) resource("source exceeds SHA-256 message size limit");
}

Sha256::Sha256() { reset(); }

auto Sha256::reset() -> void {
  static const uint32_t i[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(h, i, 32);
  len = 0;
  bl = 0;
}

auto Sha256::ror(uint32_t x, int n) -> uint32_t { return (x >> n) | (x << (32 - n)); }

auto Sha256::block(const Uint8* p) -> void {
  static const uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
      0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
      0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
      0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
      0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
      0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
      0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
      0xc67178f2};
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 |
           p[4 * i + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3),
             s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25), ch = (e & f) ^ (~e & g),
             t1 = hh + S1 + ch + K[i] + w[i];
    uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22), mj = (a & b) ^ (a & c) ^ (b & c),
             t2 = S0 + mj;
    hh = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
  h[5] += f;
  h[6] += g;
  h[7] += hh;
}

auto Sha256::update(const void* data, size_t n) -> void {
  const Uint8* p = (const Uint8*)data;
  len += n;
  if (bl) {
    size_t t = std::min(n, 64 - bl);
    memcpy(buf + bl, p, t);
    bl += t;
    p += t;
    n -= t;
    if (bl == 64) {
      block(buf);
      bl = 0;
    }
  }
  while (n >= 64) {
    block(p);
    p += 64;
    n -= 64;
  }
  if (n) {
    memcpy(buf, p, n);
    bl = n;
  }
}

auto Sha256::final(Uint8 out[32]) -> void {
  Uint64 bits = len * 8;
  Uint8 pad = 0x80;
  update(&pad, 1);
  Uint8 z = 0;
  while (bl != 56) update(&z, 1);
  Uint8 L[8];
  for (int i = 0; i < 8; i++) L[i] = (Uint8)(bits >> (56 - 8 * i));
  update(L, 8);
  for (int i = 0; i < 8; i++) {
    out[4 * i] = h[i] >> 24;
    out[4 * i + 1] = h[i] >> 16;
    out[4 * i + 2] = h[i] >> 8;
    out[4 * i + 3] = h[i];
  }
}

Hash sha(const void* p, size_t n) {
  Sha256 s;
  s.update(p, n);
  Hash h;
  s.final(h.data());
  return h;
}

string hex(const Uint8* p, size_t n) {
  static const char* d = "0123456789abcdef";
  string s;
  for (size_t i = 0; i < n; i++) {
    s += d[p[i] >> 4];
    s += d[p[i] & 15];
  }
  return s;
}

string hex(const Hash& h) { return hex(h.data(), 32); }

auto File::open(const string& p) -> void {
  if (f) reject("Input instances may only be opened once");
  path = p;
  f = fopen(p.c_str(), "rb");
  if (!f) reject("cannot open " + p);
  if (fseeko(f, 0, SEEK_END) != 0) reject("file seek failed");
  auto end = ftello(f);
  if (end < 0) reject("file size failed");
  size = (Uint64)end;
  if (fstat(fileno(f), &snapshot)) reject("source snapshot stat failed");
}

auto File::assert_size() const -> void {
  struct stat st {};
  if (fstat(fileno(f), &st) || st.st_size < 0 || (Uint64)st.st_size != size)
    reject("source size changed");
  if (st.st_mtim.tv_sec != snapshot.st_mtim.tv_sec ||
      st.st_mtim.tv_nsec != snapshot.st_mtim.tv_nsec ||
      st.st_ctim.tv_sec != snapshot.st_ctim.tv_sec ||
      st.st_ctim.tv_nsec != snapshot.st_ctim.tv_nsec)
    reject("source snapshot changed");
}

auto File::read(Uint64 off, void* b, size_t n) const -> void {
  if (off > size || n > size - off) reject("read past end of file");
  if (fseeko(f, (off_t)off, SEEK_SET) != 0 || fread(b, 1, n, f) != n) reject("read error");
}

auto File::read(Uint64 off, size_t n) const -> vector<Uint8> {
  if (off > size || n > size - off) reject("read past end of file");
  vector<Uint8> v(n);
  if (n) read(off, v.data(), n);
  return v;
}

File::~File() {
  if (f) fclose(f);
}

Hash tree(const vector<Hash>& leaves, size_t lo, size_t hi) {
  if (lo == hi) return sha(nullptr, 0);
  if (hi - lo == 1) return leaves[lo];
  size_t k = 1;
  while (k < (hi - lo + 1) / 2) k *= 2;
  Hash l = tree(leaves, lo, lo + k), r = tree(leaves, lo + k, hi);
  Uint8 b[65] = {1};
  memcpy(b + 1, l.data(), 32);
  memcpy(b + 33, r.data(), 32);
  return sha(b, 65);
}

Hash tree_leaf(const Uint8* p, size_t n) {
  Sha256 s;
  Uint8 z = 0;
  s.update(&z, 1);
  s.update(p, n);
  Hash h;
  s.final(h.data());
  return h;
}

Hash logical_hash(const string& kind, Uint64 element, Uint64 first, Uint64 count, const Uint8* p,
                  size_t n) {
  vector<Uint8> prefix = {'S', 'I', 'C', 'X', '1', '-', 'L', 'E', 'A', 'F', 0};
  prefix.insert(prefix.end(), kind.begin(), kind.end());
  put64(prefix, element);
  put64(prefix, first);
  put64(prefix, count);
  put64(prefix, n);
  Sha256 s;
  s.update(prefix.data(), prefix.size());
  s.update(p, n);
  Hash h;
  s.final(h.data());
  return h;
}

Hash parse_hash(const string& s) {
  if (s.size() != 64) reject("expected hash must be 64 lowercase hex digits");
  Hash h;
  auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    reject("invalid lowercase hash");
  };
  for (size_t i = 0; i < 32; ++i) h[i] = (Uint8)(16 * digit(s[2 * i]) + digit(s[2 * i + 1]));
  return h;
}

auto Input::whole_file_sha() const -> Hash {
  f.assert_size();
  sha_message_size(f.size);
  Sha256 s;
  vector<Uint8> b(1u << 20);
  for (Uint64 off = 0; off < f.size;) {
    size_t n = (size_t)std::min<Uint64>(b.size(), f.size - off);
    f.read(off, b.data(), n);
    s.update(b.data(), n);
    off = add(off, n);
  }
  Hash h;
  s.final(h.data());
  f.assert_size();
  return h;
}

auto Input::header_at(Uint64 off, const string& type, uint32_t flags, Uint64 len) const -> void {
  Uint8 h[16];
  f.read(off, h, 16);
  if (memcmp(h, type.data(), 4) || le32(h + 4) != flags || le64(h + 8) != len)
    reject("physical header disagrees with directory: " + type);
}

auto Input::payload(size_t ci) const -> vector<Uint8> {
  if (ci >= ch.size()) reject("chunk index out of range");
  const auto& c = ch[ci];
  if (c.codec) reject("unsupported codec");
  if (c.len > limits.chunk_bytes) resource("physical payload exceeds chunk byte limit");
  if (c.type == "EXMT" || c.type == "PROV" || c.type == "RAYS" || c.type == "PREC")
    if (c.len > limits.json_bytes) resource("JSON chunk exceeds limit");
  f.assert_size();
  header_at(c.off, c.type, c.flags, c.len);
  auto b = f.read(add(c.off, 16), (size_t)c.len);
  if (sha(b.data(), b.size()) != c.sha) reject("encoded payload SHA-256 mismatch: " + c.type);
  if (c.decoded != b.size()) reject("raw decoded length mismatch");
  f.assert_size();
  return b;
}
}  // namespace sic_check::crt::sicx
