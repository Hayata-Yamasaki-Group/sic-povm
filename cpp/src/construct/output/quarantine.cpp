// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/quarantine.hpp"

namespace sic::construct {
struct stat QuarantineOutput::regular(const std::filesystem::path& path) {
  struct stat value {};
  if (::lstat(path.c_str(), &value) || !S_ISREG(value.st_mode))
    throw sic::Error(sic::ErrorKind::io, "quarantine path is not a regular file");
  return value;
}

void QuarantineOutput::pin() {
  if (completed_fd_ >= 0) ::close(completed_fd_);
  completed_fd_ = ::open(private_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (completed_fd_ < 0 || ::fstat(completed_fd_, &completed_) || !S_ISREG(completed_.st_mode))
    throw sic::Error(sic::ErrorKind::io, "cannot pin completed quarantine");
}

void QuarantineOutput::validate_target(const std::filesystem::path& path) {
  if (path.empty() || path.filename().empty())
    throw sic::Error(sic::ErrorKind::invalid_argument, "output requires a nonempty file name");
  for (const auto& part : path)
    if (part.string().find(".sicx-quarantine") != std::string::npos)
      throw sic::Error(sic::ErrorKind::invalid_argument,
                       ".sicx-quarantine is a reserved producer scratch namespace");
}

QuarantineOutput::QuarantineOutput(std::filesystem::path target, bool resume) try
    : target_(std::move(target)),
      private_(target_.string() + ".sicx-quarantine"),
      lock_path_((target_.has_parent_path() ? target_.parent_path() : std::filesystem::path(".")) /
                 ("." + target_.filename().string() + ".producer-lock")) {
  validate_target(target_);
  lock_ = ::open(lock_path_.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (lock_ < 0) throw sic::Error(sic::ErrorKind::io, "cannot open producer output lock");
  try {
    if (::flock(lock_, LOCK_EX | LOCK_NB))
      throw sic::Error(sic::ErrorKind::io, "producer output is locked");
    if (std::filesystem::symlink_status(target_).type() != std::filesystem::file_type::not_found)
      throw sic::Error(sic::ErrorKind::io, "output already exists");
    if (std::filesystem::symlink_status(private_).type() != std::filesystem::file_type::not_found) {
      if (!resume) throw sic::Error(sic::ErrorKind::io, "quarantine output exists; use --resume");
      pin();
      replay_completed_ = true;
    }
  } catch (...) {
    if (completed_fd_ >= 0) ::close(completed_fd_);
    ::close(lock_);
    lock_ = -1;
    throw;
  }
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

QuarantineOutput::~QuarantineOutput() {
  if (owned_completed_) {
    struct stat now {};
    if (::lstat(private_.c_str(), &now) == 0 && same_inode(now, completed_))
      ::unlink(private_.c_str());
  }
  if (completed_fd_ >= 0) ::close(completed_fd_);
  if (lock_ >= 0) {
    ::flock(lock_, LOCK_UN);
    ::close(lock_);
  }
  // Keep the tiny lock inode; unlinking it could split concurrent owners across two inodes.
}

void QuarantineOutput::prepare_replay(bool authenticated) {
  if (!replay_completed_) return;
  if (!authenticated)
    throw sic::Error(sic::ErrorKind::integrity, "completed replay is not authenticated");
  if (!same_inode(regular(private_), completed_))
    throw sic::Error(sic::ErrorKind::integrity, "quarantine changed before replay");
  if (::unlink(private_.c_str()))
    throw sic::Error(sic::ErrorKind::io, "cannot remove completed quarantine for replay");
  replay_completed_ = false;
  ::close(completed_fd_);
  completed_fd_ = -1;
}

void QuarantineOutput::publish() try {
  if (!owned_completed_ || !same_inode(regular(private_), completed_))
    throw sic::Error(sic::ErrorKind::integrity, "quarantine changed before publication");
#if defined(__linux__)
  // Link the pinned checked inode; replacing its private name cannot substitute another file.
  const auto pinned = "/proc/self/fd/" + std::to_string(completed_fd_);
  if (::linkat(AT_FDCWD, pinned.c_str(), AT_FDCWD, target_.c_str(), AT_SYMLINK_FOLLOW))
    throw sic::Error(sic::ErrorKind::io, "cannot publish checked producer output: " +
                                             std::string(std::strerror(errno)));
  struct stat now {};
  if (::lstat(private_.c_str(), &now) == 0 && same_inode(now, completed_))
    if (::unlink(private_.c_str()))
      throw sic::Error(sic::ErrorKind::io, "cannot remove published quarantine name");
#else
  if (!same_inode(regular(private_), completed_))
    throw sic::Error(sic::ErrorKind::integrity, "quarantine changed before publication");
  sic::support::publish_no_replace(private_, target_);
#endif
  owned_completed_ = false;
  for (const auto* suffix : {".spool.partial", ".spool.partial.journal"})
    std::filesystem::remove(private_.string() + suffix);
  sic::support::sync_parent(target_);
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

}  // namespace sic::construct

namespace sic::construct {
void QuarantineOutput::completed() {
  pin();
  owned_completed_ = true;
}
}  // namespace sic::construct
