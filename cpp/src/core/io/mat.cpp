// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/io/mat.hpp"

#include "sic/support/parse_number.hpp"

namespace sic::core::interchange {
std::vector<std::uint16_t> utf8_to_utf16(const std::string& s) {
  std::vector<std::uint16_t> u;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::uint32_t cp = 0;
    std::size_t n = 0;
    if (c < 0x80) {
      cp = c;
      n = 1;
    } else if (c >= 0xC2 && c < 0xE0) {
      cp = c & 0x1F;
      n = 2;
    } else if (c >= 0xE0 && c < 0xF0) {
      cp = c & 0x0F;
      n = 3;
    } else if (c >= 0xF0 && c < 0xF5) {
      cp = c & 0x07;
      n = 4;
    } else
      throw sic::Error(ErrorKind::invalid_argument, "invalid UTF-8 in string");
    if (i + n > s.size())
      throw sic::Error(ErrorKind::invalid_argument, "truncated UTF-8 in string");
    for (std::size_t k = 1; k < n; ++k) {
      const unsigned char cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80)
        throw sic::Error(ErrorKind::invalid_argument, "invalid UTF-8 continuation byte");
      cp = (cp << 6) | (cc & 0x3F);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
        (cp >= 0xD800 && cp < 0xE000))
      throw sic::Error(ErrorKind::invalid_argument, "invalid UTF-8 code point");
    if (cp >= 0x10000) {
      cp -= 0x10000;
      u.push_back(static_cast<std::uint16_t>(0xD800 + (cp >> 10)));
      u.push_back(static_cast<std::uint16_t>(0xDC00 + (cp & 0x3FF)));
    } else
      u.push_back(static_cast<std::uint16_t>(cp));
    i += n;
  }
  return u;
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
void append_utf8(std::string& out, std::uint32_t cp) {
  if (cp < 0x80)
    out += static_cast<char>(cp);
  else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
std::string utf16_to_utf8(const std::vector<std::uint16_t>& u) {
  std::string out;
  for (std::size_t i = 0; i < u.size(); ++i) {
    std::uint32_t cp = u[i];
    if (cp >= 0xD800 && cp < 0xDC00) {
      if (i + 1 >= u.size() || u[i + 1] < 0xDC00 || u[i + 1] >= 0xE000)
        throw sic::Error(ErrorKind::invalid_format, "unpaired UTF-16 surrogate");
      cp = 0x10000 + ((cp - 0xD800) << 10) + (u[++i] - 0xDC00);
    } else if (cp >= 0xDC00 && cp < 0xE000)
      throw sic::Error(ErrorKind::invalid_format, "unpaired UTF-16 surrogate");
    append_utf8(out, cp);
  }
  return out;
}
}  // namespace sic::core::interchange

namespace sic::core::interchange::mat5 {
uint64_t padded_element(uint64_t n) {
  if (n > kMaximumElementBytes)
    throw sic::Error(ErrorKind::unsupported, "mat: element exceeds Level-5 wire size; use HDF5");
  return 8 + (n + 7) / 8 * 8;
}
}  // namespace sic::core::interchange::mat5

namespace sic::core::interchange::mat5 {
void admit_matrix(uint64_t rows, uint64_t cols, uint64_t name_bytes, uint64_t bytes, bool complex) {
  if (rows > kMaximumDimension || cols > kMaximumDimension)
    throw sic::Error(ErrorKind::unsupported,
                     "mat: dimension exceeds signed INT32 profile; use HDF5");
  const uint64_t body = 32 + padded_element(name_bytes) + padded_element(bytes) * (complex ? 2 : 1);
  if (body > kMaximumElementBytes)
    throw sic::Error(ErrorKind::unsupported, "mat: matrix exceeds Level-5 wire size; use HDF5");
}
}  // namespace sic::core::interchange::mat5

namespace sic::core::interchange::mat5 {
void put32(ByteBuffer& b, std::uint32_t x) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(x >> (8 * i)));
}
}  // namespace sic::core::interchange::mat5

namespace sic::core::interchange::mat5 {
void pad8(ByteBuffer& b) {
  while (b.size() % 8) b.push_back(0);
}
}  // namespace sic::core::interchange::mat5

