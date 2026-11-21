// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/core/interchange.hpp"
#include "sic/support/durable_file.hpp"

namespace sic::core::interchange {
void require_finite(const Vector& v, const std::string& what, ErrorKind kind) {
  for (std::size_t j = 0; j < v.size(); ++j)
    if (!std::isfinite(v[j].real()) || !std::isfinite(v[j].imag()))
      throw sic::Error(kind,
                       what + ": non-finite value (NaN or Inf) in coordinate " + std::to_string(j));
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
ByteBuffer read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw sic::Error(ErrorKind::io, "cannot open " + path);
  ByteBuffer bytes;
  char buffer[8192];
  while (in.read(buffer, sizeof(buffer)) || in.gcount()) {
    bytes.insert(bytes.end(), buffer, buffer + in.gcount());
  }
  if (in.bad() || (in.fail() && !in.eof()))
    throw sic::Error(ErrorKind::io, "read failed for " + path);
  return bytes;
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
void write_file(const std::string& path, const void* data, std::size_t n) try {
  // Interchange is a general writer: callers may replace an existing export.
  const auto status = std::filesystem::symlink_status(path);
  if (std::filesystem::exists(status) && !std::filesystem::is_regular_file(status))
    throw sic::Error(ErrorKind::io, "write failed for " + path);
  support::atomic_write(path, std::span(static_cast<const uint8_t*>(data), n), true, 0666);
} catch (const sic::Error& error) {
  if (error.kind() != ErrorKind::io) throw;
  const bool creation =
      std::string_view(error.what()).find("cannot create ") != std::string_view::npos;
  throw sic::Error(ErrorKind::io, (creation ? "cannot create " : "write failed for ") + path);
} catch (const std::filesystem::filesystem_error&) {
  throw sic::Error(ErrorKind::io, "cannot create " + path);
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
void write_file(const std::string& path, const ByteBuffer& b) {
  write_file(path, b.data(), b.size());
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
void write_file(const std::string& path, const std::string& s) {
  write_file(path, s.data(), s.size());
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
bool bit_equal(const Vector& a, const Vector& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t j = 0; j < a.size(); ++j) {
    const double x[4] = {a[j].real(), a[j].imag(), b[j].real(), b[j].imag()};
    if (std::memcmp(&x[0], &x[2], 8) != 0 || std::memcmp(&x[1], &x[3], 8) != 0) return false;
  }
  return true;
}
}  // namespace sic::core::interchange
