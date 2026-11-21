// SPDX-License-Identifier: AGPL-3.0-or-later
// Checked POSIX durability primitives and atomic publication of small files.
#pragma once

#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace sic::support {

// Sync the descriptor, retrying EINTR; I/O failures throw sic::Error(io).
void sync_file(int fd, bool data_only = false);

// Open and fully sync a file or directory, then close it; I/O failures throw sic::Error(io).
void sync_file(const std::filesystem::path& path);

// Sync and close the parent directory; I/O failures throw sic::Error(io).
void sync_parent(const std::filesystem::path& file);

// Atomic POSIX rename, without syncing either directory. Replaces files or an
// empty destination directory; refuses a nonempty destination directory.
// I/O failures throw sic::Error(io).
void publish_replace(const std::filesystem::path& from, const std::filesystem::path& to);

// Atomic no-clobber move. Writers use it instead of an existence check followed by rename.
// I/O failures throw sic::Error(io); unlink failure may leave both names published.
void publish_no_replace(const std::filesystem::path& from, const std::filesystem::path& to);

// Write a same-directory exclusive temporary, sync it, publish it, then sync the
// directory. data_only applies only to the file; the directory always gets a full fsync.
// With replace=false, atomic link publication refuses an existing
// destination, including a dangling symlink. A sync failure after publication
// throws but leaves the published file in place; callers must not assume rollback.
// I/O failures throw sic::Error(io), including destination collisions.
void atomic_write(const std::filesystem::path& path, std::span<const uint8_t> bytes,
                  bool replace = true, mode_t mode = 0644, bool data_only = false);

// Text overload of atomic_write; I/O failures throw sic::Error(io).
void atomic_write(const std::filesystem::path& path, std::string_view text, bool replace = true,
                  mode_t mode = 0644, bool data_only = false);

}  // namespace sic::support
