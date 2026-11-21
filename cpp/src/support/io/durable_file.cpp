// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/durable_file.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <ostream>
#include <streambuf>
#include <system_error>

#include "sic/error.hpp"
#include "support/io/durable_output.hpp"
#include "support/io/write_all.hpp"
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

namespace sic::support::file_detail {
[[noreturn]] void error(const std::string& operation) {
  const int code = errno;
  throw sic::Error(sic::ErrorKind::io,
                   operation + ": " + std::error_code(code, std::generic_category()).message());
}
}  // namespace sic::support::file_detail

namespace sic::support {
namespace file_detail {
[[noreturn]] void error(const std::string& operation);

struct Descriptor {
  int fd;
  explicit Descriptor(int value) : fd(value) {}
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  ~Descriptor() {
    if (fd >= 0) ::close(fd);
  }
  void close() {
    const int value = fd;
    fd = -1;  // A failed close must not be retried on a possibly reused descriptor.
    if (::close(value)) error("close failed");
  }
};
}  // namespace file_detail
void write_all(int fd, std::span<const uint8_t> bytes, std::optional<uint64_t> offset) {
  const auto maximum = static_cast<uint64_t>(std::numeric_limits<off_t>::max());
  if (offset && (*offset > maximum || bytes.size() > maximum - *offset))
    throw std::overflow_error("positioned write exceeds off_t range");
  while (!bytes.empty()) {
    const size_t size =
        std::min(bytes.size(), static_cast<size_t>(std::numeric_limits<ssize_t>::max()));
    const ssize_t count = offset ? ::pwrite(fd, bytes.data(), size, static_cast<off_t>(*offset))
                                 : ::write(fd, bytes.data(), size);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      if (count == 0) errno = EIO;
      file_detail::error("write failed");
    }
    bytes = bytes.subspan(static_cast<size_t>(count));
    if (offset) *offset += static_cast<uint64_t>(count);
  }
}
void sync_file(int fd, bool data_only) {
  int result;
  do {
    result = data_only ? ::fdatasync(fd) : ::fsync(fd);
  } while (result && errno == EINTR);
  if (result) file_detail::error(data_only ? "fdatasync failed" : "fsync failed");
}
}  // namespace sic::support

namespace sic::support {
void sync_file(const std::filesystem::path& path) {
  file_detail::Descriptor file(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
  if (file.fd < 0) file_detail::error("cannot open for sync " + path.string());
  sync_file(file.fd);
  file.close();
}

void sync_parent(const std::filesystem::path& file) {
  const auto parent = file.parent_path().empty() ? std::filesystem::path(".") : file.parent_path();
  file_detail::Descriptor directory(::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if (directory.fd < 0) file_detail::error("cannot open directory " + parent.string());
  sync_file(directory.fd);
  directory.close();
}
}  // namespace sic::support

namespace sic::support {
void publish_replace(const std::filesystem::path& from, const std::filesystem::path& to) {
  if (::rename(from.c_str(), to.c_str())) file_detail::error("rename failed for " + to.string());
}

void publish_no_replace(const std::filesystem::path& from, const std::filesystem::path& to) {
#if defined(__linux__) && defined(SYS_renameat2)
  if (::syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0)
    return;
  if (errno != ENOSYS && errno != EINVAL && errno != EOPNOTSUPP)
    file_detail::error("cannot publish " + to.string());
#endif
  if (::link(from.c_str(), to.c_str())) file_detail::error("cannot publish " + to.string());
  if (::unlink(from.c_str())) file_detail::error("cannot remove private output " + from.string());
}
}  // namespace sic::support

namespace sic::support {
namespace {
void write_atomic(const std::filesystem::path& path, const std::function<void(int)>& write,
                  bool replace, mode_t mode, bool data_only) {
  static std::atomic<uint64_t> serial{0};
  std::filesystem::path temporary;
  int fd;
  do {
    temporary = path.string() + ".tmp-" + std::to_string(::getpid()) + "-" +
                std::to_string(serial.fetch_add(1, std::memory_order_relaxed));
    fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
  } while (fd < 0 && errno == EEXIST);
  if (fd < 0) file_detail::error("cannot create " + temporary.string());
  struct Cleanup {
    const std::filesystem::path& path;
    ~Cleanup() { ::unlink(path.c_str()); }
  } cleanup{temporary};
  file_detail::Descriptor output(fd);
  try {
    write(output.fd);
  } catch (const sic::Error& error) {
    if (error.kind() != ErrorKind::io) throw;
    throw sic::Error(error.kind(), temporary.string() + ": " + error.what());
  }
  sync_file(output.fd, data_only);
  output.close();
  if (replace) {
    publish_replace(temporary, path);
  } else {
    if (::link(temporary.c_str(), path.c_str()))
      file_detail::error("cannot publish " + path.string());
    if (::unlink(temporary.c_str())) file_detail::error("cannot unlink " + temporary.string());
  }
  sync_parent(path);
}
class OutputBuffer : public std::streambuf {
  int fd_;
  char buffer_[8192];
  int sync() override {
    write_all(fd_, std::span(reinterpret_cast<const uint8_t*>(pbase()), pptr() - pbase()));
    setp(buffer_, buffer_ + sizeof buffer_);
    return 0;
  }
  int_type overflow(int_type value) override {
    sync();
    if (!traits_type::eq_int_type(value, traits_type::eof())) {
      *pptr() = traits_type::to_char_type(value);
      pbump(1);
    }
    return traits_type::not_eof(value);
  }
  std::streamsize xsputn(const char* data, std::streamsize count) override {
    if (count <= 0) return 0;
    if (count > epptr() - pptr()) sync();
    if (static_cast<uint64_t>(count) > sizeof buffer_) {
      write_all(fd_, std::span(reinterpret_cast<const uint8_t*>(data), static_cast<size_t>(count)));
    } else {
      std::memcpy(pptr(), data, static_cast<size_t>(count));
      pbump(static_cast<int>(count));
    }
    return count;
  }

 public:
  explicit OutputBuffer(int fd) : fd_(fd) { setp(buffer_, buffer_ + sizeof buffer_); }
};
}  // namespace

void atomic_write(const std::filesystem::path& path, std::span<const uint8_t> bytes, bool replace,
                  mode_t mode, bool data_only) {
  write_atomic(
      path, [&](int fd) { write_all(fd, bytes); }, replace, mode, data_only);
}

void atomic_write_stream(const std::filesystem::path& path,
                         const std::function<void(std::ostream&)>& write, bool replace, mode_t mode,
                         bool data_only) {
  write_atomic(
      path,
      [&](int fd) {
        OutputBuffer buffer(fd);
        std::ostream stream(&buffer);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        try {
          write(stream);
          stream.flush();
        } catch (const std::ios_base::failure& error) {
          if (!stream.fail()) throw;
          throw sic::Error(ErrorKind::io, error.what());
        }
      },
      replace, mode, data_only);
}
}  // namespace sic::support

namespace sic::support {
void atomic_write(const std::filesystem::path& path, std::string_view text, bool replace,
                  mode_t mode, bool data_only) {
  atomic_write(path, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()), replace,
               mode, data_only);
}
}  // namespace sic::support
