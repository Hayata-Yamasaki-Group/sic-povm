// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/sha256.hpp"

#include <limits>

#include "sic/error.hpp"
#include "support/hex.hpp"

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#include <immintrin.h>
#define SIC_SHA256_HAVE_X86 1
#else
#define SIC_SHA256_HAVE_X86 0
#endif

namespace sic::support::sha256 {
void compress_portable(uint32_t* state, const unsigned char* data, size_t blocks) {
  const auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
  for (; blocks; --blocks, data += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = (uint32_t)data[4 * i] << 24 | (uint32_t)data[4 * i + 1] << 16 |
             (uint32_t)data[4 * i + 2] << 8 | (uint32_t)data[4 * i + 3];
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t t1 =
          h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
      const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
  }
}
}  // namespace sic::support::sha256

namespace sic::support::sha256 {
#if SIC_SHA256_HAVE_X86
// Intel SHA extensions. State is kept as ABEF/CDGH; each 4-round group j uses
// message words W[4j..4j+3]; for j >= 4 they are derived with sha256msg1/msg2:
// W_j = msg2(msg1(W_{j-4}, W_{j-3}) + alignr(W_{j-1}, W_{j-2}, 4), W_{j-1}).
__attribute__((target("sha,sse4.1,ssse3"))) void compress_shani(uint32_t* state,
                                                                const unsigned char* data,
                                                                size_t blocks) {
  const __m128i mask = _mm_set_epi64x(0x0c0d0e0f08090a0bULL, 0x0405060700010203ULL);
  __m128i tmp = _mm_loadu_si128((const __m128i*)&state[0]);
  __m128i state1 = _mm_loadu_si128((const __m128i*)&state[4]);
  tmp = _mm_shuffle_epi32(tmp, 0xB1);                // CDAB
  state1 = _mm_shuffle_epi32(state1, 0x1B);          // EFGH
  __m128i state0 = _mm_alignr_epi8(tmp, state1, 8);  // ABEF
  state1 = _mm_blend_epi16(state1, tmp, 0xF0);       // CDGH
  for (; blocks; --blocks, data += 64) {
    const __m128i abef = state0, cdgh = state1;
    __m128i w[4];
#pragma GCC unroll 16
    for (int j = 0; j < 16; ++j) {
      __m128i word;
      if (j < 4) {
        word = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 16 * j)), mask);
      } else {
        __m128i x = _mm_sha256msg1_epu32(w[j & 3], w[(j + 1) & 3]);
        x = _mm_add_epi32(x, _mm_alignr_epi8(w[(j + 3) & 3], w[(j + 2) & 3], 4));
        word = _mm_sha256msg2_epu32(x, w[(j + 3) & 3]);
      }
      w[j & 3] = word;
      __m128i msg = _mm_add_epi32(word, _mm_loadu_si128((const __m128i*)&kK[4 * j]));
      state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
      msg = _mm_shuffle_epi32(msg, 0x0E);
      state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
    }
    state0 = _mm_add_epi32(state0, abef);
    state1 = _mm_add_epi32(state1, cdgh);
  }
  tmp = _mm_shuffle_epi32(state0, 0x1B);        // FEBA
  state1 = _mm_shuffle_epi32(state1, 0xB1);     // DCHG
  state0 = _mm_blend_epi16(tmp, state1, 0xF0);  // DCBA
  state1 = _mm_alignr_epi8(state1, tmp, 8);     // HGFE
  _mm_storeu_si128((__m128i*)&state[0], state0);
  _mm_storeu_si128((__m128i*)&state[4], state1);
}

bool cpu_has_shani() {
  unsigned a = 0, b = 0, c = 0, d = 0;
  if (!__get_cpuid(1, &a, &b, &c, &d)) return false;
  const bool ssse3 = c & (1u << 9), sse41 = c & (1u << 19);
  if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return false;
  return ssse3 && sse41 && (b & (1u << 29));
}
#else
bool cpu_has_shani() { return false; }
#endif

Compress best_compress() {
#if SIC_SHA256_HAVE_X86
  static const Compress chosen = cpu_has_shani() ? &compress_shani : &compress_portable;
  return chosen;
#else
  return &compress_portable;
#endif
}

}  // namespace sic::support::sha256
namespace sic::support::sha256 {
Hasher::Hasher(Compress compress) : compress_(compress) {}
}  // namespace sic::support::sha256

namespace sic::support::sha256 {
void Hasher::update(const void* input, size_t size) {
  const auto* data = static_cast<const unsigned char*>(input);
  total_ += size;
  if (fill_) {
    if (size == 0) return;
    const size_t take = std::min(size, (size_t)64 - fill_);
    std::memcpy(block_ + fill_, data, take);
    fill_ += take;
    data += take;
    size -= take;
    if (fill_ < 64) return;
    compress_(state_.data(), block_, 1);
    fill_ = 0;
  }
  if (size >= 64) {
    compress_(state_.data(), data, size / 64);
    data += size & ~(size_t)63;
    size &= 63;
  }
  if (size) {
    std::memcpy(block_, data, size);
    fill_ = size;
  }
}
}  // namespace sic::support::sha256

