// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>

#include "sic/error.hpp"
#include "sic/support/durable_file.hpp"
namespace sic::construct {
class QuarantineOutput {
  std::filesystem::path target_, private_, lock_path_;
  int lock_ = -1, completed_fd_ = -1;
  bool replay_completed_ = false, owned_completed_ = false;
  struct stat completed_ {};
  static bool same_inode(const struct stat& a, const struct stat& b) {
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
  }
  static struct stat regular(const std::filesystem::path& path);
  void pin();

 public:
  static void validate_target(const std::filesystem::path& path);
  explicit QuarantineOutput(std::filesystem::path target, bool resume);
  QuarantineOutput(const QuarantineOutput&) = delete;
  QuarantineOutput& operator=(const QuarantineOutput&) = delete;
  ~QuarantineOutput();
  const std::filesystem::path& path() const { return private_; }
  bool has_completed_replay() const { return replay_completed_; }
  // Only after the spool journal, plan and component note authenticate successfully.
  void prepare_replay(bool authenticated);
  void completed();
  void publish();
};
}  // namespace sic::construct
