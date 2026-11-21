// SPDX-License-Identifier: AGPL-3.0-or-later
// SICX transport layer (docs/sicx.md#10b section 10a): split a canonical .sicx into GitHub Release
// parts, and fetch / repair it from parts. The layer knows only byte ranges and SHA-256; the
// Reader is used read-only as the final self-check. Parts file:
// data/d<d>/m<N>/multiplet.sicx.parts.json (schema sicx-transport-parts/1.0, canonical JSON, every
// number a string).
#pragma once

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>

#include "sic/sicx/verify.hpp"

namespace sic::transport {

inline constexpr const char* kSchema = "sicx-transport-parts/1.0";
inline constexpr uint64_t kPartSize = 1'900'000'000ull;
inline constexpr uint64_t kMaxParts = 1ull << 24;
inline constexpr const char* kLogicalName = "multiplet.sicx";

struct Part {
  uint64_t index = 0, offset = 0, size = 0;
  std::string sha256, asset;
};
struct Parts {
  uint64_t size = 0, part_size = 0;
  std::string sha256, exact_root, file_root, repository, tag;
  std::vector<Part> parts;
};

bool is_hex64(const std::string& s);
bool word(const std::string& s, const char* extra);
// Prefix d<d>-m<N>.
bool valid_prefix(const std::string& p);
std::string asset_name(const std::string& prefix, uint64_t i);
uint64_t parse_u64(const std::string& s);
uint64_t number(const sic::support::Json& j);

sic::support::Json parts_to_json(const Parts& p);

// Checks every invariant of docs/sicx.md#10b section 10a.
Parts parse_parts(std::string text);

const Part& part_at(const Parts& p, uint64_t offset);
uint64_t suffix_offset(const sic::sicx::Reader& r);
void validate_layout(const Parts& p, const sic::sicx::Reader& r);
// Parameterized packing kernel permits small-boundary tests without multi-GB files.
std::vector<sic::support::ByteRange> split_ranges(const sic::sicx::Reader& r, uint64_t ceiling);

std::string read_text(const std::filesystem::path& p);
void write_atomic(const std::filesystem::path& path, const std::string& text);

// ------------------------------------------------------------------ split
struct SplitOptions {
  std::filesystem::path file, parts_path;  // parts_path: where the parts file is written
  std::filesystem::path assets_dir;        // empty: hash only, no part files
  std::string prefix, repository = "OWNER/REPOSITORY", tag = "v1.0.0";
  uint64_t part_size = kPartSize;
  std::string upload_cmd, confirm_cmd;  // streaming mode (assets_dir required)
};

Parts split(const SplitOptions& o);

// ------------------------------------------------------------------ fetch / repair engine
// Whole-file SHA-256 and the SICX self-check (footer, PDIR, Merkle roots, EXMT) of a finished file.
void final_checks(const std::filesystem::path& file, const Parts& p);

}  // namespace sic::transport
