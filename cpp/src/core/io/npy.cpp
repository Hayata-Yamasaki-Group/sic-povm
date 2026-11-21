// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/core/interchange.hpp"
#include "sic/support/parse_number.hpp"

namespace sic::core::interchange {
ByteBuffer npy_encode(const Vector& psi) {
  require_finite(psi, "npy vector");
  std::string dict =
      "{'descr': '<c16', 'fortran_order': False, 'shape': (" + std::to_string(psi.size()) + ",), }";
  const std::size_t unpadded = 10 + dict.size() + 1;
  const std::size_t pad = (64 - unpadded % 64) % 64;
  dict.append(pad, ' ');
  dict.push_back('\n');
  ByteBuffer out = {0x93,
                    'N',
                    'U',
                    'M',
                    'P',
                    'Y',
                    1,
                    0,
                    static_cast<unsigned char>(dict.size() & 255),
                    static_cast<unsigned char>(dict.size() >> 8)};
  out.insert(out.end(), dict.begin(), dict.end());
  const std::size_t head = out.size();
  out.resize(head + 16 * psi.size());
  for (std::size_t j = 0; j < psi.size(); ++j) {
    const double c[2] = {psi[j].real(), psi[j].imag()};
    std::memcpy(out.data() + head + 16 * j, c, 16);
  }
  return out;
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
Vector npy_decode(const ByteBuffer& in, const std::string& what) {
  auto fail = [&](const char* m) { throw sic::Error(ErrorKind::invalid_format, what + ": " + m); };
  if (in.size() < 10 || std::memcmp(in.data(), "\x93NUMPY", 6) != 0) fail("bad magic");
  if (in[7] != 0 || in[6] < 1 || in[6] > 3) fail("unsupported format version");
  const std::size_t prefix = in[6] == 1 ? 10 : 12;
  if (in.size() < prefix) fail("truncated header");
  std::size_t hl = 0;
  for (std::size_t j = 8; j < prefix; ++j) hl |= std::size_t(in[j]) << (8 * (j - 8));
  if (hl == 0 || hl > in.size() - prefix || (prefix + hl) % 16 != 0)
    fail("bad header length/alignment");
  const std::string dict(in.begin() + prefix, in.begin() + prefix + hl);
  if (dict.back() != '\n') fail("header not newline-terminated");
  std::size_t at = 0, n = 0;
  auto ws = [&] {
    while (at < dict.size() && std::isspace(static_cast<unsigned char>(dict[at]))) ++at;
  };
  auto expect = [&](char c) {
    ws();
    if (at == dict.size() || dict[at++] != c) fail("malformed header dictionary");
  };
  auto quoted = [&] {
    ws();
    if (at == dict.size() || (dict[at] != '\'' && dict[at] != '"'))
      fail("expected quoted header string");
    const char quote = dict[at++];
    const auto start = at;
    while (at < dict.size() && dict[at] != quote) ++at;
    if (at == dict.size()) fail("unterminated header string");
    return dict.substr(start, at++ - start);
  };
  expect('{');
  std::set<std::string> keys;
  for (;;) {
    ws();
    if (at < dict.size() && dict[at] == '}') {
      ++at;
      break;
    }
    const auto key = quoted();
    if (!keys.insert(key).second) fail("duplicate header key");
    expect(':');
    if (key == "descr") {
      if (quoted() != "<c16") fail("dtype is not '<c16'");
    } else if (key == "fortran_order") {
      ws();
      if (dict.compare(at, 5, "False") != 0) fail("fortran_order must be False");
      at += 5;
    } else if (key == "shape") {
      expect('(');
      ws();
      const auto start = at;
      while (at < dict.size() && dict[at] >= '0' && dict[at] <= '9') ++at;
      n = support::parse_number<std::size_t>(std::string_view(dict).substr(start, at - start),
                                             what + ": NPY dimension", ErrorKind::invalid_format);
      expect(',');
      expect(')');
    } else
      fail("unknown header key");
    ws();
    if (at < dict.size() && dict[at] == '}') {
      ++at;
      break;
    }
    expect(',');
  }
  ws();
  if (at != dict.size() || keys.size() != 3) fail("trailing or missing header fields");
  if (n == 0 || n > (in.size() - prefix - hl) / 16)
    fail("empty vector or data shorter than the shape");
  if (in.size() != prefix + hl + 16 * n) fail("data length does not match shape");
  Vector v(n);
  for (std::size_t j = 0; j < n; ++j) {
    double c[2];
    std::memcpy(c, in.data() + prefix + hl + 16 * j, 16);
    v[j] = {c[0], c[1]};
  }
  require_finite(v, what, ErrorKind::invalid_format);
  return v;
}
}  // namespace sic::core::interchange
