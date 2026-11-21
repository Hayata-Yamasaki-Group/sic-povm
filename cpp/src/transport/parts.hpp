// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/transport/parts.hpp"
namespace sic::transport {
// The caller selects whether the digits came from its request or a stored record.
uint64_t parse_u64(const std::string& s, ErrorKind kind);
// ------------------------------------------------------------------ processes
int run(const std::vector<std::string>& args);
// `sh -c 'CMD "$0"' ARG`: CMD is a shell command line, ARG its last argument.
int run_sh(const std::string& cmd, const std::string& arg);
std::string capture_sh(const std::string& cmd);

// ------------------------------------------------------------------ file helpers
std::string hash_range(int fd, uint64_t offset, uint64_t size, std::vector<unsigned char>& buf);
void pwrite_all(int fd, const unsigned char* data, size_t n, uint64_t offset);
// Free bytes on the file system holding dir.
uint64_t free_bytes(const std::filesystem::path& dir);
void check_disk(const std::filesystem::path& dir, uint64_t need, const char* what);

// Makes every part range of the open work file match the parts file. Ranges that already hash right
// are kept (when hash_existing); others are taken from the source (a local directory, a file:// or
// https:// base URL, or the GitHub release URL), verified, written with pwrite and fsynced.
// Returns the indices that had to be written.
std::string source_base(const Parts& p, const std::string& source);
std::vector<uint64_t> sync_parts(int fd, const Parts& p, const std::string& source, unsigned jobs,
                                 bool hash_existing, const std::filesystem::path& tmp_dir);

}  // namespace sic::transport
