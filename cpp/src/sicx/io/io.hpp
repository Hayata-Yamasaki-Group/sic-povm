// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <filesystem>
#include <istream>

#include "sic/sicx/format.hpp"
namespace sic::sicx {
// Translate only native filesystem failures; categorized and standard arithmetic
// or allocation failures retain their original type.
template <class Operation>
decltype(auto) filesystem_io(Operation&& operation) {
  try {
    return operation();
  } catch (const std::filesystem::filesystem_error& error) {
    throw Error(ErrorKind::io, error.what());
  }
}
void rename_file(const std::filesystem::path& from, const std::filesystem::path& to,
                 std::string_view operation);
void sync_fd(int fd, const char* what);
void sync_dir(const std::filesystem::path& file);
void pwrite_all(int fd, const uint8_t* data, size_t size, uint64_t offset);
void pread_exact(int fd, uint8_t* data, size_t size, uint64_t offset);
#ifdef SIC_ENABLE_TEST_HOOKS
extern thread_local void (*io_fault_hook)(std::string_view, const std::filesystem::path&);
uint64_t env_u64(const char* name);
#endif
std::string read_bounded_text(const std::filesystem::path& path, uint64_t limit = kMaxTextChunk);
bool bounded_line(std::istream& in, std::string& text, uint64_t limit);
}  // namespace sic::sicx
