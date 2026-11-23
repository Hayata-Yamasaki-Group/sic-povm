// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/quarantine.hpp"

#include "sic/support/json.hpp"
#include "support/io/durable_output.hpp"
#include "support/io/write_all.hpp"

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

void QuarantineOutput::record_ray_pair(uint64_t file_bytes, std::string_view file_root,
                                       std::string_view exact_root, uint64_t ray_bytes,
                                       std::string_view ray_sha256) {
  const auto sealed = regular(private_);
  if (!owned_completed_ || !same_inode(sealed, completed_) || sealed.st_size < 0 ||
      static_cast<uint64_t>(sealed.st_size) != file_bytes)
    throw sic::Error(sic::ErrorKind::integrity, "quarantine changed before recording ray pair");
  struct stat lock {};
  if (::fstat(lock_, &lock) || !S_ISREG(lock.st_mode) || lock.st_nlink != 1 || (lock.st_mode & 077))
    throw sic::Error(sic::ErrorKind::io, "producer lock is not a private regular file");
  using sic::support::Json;
  Json pin;
  pin["schema"] = Json::string_value("sic-producer-ray-pair/1.0");
  pin["device"] = Json::string_value(std::to_string(sealed.st_dev));
  pin["inode"] = Json::string_value(std::to_string(sealed.st_ino));
  pin["file_bytes"] = Json::string_value(std::to_string(file_bytes));
  pin["file_root"] = Json::string_value(std::string(file_root));
  pin["exact_root"] = Json::string_value(std::string(exact_root));
  pin["ray_bytes"] = Json::string_value(std::to_string(ray_bytes));
  pin["ray_sha256"] = Json::string_value(std::string(ray_sha256));
  const auto text = sic::support::dump_json(pin) + '\n';
  sic::support::write_all(lock_, {reinterpret_cast<const uint8_t*>(text.data()), text.size()}, 0);
  if (::ftruncate(lock_, static_cast<off_t>(text.size())))
    throw sic::Error(sic::ErrorKind::io, "cannot set producer pair record length");
  sic::support::sync_file(lock_);
  sic::support::sync_parent(lock_path_);
}

void QuarantineOutput::publish() try {
  if (!owned_completed_ || !same_inode(regular(private_), completed_))
    throw sic::Error(sic::ErrorKind::integrity, "quarantine changed before publication");
  sic::support::publish_pinned_no_replace(completed_fd_, private_, target_);
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
