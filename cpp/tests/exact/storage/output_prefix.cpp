// SPDX-License-Identifier: AGPL-3.0-or-later
#include <filesystem>
#include <functional>
#include <thread>

#include "assertions.hpp"
#include "exact/storage/leaf_source.hpp"
#include "sicx/codec/integer.hpp"
#include "support/sha256.hpp"

using namespace sic::exact::storage;

static bool refuses(const std::function<void()>& run, std::string_view message = "") {
  try {
    run();
  } catch (const std::exception& error) {
    return std::string_view(error.what()).find(message) != std::string_view::npos;
  }
  return false;
}

struct Temporary {
  FILE* file = tmpfile();
  Temporary() {
    if (!file) throw std::runtime_error("temporary output creation failed");
  }
  ~Temporary() { fclose(file); }
};

static std::vector<Leaf> values(size_t first, size_t count) {
  std::vector<Leaf> result(count);
  for (size_t j = 0; j < count; ++j) {
    const size_t k = first + j;
    if ((k % 31) < 8) continue;
    fmpz_set_si(result[j].x, static_cast<long>(k % 1009) - 507);
    fmpz_set_si(result[j].y, 1 - static_cast<long>(k % 127));
    if (k % 997 == 11) {
      fmpz_one(result[j].y);
      fmpz_mul_2exp(result[j].y, result[j].y, 16999);
      fmpz_add_ui(result[j].y, result[j].y, 37);
      if (k & 1) fmpz_neg(result[j].y, result[j].y);
    }
  }
  return result;
}

static void same(const Leaf& a, const Leaf& b) {
  SIC_CHECK(fmpz_equal(a.x, b.x));
  SIC_CHECK(fmpz_equal(a.y, b.y));
}

static void hash_tests() {
  Temporary file;
  std::array<unsigned char, 65536> scratch{};
  fputs("prefixabc", file.file);
  fflush(file.file);
  SIC_CHECK(sic::support::sha256::range_hash(fileno(file.file), 6, 3, scratch) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  SIC_CHECK(refuses([&] { sic::support::sha256::range_hash(fileno(file.file), 0, 12, scratch); },
                    "truncated"));
  SIC_CHECK(refuses([&] { sic::support::sha256::range_hash(-1, 0, 9, scratch); }, "unreadable"));
  SIC_CHECK(lseek(fileno(file.file), 3, SEEK_SET) == 3);
  SIC_CHECK(sic::support::sha256::range_hash(fileno(file.file), 6, 3, scratch) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  SIC_CHECK(lseek(fileno(file.file), 0, SEEK_CUR) == 3);
  // Cross several bounded read buffers without changing the descriptor position.
  SIC_CHECK(ftruncate(fileno(file.file), 2 * sic::support::resources::kMib) == 0);
  sic::support::sha256::Hasher reference;
  std::array<unsigned char, 65536> zeros{};
  reference.update("prefixabc", 9);
  size_t remaining = 2 * sic::support::resources::kMib - 9;
  while (remaining) {
    const size_t count = std::min(remaining, zeros.size());
    reference.update(zeros.data(), count);
    remaining -= count;
  }
  SIC_CHECK(sic::support::sha256::range_hash(fileno(file.file), 0,
                                             2 * sic::support::resources::kMib,
                                             scratch) == reference.finish_hex());
  SIC_CHECK(lseek(fileno(file.file), 0, SEEK_CUR) == 3);
}

static void private_sources() {
  auto data = values(100, 20);
  Private first(true, 5), second(true, 5);
  auto generate = [&](size_t start, std::span<Leaf> block) {
    for (size_t j = 0; j < block.size(); ++j) {
      fmpz_set(block[j].x, data[start + j].x);
      fmpz_set(block[j].y, data[start + j].y);
    }
  };
  const auto source = first.append_generated(data.size(), generate);
  const auto foreign = second.append_generated(data.size(), generate);
  size_t visited = 0;
  source.read_blocks(
      [&](size_t start, std::span<Leaf> block) {
        SIC_CHECK(start == visited);
        for (const auto& leaf : block) same(leaf, data[visited++]);
      },
      7);
  SIC_CHECK(visited == data.size());
  bool thread_refused = false;
  std::thread wrong_thread(
      [&] { thread_refused = refuses([&] { Source::Cursor other(source); }, "coordinator"); });
  wrong_thread.join();
  SIC_CHECK(thread_refused);
  Source::Cursor end(source, data.size());
  SIC_CHECK(refuses([&] { end.next(); }, "past end"));
  SIC_CHECK(refuses([&] { end.next(); }, "cannot resume"));
  SIC_CHECK(refuses([&] { first.concatenate({source, foreign}); }, "attempt"));
  const auto combined = first.concatenate({source, source});
  Source::Cursor cursor(combined, 19);
  same(cursor.next(), data[19]);
  same(cursor.next(), data[0]);
  first.invalidate();
  SIC_CHECK(refuses([&] { cursor.next(); }, "expired"));
  SIC_CHECK(refuses([&] { Source::Cursor dead(source); }, "expired"));
  SIC_CHECK(
      refuses([&] { foreign.read_blocks([&](size_t, std::span<Leaf>) { second.invalidate(); }); },
              "expired"));
  Private generation(true, 5);
  SIC_CHECK(refuses(
      [&] {
        generation.append_generated(65537,
                                    [&](size_t, std::span<Leaf>) { generation.invalidate(); });
      },
      "expired"));
}

int main() {
  try {
    flint_set_num_threads(1);
    setenv("SIC_SPOOL_LATTICE", "1", 1);
    hash_tests();
    private_sources();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  flint_cleanup();
  return sic_test::report();
}
