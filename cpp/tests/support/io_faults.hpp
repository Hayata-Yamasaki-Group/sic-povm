// SPDX-License-Identifier: AGPL-3.0-or-later
// Linker-only fault injection. No production seams or test-hook build required.
#pragma once
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#ifdef SIC_TEST_WRAP_STREAMS
#include <fstream>
#endif
#if defined(SIC_TEST_WRAP_STORAGE) || defined(SIC_TEST_WRAP_OPEN)
#include <fcntl.h>
#endif

namespace faults {
struct Fault {
  int at = 0, calls = 0, error = EIO;
  bool hit() { return ++calls == at; }
};
inline Fault pread, write, pwrite, close, fsync, fdatasync, link, unlink, rename, publish;
#ifdef SIC_TEST_WRAP_NUMA
inline Fault mempolicy;
#endif
inline Fault open, mkstemp, mkdtemp, fopen, fdopen, fclose, fflush, fwrite, fputc, fread, read,
    fstat;
inline FILE* read_error_stream = nullptr;
inline std::vector<std::string> private_files;
inline std::function<void(const char*, const char*)> rename_race;
#ifdef SIC_TEST_WRAP_STREAMS
inline Fault stream_read;
#endif
inline int sync_countdown = 0;
inline std::function<void(int)> sync_observer;
inline bool seek_failure = false, short_fwrite = false, trailing_byte = false,
            trailing_read_error = false;
inline std::optional<std::string> replace_on_seek;
inline int write_error_after = 0, pwrite_error_after = 0;
inline bool short_read = false;
inline bool short_write = false, short_pwrite = false, zero_write = false, zero_pwrite = false;
inline void reset() {
#ifdef SIC_TEST_WRAP_STREAMS
  stream_read = {};
#endif
#ifdef SIC_TEST_WRAP_NUMA
  mempolicy = {};
#endif
#ifdef SIC_TEST_WRAP_READ
  read = {};
#endif
  sync_countdown = 0;
  sync_observer = {};
  seek_failure = short_fwrite = trailing_byte = trailing_read_error = false;
  replace_on_seek.reset();
  pread = write = pwrite = close = fsync = fdatasync = link = unlink = rename = publish = {};
  write_error_after = pwrite_error_after = 0;
  short_read = false;
  short_write = short_pwrite = zero_write = zero_pwrite = false;
  open = mkstemp = mkdtemp = fopen = fdopen = fclose = fflush = fwrite = fputc = fread = read =
      fstat = {};
  read_error_stream = nullptr;
  rename_race = {};
}
inline int failure(const Fault& fault) {
  errno = fault.error;
  return -1;
}
}  // namespace faults
inline void sic_test_io_check(bool ok) {
  if (!ok) std::abort();
}
extern "C" {
#ifdef SIC_TEST_WRAP_STREAMS
// exceptions(badbit) calls clear after installing its mask. Inject a real badbit
// there, covering ifstream reads inside libstdc++ without a production hook.
void sic_test_real_ios_clear(std::ios*, std::ios::iostate) asm(
    "__real__ZNSt9basic_iosIcSt11char_traitsIcEE5clearESt12_Ios_Iostate");
void sic_test_wrap_ios_clear(std::ios* stream, std::ios::iostate state) asm(
    "__wrap__ZNSt9basic_iosIcSt11char_traitsIcEE5clearESt12_Ios_Iostate");
void sic_test_wrap_ios_clear(std::ios* stream, std::ios::iostate state) {
  if (state == std::ios::goodbit && stream->exceptions() == std::ios::badbit &&
      dynamic_cast<std::ifstream*>(stream) && faults::stream_read.hit())
    state = std::ios::badbit;
  sic_test_real_ios_clear(stream, state);
}
#endif
#ifdef SIC_TEST_WRAP_WRITE
ssize_t __real_write(int, const void*, size_t);
ssize_t __wrap_write(int fd, const void* data, size_t size) {
  if (faults::write.hit()) {
    if (faults::zero_write) return 0;
    if (faults::short_write) return __real_write(fd, data, size > 1 ? size / 2 : size);
    return faults::failure(faults::write);
  }
  if (faults::write_error_after == faults::write.calls) {
    errno = ENOSPC;
    return -1;
  }
  return __real_write(fd, data, size);
}
#endif
#ifdef SIC_TEST_WRAP_PWRITE
ssize_t __real_pwrite(int, const void*, size_t, off_t);
ssize_t __wrap_pwrite(int fd, const void* data, size_t size, off_t offset) {
  if (faults::pwrite.hit()) {
    if (faults::zero_pwrite) return 0;
    if (faults::short_pwrite) return __real_pwrite(fd, data, size > 1 ? size / 2 : size, offset);
    return faults::failure(faults::pwrite);
  }
  if (faults::pwrite_error_after == faults::pwrite.calls) {
    errno = ENOSPC;
    return -1;
  }
  return __real_pwrite(fd, data, size, offset);
}
#endif
#ifdef SIC_TEST_WRAP_PREAD
ssize_t __real_pread(int, void*, size_t, off_t);
ssize_t __wrap_pread(int fd, void* data, size_t size, off_t offset) {
  if (faults::pread.hit()) {
    if (faults::short_read) return __real_pread(fd, data, size > 1 ? size / 2 : size, offset);
    return faults::failure(faults::pread);
  }
  return __real_pread(fd, data, size, offset);
}
#ifdef __GLIBC__
// Fortified call sites must reach the same fault state without losing bounds checks.
ssize_t __real___pread_chk(int, void*, size_t, off_t, size_t);
ssize_t __wrap___pread_chk(int fd, void* data, size_t size, off_t offset, size_t capacity) {
  if (size > capacity) return __real___pread_chk(fd, data, size, offset, capacity);
  if (faults::pread.hit()) {
    if (faults::short_read)
      return __real___pread_chk(fd, data, size > 1 ? size / 2 : size, offset, capacity);
    return faults::failure(faults::pread);
  }
  return __real___pread_chk(fd, data, size, offset, capacity);
}
#endif
#endif
#ifdef SIC_TEST_WRAP_CLOSE
int __real_close(int);
int __wrap_close(int fd) {
  const bool hit = faults::close.hit();
  const int rc = __real_close(fd);  // Linux may release the fd even on failure.
  return hit ? faults::failure(faults::close) : rc;
}
#endif
#ifdef SIC_TEST_WRAP_FSYNC
int __real_fsync(int);
int __wrap_fsync(int fd) {
  const auto observer = faults::sync_observer;
  if (observer) observer(fd);
  if (faults::sync_countdown && --faults::sync_countdown == 0) {
    errno = EIO;
    return -1;
  }
  return faults::fsync.hit() ? faults::failure(faults::fsync) : __real_fsync(fd);
}
#endif
#ifdef SIC_TEST_WRAP_FDATASYNC
int __real_fdatasync(int);
int __wrap_fdatasync(int fd) {
  return faults::fdatasync.hit() ? faults::failure(faults::fdatasync) : __real_fdatasync(fd);
}
#endif
}

#include "io_faults_publication.hpp"
#include "io_faults_stdio.hpp"
