// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/byte_source.hpp"

#include <limits>

namespace sic::support {
void ByteSource::plan(std::span<const ByteRange> ranges) const {
  for (auto r : ranges) check(r);
}
}  // namespace sic::support

namespace sic::support {
void ByteSource::check(ByteRange r) const {
  if (r.offset > size() || r.size > size() - r.offset)
    throw sic::Error(sic::ErrorKind::io, "byte source: read beyond the end of the file");
}
}  // namespace sic::support

namespace sic::support {
LocalByteSource::LocalByteSource(const std::filesystem::path& path) {
  const auto description = path.string();
  fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd_ < 0) throw sic::Error(sic::ErrorKind::io, "cannot open " + description);
  initialize_size(description);
}

LocalByteSource::LocalByteSource(int fd) {
  const std::string description = "descriptor";
  fd_ = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
  if (fd_ < 0) throw sic::Error(sic::ErrorKind::io, "cannot duplicate local source descriptor");
  initialize_size(description);
}

void LocalByteSource::initialize_size(const std::string& description) {
  struct stat st {};
  if (::fstat(fd_, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
      static_cast<uint64_t>(st.st_size) > (uint64_t(1) << 62)) {
    ::close(fd_);
    fd_ = -1;
    throw sic::Error(sic::ErrorKind::io, "byte source: invalid regular file " + description);
  }
  size_ = static_cast<uint64_t>(st.st_size);
}
}  // namespace sic::support

namespace sic::support {
LocalByteSource::~LocalByteSource() {
  if (fd_ >= 0) ::close(fd_);
}
}  // namespace sic::support

namespace sic::support {
void LocalByteSource::read(uint64_t offset, std::span<uint8_t> output) const {
  check({offset, output.size()});
  while (!output.empty()) {
    const size_t want =
        std::min(output.size(), static_cast<size_t>(std::numeric_limits<ssize_t>::max()));
    const ssize_t n = ::pread(fd_, output.data(), want, static_cast<off_t>(offset));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0)
      throw sic::Error(n < 0 ? sic::ErrorKind::io : sic::ErrorKind::invalid_format,
                       "byte source: short or failed local read");
    offset += static_cast<uint64_t>(n);
    output = output.subspan(static_cast<size_t>(n));
  }
}
}  // namespace sic::support

namespace sic::support {
std::shared_ptr<ByteSource> local_byte_source(const std::filesystem::path& path) {
  return std::make_shared<LocalByteSource>(path);
}
}  // namespace sic::support
