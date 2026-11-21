// FIPS 180-4 / NIST example vectors, SHA-NI vs portable on random inputs and
// random update splits, and file_hex vs coreutils sha256sum on fixture files.
#include "support/sha256.hpp"

#include "support/hex.hpp"
#ifdef SIC_HAVE_FLINT
#include "exact/group/group_checkpoint.hpp"
#endif

#include <sys/stat.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

namespace sha256 = sic::support::sha256;

static int failures = 0;
static void check(bool ok, const std::string& what) {
  if (!ok) {
    ++failures;
    std::cerr << "FAIL " << what << '\n';
  }
}
static std::string hex_of(const std::string& text, sha256::Compress compress, size_t split = 0) {
  sha256::Hasher hasher(compress);
  hasher.update(nullptr, 0);
  const auto* p = (const unsigned char*)text.data();
  if (split && split < text.size()) {
    hasher.update(p, split);
    hasher.update(nullptr, 0);  // Zero-length null input remains valid with a partial block.
    hasher.update(p + split, text.size() - split);
  } else
    hasher.update(p, text.size());
  hasher.update(nullptr, 0);
  return hasher.finish_hex();
}
static std::string coreutils(const std::filesystem::path& file) {
  const std::string command = "sha256sum < '" + file.string() + "'";
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) return "";
  char buffer[128] = {};
  const size_t n = fread(buffer, 1, 64, pipe);
  const int status = pclose(pipe);
  if (status) return "";
  return std::string(buffer, n);
}

