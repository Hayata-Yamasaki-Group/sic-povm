// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/io/io.hpp"

#include <unistd.h>

#include <array>
#include <cstring>
#include <fstream>

#include "sic/support/durable_file.hpp"
#include "sic/support/parse_number.hpp"
#include "support/io/write_all.hpp"
namespace sic::sicx {
#ifdef SIC_ENABLE_TEST_HOOKS
thread_local void (*io_fault_hook)(std::string_view, const std::filesystem::path&) = nullptr;
#endif

void rename_file(const std::filesystem::path& from, const std::filesystem::path& to,
                 std::string_view operation) {
  filesystem_io([&] {
#ifdef SIC_ENABLE_TEST_HOOKS
    if (io_fault_hook) io_fault_hook(operation, to);
#else
    (void)operation;
#endif
    std::filesystem::rename(from, to);
  });
}

void sync_fd(int fd, const char* what) {
  try {
    support::sync_file(fd, true);
  } catch (const sic::Error& error) {
    throw sic::Error(ErrorKind::io, std::string(what) + ": " + error.what());
  }
}

void sync_dir(const std::filesystem::path& file) {
  filesystem_io([&] {
#ifdef SIC_ENABLE_TEST_HOOKS
    if (io_fault_hook) io_fault_hook("parent_sync", file);
#endif
    support::sync_parent(file);
  });
}

void pwrite_all(int fd, const uint8_t* data, size_t size, uint64_t offset) {
  support::write_all(fd, {data, size}, offset);
}

void pread_exact(int fd, uint8_t* data, size_t size, uint64_t offset) {
#ifdef SIC_ENABLE_TEST_HOOKS
  if (io_fault_hook) io_fault_hook("read", {});
#endif
  while (size) {
    const ssize_t n = ::pread(fd, data, size, static_cast<off_t>(offset));
    if (n < 0) {
      if (errno == EINTR) continue;
      throw sic::Error(ErrorKind::io, "read failed");
    }
    if (n == 0) throw sic::Error(ErrorKind::invalid_format, "unexpected end of file");
    data += n;
    size -= static_cast<size_t>(n);
    offset += static_cast<uint64_t>(n);
  }
}

#ifdef SIC_ENABLE_TEST_HOOKS
uint64_t env_u64(const char* name) {
  const char* v = std::getenv(name);
  return v && *v ? support::parse_number<uint64_t>(v, name, ErrorKind::invalid_argument) : 0;
}

#endif

std::string read_bounded_text(const std::filesystem::path& path, uint64_t limit) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) throw sic::Error(ErrorKind::io, "cannot stat " + path.string());
  if (size > limit) throw sic::Error(ErrorKind::resource, "text input resource refusal");
  std::ifstream in(path, std::ios::binary);
  if (!in) throw sic::Error(ErrorKind::io, "cannot read " + path.string());
  std::string out;
  std::array<char, 65536> buffer{};
  while (in) {
    in.read(buffer.data(), buffer.size());
    const auto n = static_cast<size_t>(in.gcount());
    if (n > limit - out.size())
      throw sic::Error(ErrorKind::resource, "text input grew beyond resource limit");
    out.append(buffer.data(), n);
  }
  if (in.bad() || !in.eof()) throw sic::Error(ErrorKind::io, "text read failed");
  return out;
}

bool bounded_line(std::istream& in, std::string& text, uint64_t limit) {
  text.clear();
  std::array<char, 65536> buffer{};
  for (;;) {
    in.get(buffer.data(), buffer.size(), '\n');
    const auto n = static_cast<size_t>(in.gcount());
    if (n > limit - text.size())
      throw sic::Error(ErrorKind::resource, "text line resource refusal");
    text.append(buffer.data(), n);
    if (in.bad()) throw sic::Error(ErrorKind::io, "text read failed");
    if (in.eof()) {
      if (!text.empty()) fail(sic::ErrorKind::invalid_format, "text line without terminating LF");
      return false;
    }
    if (in.fail()) in.clear(in.rdstate() & ~std::ios::failbit);
    if (in.peek() == '\n') {
      in.get();
      return true;
    }
  }
}

}  // namespace sic::sicx
