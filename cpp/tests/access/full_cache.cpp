// SPDX-License-Identifier: AGPL-3.0-or-later
#include <fstream>
#include <iostream>

#include "access/range_cache.hpp"
#include "sic/access/remote.hpp"
#include "sic/error.hpp"
#include "sic/support/durable_file.hpp"
#include "support/io_faults.hpp"
#include "support/sha256.hpp"
#include "transport/parts.hpp"

namespace fs = std::filesystem;
static void check(bool value, const char* why) {
  if (!value) throw std::runtime_error(why);
}
static std::string contents(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  check(bool(input), "open test input");
  return {std::istreambuf_iterator<char>(input), {}};
}

int main(int argc, char** argv) try {
  check(argc == 2, "expected fixture path");
  char pattern[] = "/tmp/sic-full-cache-XXXXXX";
  check(::mkdtemp(pattern), "mkdtemp");
  const fs::path dir(pattern), assets = dir / "assets", manifest = dir / "parts.json";
  struct Cleanup {
    fs::path path;
    ~Cleanup() {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
  } cleanup{dir};
  sic::transport::SplitOptions split;
  split.file = argv[1];
  split.parts_path = manifest;
  split.assets_dir = assets;
  split.prefix = "d4-m1";
  const auto parts = sic::transport::split(split);
  const auto bytes = contents(argv[1]);
  // Exercise constructor validation and validation under the full-download lock,
  // with both the default and finite-budget check_full paths.
  for (const bool finite_budget : {false, true}) {
    for (const bool during_admission : {false, true}) {
      sic::access::RemoteOptions options;
      options.source = assets.string();
      options.cache = dir / (std::to_string(finite_budget) + std::to_string(during_admission));
      options.jobs = 1;
      options.prefetch_bytes = 0;
      options.fetch_full = true;
      options.max_fraction = 0.01;
      if (finite_budget) options.memory_budget = uint64_t{8} << 30;
      const auto full =
          options.cache / "ranges" / parts.exact_root / parts.file_root /
          sic::access::checksum(sic::support::dump_json(sic::transport::parts_to_json(parts))) /
          "full.sicx";
      std::shared_ptr<sic::access::RemoteByteSource> source;
      const sic::support::ByteRange all{0, parts.size};
      const auto prepare = [&](std::string_view cached) {
        faults::reset();
        source.reset();
        fs::remove(full);
        if (during_admission) source = sic::access::remote_byte_source(manifest, options);
        fs::create_directories(full.parent_path());
        sic::support::atomic_write(full, cached);
      };
      const auto validate = [&] {
        if (!during_admission) source = sic::access::remote_byte_source(manifest, options);
        source->plan({&all, 1});
      };
      for (const bool wrong_size : {false, true}) {
        auto damaged = bytes;
        if (wrong_size)
          damaged.pop_back();
        else
          damaged[damaged.size() / 2] ^= 1;
        prepare(damaged);
        validate();
        check(source->stats().full_download && source->stats().transferred == parts.size,
              "corrupt cache was not refetched");
        check(contents(full) == bytes, "refetched cache bytes differ");
      }
      for (const int code : {EIO, EMFILE, EACCES}) {
        prepare(bytes);
        struct stat before {
        }, after{};
        check(::stat(full.c_str(), &before) == 0, "stat before read failure");
        faults::read = {1, 0, code};
        bool refused = false;
        try {
          validate();
        } catch (const sic::Error& error) {
          check(error.kind() == sic::ErrorKind::io, "cached read failure lost io category");
          check(std::string(error.what()).find(full.string()) != std::string::npos,
                "read failure did not originate in the cached file");
          refused = true;
        }
        check(refused && faults::read.calls == 1, "cached read fault did not propagate");
        faults::reset();
        check(::stat(full.c_str(), &after) == 0 && before.st_ino == after.st_ino,
              "I/O failure removed or replaced cached file");
        check(contents(full) == bytes, "I/O failure changed cached bytes");
        if (source)
          check(!source->stats().full_download && source->stats().transferred == 0,
                "I/O failure triggered refetch");
        // A healthy cache remains reusable without a source mirror.
        auto offline = options;
        offline.source = (dir / "unavailable").string();
        auto reused = sic::access::remote_byte_source(manifest, offline);
        check(reused->stats().full_download && reused->stats().transferred == 0,
              "healthy cache was not reused");
      }
    }
  }
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
