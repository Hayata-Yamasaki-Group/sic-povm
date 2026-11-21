// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/transport/parts.hpp"
namespace sic::transport {
struct TransferJob {
  Parts parts;
  std::filesystem::path out_dir, final_path;
  std::string source, name;
  unsigned jobs = 4;
};
uint64_t inflight(const TransferJob& j);
uint64_t allocated(const std::filesystem::path& p);
int open_rw(const std::filesystem::path& p, bool create);
void preallocate(int fd, uint64_t size);
struct FetchOptions {
  TransferJob job;
  uint64_t extra;
  bool force;
};
int fetch(const FetchOptions& options);
}  // namespace sic::transport