int main(int argc, char** argv) {
  const std::array<uint8_t, 4> octets{0, 1, 0xab, 0xff};
  check(sic::support::hex_encode(octets) == "0001abff", "byte-order hex and leading zeros");
  check(sic::support::hex_encode({}).empty(), "empty hex");
  check(sic::support::hex_unsigned(0) == "0", "zero integer hex");
  check(sic::support::hex_unsigned(1, 16) == "0000000000000001", "padded integer hex");
  check(sic::support::hex_unsigned(UINT64_MAX) == "ffffffffffffffff", "maximum integer hex");
  std::vector<sha256::Compress> engines{&sha256::compress_portable};
  if (sha256::best_compress() != &sha256::compress_portable)
    engines.push_back(sha256::best_compress());
  std::cout << "engines=" << engines.size() << " best="
            << (sha256::best_compress() == &sha256::compress_portable ? "portable" : "sha-ni")
            << '\n';
  const std::vector<std::pair<std::string, std::string>> vectors = {
      {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
      {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
      {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
       "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
      {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmn"
       "opqrsmnopqrstnopqrstu",
       "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
      {std::string(1000000, 'a'),
       "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
      {"The quick brown fox jumps over the lazy dog",
       "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"}};
  for (auto compress : engines)
    for (const auto& [text, digest] : vectors)
      for (size_t split :
           {size_t(0), size_t(1), size_t(55), size_t(64), size_t(65), text.size() / 2})
        check(hex_of(text, compress, split) == digest,
              "vector len=" + std::to_string(text.size()) + " split=" + std::to_string(split));
  for (const auto& [text, digest] : vectors) {
    check(sha256::digest_hex(text) == digest, "one-shot digest_hex");
    const auto raw = sha256::digest(text.data(), text.size());
    sha256::Hasher h;
    h.update(text.data(), text.size());
    check(raw == h.finish(), "one-shot raw digest");
  }
  std::mt19937_64 random(20261101);
  for (int trial = 0; trial < 4000; ++trial) {
    const size_t length = trial < 1100 ? (size_t)trial : (size_t)(random() % 300000);
    std::string text(length, '\0');
    for (auto& c : text) c = (char)random();
    const size_t split = length ? (size_t)(random() % (length + 1)) : 0;
    const std::string reference = hex_of(text, &sha256::compress_portable);
    for (auto compress : engines)
      check(hex_of(text, compress, split) == reference, "random len=" + std::to_string(length));
    if (trial < 1100)
      for (auto compress : engines) {
        sha256::Hasher h(compress);
        for (size_t at = 0; at < text.size();) {
          const size_t count = std::min<size_t>(1 + random() % 97, text.size() - at);
          h.update(text.data() + at, count);
          h.update(nullptr, 0);
          at += count;
        }
        check(h.finish_hex() == reference, "multiple chunks and zero-byte updates");
      }
  }
  if (argc > 1) {
    const std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);
    for (size_t length : {size_t(0), size_t(1), size_t(55), size_t(56), size_t(63), size_t(64),
                          size_t(65), size_t(119), size_t(120), size_t(4194303), size_t(4194304),
                          size_t(4194305), size_t(8388607), size_t(8388608), size_t(8388609),
                          size_t(16777216), size_t(33554433)}) {  // chunk = 8 MiB
      const auto file = dir / ("fixture-" + std::to_string(length) + ".bin");
      {
        std::ofstream out(file, std::ios::binary);
        for (size_t i = 0; i < length; ++i) out.put((char)random());
      }
      const std::string expected = coreutils(file);
      check(expected.size() == 64, "sha256sum available");
      for (auto compress : engines)
        check(sha256::file_hex(file, compress) == expected, "file len=" + std::to_string(length));
      check(sha256::file_hex(file) == expected, "file_hex default len=" + std::to_string(length));
#ifdef SIC_HAVE_FLINT
      check(sic::exact::checkpoint_digest(file) == expected,
            "checkpoint_digest vs subprocess len=" + std::to_string(length));
#endif
    }
    {  // /dev/fd/N reopens the file: same digest, caller's offset untouched
      const auto file = dir / "fixture-8388609.bin";
      const int fd = ::open(file.c_str(), O_RDONLY);
      check(fd >= 0 && ::lseek(fd, 1000, SEEK_SET) == 1000, "open fixture");
      check(sha256::file_hex("/dev/fd/" + std::to_string(fd)) == sha256::file_hex(file),
            "/dev/fd digest");
      check(::lseek(fd, 0, SEEK_CUR) == 1000, "/dev/fd offset unchanged");
      ::close(fd);
    }
    {  // FIFO (short reads): digest of the streamed bytes
      const auto fifo = dir / "fixture.fifo";
      std::filesystem::remove(fifo);
      check(::mkfifo(fifo.c_str(), 0600) == 0, "mkfifo");
      std::string text(20000003, '\0');
      for (auto& c : text) c = (char)random();
      std::thread writer([&] {
        std::ofstream out(fifo, std::ios::binary);
        for (size_t at = 0; at < text.size(); at += 65521)
          out.write(text.data() + at, (std::streamsize)std::min<size_t>(65521, text.size() - at));
      });
      const std::string got = sha256::file_hex(fifo);
      writer.join();
      check(got == hex_of(text, &sha256::compress_portable), "FIFO digest");
      std::filesystem::remove(fifo);
    }
    if (::geteuid() != 0) {  // unreadable file throws
      const auto file = dir / "unreadable.bin";
      {
        std::ofstream out(file);
        out << "x";
      }
      ::chmod(file.c_str(), 0);
      bool unreadable = false;
      try {
        sha256::file_hex(file);
      } catch (const std::exception&) {
        unreadable = true;
      }
      check(unreadable, "unreadable file throws");
      ::chmod(file.c_str(), 0600);
      std::filesystem::remove(file);
    }
    bool threw = false;
    try {
      sha256::file_hex(dir / "missing");
    } catch (const std::exception&) {
      threw = true;
    }
    check(threw, "missing file throws");
    threw = false;
    try {
      sha256::file_hex(dir);
    } catch (const std::exception&) {
      threw = true;
    }
    check(threw, "directory throws");
  }
  std::cout << (failures ? "FAIL" : "PASS") << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
