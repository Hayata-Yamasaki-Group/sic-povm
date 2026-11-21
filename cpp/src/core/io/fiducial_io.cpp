// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/core/fiducial_io.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace sic::core {

namespace {
// Collects all non-comment tokens.
std::istringstream strip_comments(std::istream& in, const InputLimits& limits) {
  std::string body;
  bool comment = false;
  uint64_t consumed = 0;
  try {
    for (char c; in.get(c);) {
      if (consumed == limits.maximum_bytes)
        throw sic::Error(ErrorKind::resource, "read_vector: input byte policy exceeded");
      ++consumed;
      if (c == '\n') {
        body += c;
        comment = false;
      } else if (c == '#')
        comment = true;
      else if (!comment)
        body += c;
    }
  } catch (const sic::Error&) {
    throw;
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::overflow_error&) {
    throw;
  } catch (...) {
    if (in.bad() || !in.eof()) throw sic::Error(ErrorKind::io, "read_vector: stream read failed");
  }
  if (in.bad() || (in.fail() && !in.eof()))
    throw sic::Error(ErrorKind::io, "read_vector: stream read failed");
  return std::istringstream(body);
}
}  // namespace

Vector read_vector(std::istream& in) { return read_vector(in, InputLimits{}); }
Vector read_vector(std::istream& in, const InputLimits& limits) {
  auto tokens = strip_comments(in, limits);
  long long d = 0;
  if (!(tokens >> d) || d < 1)
    throw sic::Error(ErrorKind::invalid_format, "read_vector: missing or invalid dimension");
  if (static_cast<uint64_t>(d) > limits.maximum_dimension ||
      static_cast<uint64_t>(d) > limits.maximum_bytes / sizeof(Complex))
    throw sic::Error(ErrorKind::resource, "read_vector: dimension/allocation policy exceeded");
  Vector v;
  // Grow only as validated entries arrive, so a truncated declaration allocates no huge vector.
  for (long long k = 0; k < d; ++k) {
    Real re = 0, im = 0;
    auto component = [&](const char* part) {
      std::string token;
      if (!(tokens >> token))
        throw sic::Error(ErrorKind::invalid_format, "read_vector: missing " + std::string(part) +
                                                        " token at entry " + std::to_string(k));
      std::istringstream number(token);
      Real value = 0;
      if (!(number >> value) || !number.eof() || !std::isfinite(value))
        throw sic::Error(ErrorKind::invalid_format,
                         "read_vector: " + std::string(part) + " token '" + token + "' at entry " +
                             std::to_string(k) + " is not a finite representable number");
      return value;
    };
    re = component("real");
    im = component("imaginary");
    v.emplace_back(re, im);
  }
  std::string extra;
  if (tokens >> extra)
    throw sic::Error(ErrorKind::invalid_format, "read_vector: trailing data: " + extra);
  return v;
}

Vector read_vector_file(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw sic::Error(ErrorKind::io, "cannot open " + path);
  return read_vector(f);
}

void write_vector(std::ostream& out, const Vector& v, int precision) {
  interchange::require_finite(v, "write_vector");
  try {
    out << v.size() << '\n' << std::setprecision(precision);
    for (const Complex& z : v) out << z.real() << ' ' << z.imag() << '\n';
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::overflow_error&) {
    throw;
  } catch (...) {
    if (!out) throw sic::Error(ErrorKind::io, "write_vector: stream write failed");
    throw;
  }
  if (!out) throw sic::Error(ErrorKind::io, "write_vector: stream write failed");
}

}  // namespace sic::core

namespace sic::core::files {
bool ends_with(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
Vector read_fiducial(const std::string& path) {
  if (ends_with(path, ".csv")) return interchange::read_csv(path);
  if (ends_with(path, ".npy")) return interchange::npy_decode(interchange::read_file(path), path);
  return read_vector_file(path);
}
}  // namespace sic::core::files