namespace sic::core::interchange::mat5 {
void element(ByteBuffer& b, std::uint32_t type, const void* data, std::size_t n) {
  (void)padded_element(n);
  put32(b, type);
  put32(b, static_cast<std::uint32_t>(n));
  const auto* p = static_cast<const unsigned char*>(data);
  b.insert(b.end(), p, p + n);
  pad8(b);
}
}  // namespace sic::core::interchange::mat5

namespace sic::core::interchange::mat5 {
void matrix(ByteBuffer& out, const std::string& name, std::uint32_t cls, bool complex,
            uint64_t rows, uint64_t cols, std::uint32_t dtype, const void* re, const void* im,
            std::size_t bytes) {
  admit_matrix(rows, cols, name.size(), bytes, complex);
  ByteBuffer body;
  const std::uint32_t flags[2] = {cls | (complex ? 0x0800u : 0u), 0};
  element(body, mi_uint32, flags, 8);
  const std::int32_t dims[2] = {static_cast<std::int32_t>(rows), static_cast<std::int32_t>(cols)};
  element(body, mi_int32, dims, 8);
  element(body, mi_int8, name.data(), name.size());
  element(body, dtype, re, bytes);
  if (complex) element(body, dtype, im, bytes);
  put32(out, mi_matrix);
  put32(out, static_cast<std::uint32_t>(body.size()));
  out.insert(out.end(), body.begin(), body.end());
}
}  // namespace sic::core::interchange::mat5

