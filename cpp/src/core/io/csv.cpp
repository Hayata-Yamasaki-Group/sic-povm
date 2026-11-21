// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/core/interchange.hpp"
#include "sic/support/parse_number.hpp"

namespace sic::core::interchange {
Vector read_csv(const std::string& path) {
  const ByteBuffer raw = read_file(path);
  const std::string text(raw.begin(), raw.end());
  std::istringstream in(text);
  std::string line;
  if (!std::getline(in, line)) throw sic::Error(ErrorKind::invalid_format, path + ": empty CSV");
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line != "index,real,imag")
    throw sic::Error(ErrorKind::invalid_format, path + ": expected header 'index,real,imag'");
  Vector v;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    const std::size_t c1 = line.find(','), c2 = line.find(',', c1 + 1);
    if (c1 == std::string::npos || c2 == std::string::npos)
      throw sic::Error(ErrorKind::invalid_format, path + ": bad row");
    const auto row = std::string_view(line);
    const auto idx = support::parse_number<std::size_t>(row.substr(0, c1), path + ": CSV index",
                                                        ErrorKind::invalid_format);
    const auto re = support::parse_number<double>(row.substr(c1 + 1, c2 - c1 - 1),
                                                  path + ": CSV real", ErrorKind::invalid_format);
    const char* e = line.data() + line.size();
    while (e > line.data() && (e[-1] == '\r' || e[-1] == ' ')) --e;
    const auto im = support::parse_number<double>(
        row.substr(c2 + 1, static_cast<std::size_t>(e - line.data()) - c2 - 1),
        path + ": CSV imaginary", ErrorKind::invalid_format);
    if (idx != v.size())
      throw sic::Error(ErrorKind::invalid_format, path + ": malformed row '" + line + "'");
    v.emplace_back(re, im);
  }
  if (v.empty()) throw sic::Error(ErrorKind::invalid_format, path + ": no data rows");
  return v;
}
}  // namespace sic::core::interchange