namespace sic::support::sha256 {
std::array<unsigned char, 32> Hasher::finish() {
  const uint64_t bits = total_ * 8;
  unsigned char pad[72] = {0x80};
  const size_t padding = (fill_ < 56 ? 56 : 120) - fill_;
  for (int i = 0; i < 8; ++i) pad[padding + i] = (unsigned char)(bits >> (56 - 8 * i));
  update(pad, padding + 8);
  std::array<unsigned char, 32> out{};
  for (int i = 0; i < 8; ++i)
    for (int k = 0; k < 4; ++k) out[4 * i + k] = (unsigned char)(state_[i] >> (24 - 8 * k));
  return out;
}
}  // namespace sic::support::sha256

namespace sic::support::sha256 {
std::string Hasher::finish_hex() { return sic::support::hex_encode(finish()); }
}  // namespace sic::support::sha256

namespace sic::support::sha256 {
Digest digest(const void* data, size_t size) {
  Hasher hasher;
  hasher.update(data, size);
  return hasher.finish();
}
}  // namespace sic::support::sha256

namespace sic::support::sha256 {
std::string digest_hex(const void* data, size_t size) {
  Hasher hasher;
  hasher.update(data, size);
  return hasher.finish_hex();
}
}  // namespace sic::support::sha256

namespace sic::support::sha256 {
std::string digest_hex(std::string_view text) { return digest_hex(text.data(), text.size()); }
}  // namespace sic::support::sha256

namespace sic::support::sha256 {
std::string digest_hex(std::span<const unsigned char> bytes) {
  return digest_hex(bytes.data(), bytes.size());
}
}  // namespace sic::support::sha256

namespace sic::support::sha256 {
std::string file_hex(const std::filesystem::path& path, Compress compress) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    throw sic::Error(sic::ErrorKind::io,
                     "SHA-256: cannot open " + path.string() + ": " + std::strerror(errno));
  struct Closer {
    int fd;
    ~Closer() { ::close(fd); }
  } closer{fd};
#ifdef POSIX_FADV_SEQUENTIAL
  (void)::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
  // Double buffering: chunk k+1 is read on a helper thread while chunk k is
  // hashed, so the rate is min(read, compress) instead of their harmonic sum.
  // The bytes are hashed strictly in file order; a short chunk means EOF.
  constexpr size_t kChunk = size_t(8) << 20;
  struct ReadResult {
    size_t filled;
    int error;
  };
  const auto read_full = [fd](unsigned char* data) -> ReadResult {
    size_t filled = 0;
    while (filled < kChunk) {
      const ssize_t count = ::read(fd, data + filled, kChunk - filled);
      if (count < 0) {
        if (errno == EINTR) continue;
        return {filled, errno};
      }
      if (count == 0) break;
      filled += (size_t)count;
    }
    return {filled, 0};
  };
  std::unique_ptr<unsigned char[]> current(new unsigned char[kChunk]),
      next(new unsigned char[kChunk]);
  Hasher hasher(compress);
  auto [filled, error] = read_full(current.get());
  while (!error && filled == kChunk) {
    auto pending = std::async(std::launch::async, read_full, next.get());
    hasher.update(current.get(), filled);
    const auto result = pending.get();
    filled = result.filled;
    error = result.error;
    std::swap(current, next);
  }
  if (error)
    throw sic::Error(sic::ErrorKind::io,
                     "SHA-256: read failed for " + path.string() + ": " + std::strerror(error));
  hasher.update(current.get(), filled);
  return hasher.finish_hex();
}
}  // namespace sic::support::sha256

#include <algorithm>
#include <cerrno>
#include <stdexcept>
namespace sic::support::sha256 {
std::string range_hash(int fd, uint64_t offset, uint64_t size, std::span<unsigned char> scratch) {
  const auto maximum = static_cast<uint64_t>(std::numeric_limits<off_t>::max());
  if (size && (offset > maximum || size > maximum - offset))
    throw std::overflow_error("checksum range exceeds off_t range");
  if (size && scratch.empty()) throw std::invalid_argument("empty checksum buffer");
  Hasher hasher;
  while (size) {
    const size_t bytes = static_cast<size_t>(std::min<uint64_t>(scratch.size(), size));
    read_exact(fd, static_cast<off_t>(offset), scratch.data(), bytes);
    hasher.update(scratch.data(), bytes);
    offset += bytes;
    size -= bytes;
  }
  return hasher.finish_hex();
}

}  // namespace sic::support::sha256

namespace sic::support::sha256 {
Descriptor::~Descriptor() {
  if (fd >= 0) close(fd);
}

void Descriptor::reset() {
  if (fd >= 0) close(fd);
  fd = -1;
}

void read_exact(int fd, off_t offset, void* destination, size_t bytes) {
  auto* out = static_cast<unsigned char*>(destination);
  while (bytes) {
    const ssize_t done = pread(fd, out, bytes, offset);
    if (done < 0 && errno == EINTR) continue;
    if (done <= 0) throw sic::Error(sic::ErrorKind::io, "truncated or unreadable sealed range");
    out += done;
    bytes -= done;
    offset += done;
  }
}

}  // namespace sic::support::sha256
