// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared SHA-256 (FIPS 180-4): runtime-selected x86 SHA-NI with portable fallback.
// One-shot digest/digest_hex, incremental Hasher, and bounded-memory file_hex.
// File reads use two 8 MiB buffers (read k+1 while hashing k); link Threads::Threads.
#pragma once

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace sic::support::sha256 {

inline constexpr std::array<uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline constexpr std::array<uint32_t, 8> kInitialState = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

using Compress = void (*)(uint32_t* state, const unsigned char* data, size_t blocks);

void compress_portable(uint32_t* state, const unsigned char* data, size_t blocks);

Compress best_compress();

class Hasher {
 public:
  explicit Hasher(Compress compress = best_compress());
  // A zero-byte update accepts nullptr even when a partial block is buffered.
  void update(const void* input, size_t size);
  // Finalizes this instance; do not update or finish it again.
  std::array<unsigned char, 32> finish();
  std::string finish_hex();

 private:
  Compress compress_;
  std::array<uint32_t, 8> state_ = kInitialState;
  unsigned char block_[64] = {};
  size_t fill_ = 0;
  uint64_t total_ = 0;
};

using Digest = std::array<unsigned char, 32>;

Digest digest(const void* data, size_t size);
std::string digest_hex(const void* data, size_t size);
std::string digest_hex(std::string_view text);
std::string digest_hex(std::span<const unsigned char> bytes);

// Lowercase hex SHA-256 of the bytes read from a fresh open() of path (like
// `sha256sum < path`; /dev/fd/N and /proc/PID/exe paths reopen the target).
// Throws on any open/read error.
std::string file_hex(const std::filesystem::path& path, Compress compress = best_compress());

inline constexpr size_t kFileChunkBytes = size_t{8} << 20;
// Visit the same bytes hashed, in file order, on the calling thread.
using FileChunkVisitor = std::function<void(std::span<const unsigned char>)>;
std::string file_hex_visit(const std::filesystem::path& path, bool read_ahead,
                           const FileChunkVisitor& visit, Compress compress = best_compress());

}  // namespace sic::support::sha256

#include <unistd.h>
namespace sic::support::sha256 {

struct Descriptor {
  int fd = -1;
  Descriptor() = default;
  explicit Descriptor(int value) : fd(value) {}
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  ~Descriptor();
  void reset();
};

void read_exact(int fd, off_t offset, void* destination, size_t bytes);

// Caller-owned scratch bounds memory. Empty ranges hash the empty string; a
// nonempty scratch buffer is required for nonempty ranges. Short/failed reads
// throw sic::Error(io); unrepresentable ranges throw std::overflow_error.
std::string range_hash(int fd, uint64_t offset, uint64_t size, std::span<unsigned char> scratch);

}  // namespace sic::support::sha256