namespace sic::core::interchange {
MatFile::MatFile(const std::string& description) {
  std::string text = "MATLAB 5.0 MAT-file, " + description;
  text.resize(116, ' ');
  bytes_.insert(bytes_.end(), text.begin(), text.end());
  bytes_.insert(bytes_.end(), 8, 0);  // subsystem data offset
  bytes_.push_back(0);
  bytes_.push_back(1);  // version 0x0100 (little endian)
  bytes_.push_back('I');
  bytes_.push_back('M');  // endian indicator
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
void MatFile::add_complex_column(const std::string& name, const Vector& v) {
  require_finite(v, "mat column");
  mat5::admit_matrix(v.size(), 1, name.size(), support::admission::mul(v.size(), 8), true);
  std::vector<double> re(v.size()), im(v.size());
  for (std::size_t j = 0; j < v.size(); ++j) {
    re[j] = v[j].real();
    im[j] = v[j].imag();
  }
  mat5::matrix(bytes_, name, mat5::mx_double_class, true, v.size(), 1, mat5::mi_double, re.data(),
               im.data(), 8 * v.size());
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
void MatFile::add_scalar(const std::string& name, double x) {
  require_finite({x}, "mat scalar");
  mat5::matrix(bytes_, name, mat5::mx_double_class, false, 1, 1, mat5::mi_double, &x, nullptr, 8);
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
void MatFile::add_complex_scalar(const std::string& name, Complex z) {
  require_finite({z}, "mat complex scalar");
  const double re = z.real(), im = z.imag();
  mat5::matrix(bytes_, name, mat5::mx_double_class, true, 1, 1, mat5::mi_double, &re, &im, 8);
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
void MatFile::add_string(const std::string& name, const std::string& s) {
  const std::vector<std::uint16_t> u = utf8_to_utf16(s);
  mat5::matrix(bytes_, name, mat5::mx_char_class, false, u.empty() ? 0 : 1, u.size(),
               mat5::mi_uint16, u.data(), nullptr, 2 * u.size());
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
MatFile fiducial_mat(const Vector& psi, const std::string& id, const std::string& rounding,
                     const std::string& error_bound, const std::string& source_sha256,
                     const std::string& anchor) {
  MatFile m("SIC-POVM fiducial " + id + ", certified binary64 values (sic-data)");
  m.add_complex_column("psi", psi);
  m.add_scalar("d", static_cast<double>(psi.size()));
  m.add_string("wh_convention",
               "D_(a,b)=tau^(a*b) X^a Z^b; tau=-exp(pi*i/d); X|j>=|j+1>; Z|j>=exp(2*pi*i*j/d)|j>");
  m.add_string("tau_phase", "tau = -exp(i*pi/d); tau^2 = omega = exp(2*pi*i/d)");
  if (!anchor.empty()) {
    const auto index = support::parse_number<std::uint64_t>(
        anchor, "anchor", ErrorKind::invalid_argument, 0, UINT64_MAX - 1);
    m.add_scalar("anchor", static_cast<double>(index));
    m.add_scalar("anchor_index_1based", static_cast<double>(index + 1));
  }
  m.add_string("ray_id", id);
  m.add_string("rounding", rounding);
  m.add_string("error_bound", error_bound);
  m.add_string("source_sha256", source_sha256);
  return m;
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
std::vector<MatVariable> mat_decode(const ByteBuffer& in, const std::string& what) {
  auto fail = [&](const char* m) { throw sic::Error(ErrorKind::invalid_format, what + ": " + m); };
  if (in.size() < 128 || std::memcmp(in.data(), "MATLAB 5.0 MAT-file", 19) != 0) fail("bad header");
  if (in[126] != 'I' || in[127] != 'M' || in[124] != 0 || in[125] != 1)
    fail("not a little-endian v5 file");
  auto u32 = [&](std::size_t at) {
    if (at + 4 > in.size()) fail("truncated");
    std::uint32_t x;
    std::memcpy(&x, in.data() + at, 4);
    return x;
  };
  std::vector<MatVariable> vars;
  std::size_t pos = 128;
  while (pos < in.size()) {
    if (u32(pos) != mat5::mi_matrix) fail("top-level element is not a matrix");
    const std::size_t len = u32(pos + 4), end = pos + 8 + len;
    if (end > in.size()) fail("truncated matrix");
    std::size_t p = pos + 8;
    struct SubElement {
      std::size_t offset;
      std::size_t length;
    };
    auto sub = [&](std::uint32_t expect) -> SubElement {
      if (u32(p) != expect) fail("unexpected sub-element type");
      const std::size_t n = u32(p + 4), at = p + 8;
      if (at > end || n > end - at) fail("sub-element exceeds its matrix");
      p = at + (n + 7) / 8 * 8;
      return {at, n};
    };
    MatVariable v;
    const auto [fa, fn] = sub(mat5::mi_uint32);
    if (fn != 8) fail("bad flags");
    const std::uint32_t flags = u32(fa);
    v.cls = flags & 255;
    v.complex = flags & 0x0800;
    const auto [da, dn] = sub(mat5::mi_int32);
    if (dn != 8) fail("only 2-D arrays are supported");
    v.rows = u32(da);
    v.cols = u32(da + 4);
    const auto [na, nn] = sub(mat5::mi_int8);
    v.name.assign(in.begin() + static_cast<std::ptrdiff_t>(na),
                  in.begin() + static_cast<std::ptrdiff_t>(na + nn));
    if (v.cls == mat5::mx_double_class) {
      const std::size_t count = std::size_t(v.rows) * v.cols;
      if (count > in.size() / 8) fail("implausible array size");
      for (int part = 0; part < (v.complex ? 2 : 1); ++part) {
        const auto [a, n] = sub(mat5::mi_double);
        if (n != 8 * count) fail("bad data size");
        auto& dst = part == 0 ? v.re : v.im;
        dst.resize(count);
        std::memcpy(dst.data(), in.data() + a, n);
      }
    } else if (v.cls == mat5::mx_char_class) {
      const auto [a, n] = sub(mat5::mi_uint16);
      std::vector<std::uint16_t> u(n / 2);
      for (std::size_t k = 0; k < u.size(); ++k)
        u[k] = static_cast<std::uint16_t>(in[a + 2 * k] | (in[a + 2 * k + 1] << 8));
      v.text = utf16_to_utf8(u);
    } else {
      fail("unsupported class");
    }
    if (p != end) fail("trailing bytes in matrix");
    vars.push_back(std::move(v));
    pos = end;
  }
  return vars;
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
const MatVariable& mat_find(const std::vector<MatVariable>& vars, const std::string& name) {
  for (const auto& v : vars)
    if (v.name == name) return v;
  throw sic::Error(ErrorKind::invalid_format, "mat: variable '" + name + "' not found");
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
Vector mat_psi(const std::vector<MatVariable>& vars) {
  const auto& v = mat_find(vars, "psi");
  if (!v.complex || v.cols != 1)
    throw sic::Error(ErrorKind::invalid_format, "mat: psi is not a complex column vector");
  Vector out(v.rows);
  for (std::size_t j = 0; j < out.size(); ++j) out[j] = {v.re[j], v.im[j]};
  require_finite(out, "mat psi", ErrorKind::invalid_format);
  return out;
}
}  // namespace sic::core::interchange
