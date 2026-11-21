// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Dependency-free writers/readers for the interchange formats of a fiducial vector:
//   * NumPy .npy v1.0 (dtype '<c16', shape (d,), C order),
//   * NumPy .npz (ZIP of .npy members; own deterministic store-only ZIP writer/reader, CRC-32),
//   * MATLAB Level-5 .mat (uncompressed, little-endian) with the subset of data elements
//     used by the numeric writers (real/complex double matrices, char row vectors),
//   * CSV "index,real,imag" (17 significant digits, as released by the project),
//   * SHA-256 (FIPS 180-4) for the SHA256SUMS files.
// Codecs use no FLINT and assume a little-endian IEEE-754 host.

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "sic/core/types.hpp"
#include "sic/error.hpp"
#include "sic/support/admission.hpp"

namespace sic::core::interchange {

static_assert(std::endian::native == std::endian::little,
              "interchange formats assume a little-endian host");
static_assert(sizeof(double) == 8, "binary64 required");

using ByteBuffer = std::vector<unsigned char>;

// Fails closed on NaN/Inf: every comparison with NaN is false, so downstream checks must never see
// one.
void require_finite(const Vector& v, const std::string& what,
                    ErrorKind kind = ErrorKind::invalid_argument);

// UTF-8 <-> UTF-16 for MATLAB char arrays (stored as UTF-16 code units). Invalid input throws.
ByteBuffer read_file(const std::string& path);

void write_file(const std::string& path, const void* data, std::size_t n);
void write_file(const std::string& path, const ByteBuffer& b);
void write_file(const std::string& path, const std::string& s);

// ------------------------------------------------------------------ CSV
// "index,real,imag" with a header row. Values use strict locale-independent parsing (exact round
// trip of the 17-significant-digit decimals written by the numeric writers, round-to-nearest).
Vector read_csv(const std::string& path);

// ------------------------------------------------------------------ NPY
// Format version 1.0: magic, 2 version bytes, uint16-LE header length, ASCII dict padded with
// spaces and terminated by '\n' so that the data start is a multiple of 64 bytes (NumPy's own
// choice; the format document requires a multiple of 16, which this satisfies).
ByteBuffer npy_encode(const Vector& psi);

Vector npy_decode(const ByteBuffer& in, const std::string& what = "npy");

// ------------------------------------------------------------------ NPZ (deterministic ZIP, store
// only) A standard NumPy .npz is a ZIP archive of .npy members. This writer is byte-deterministic:
// members are stored (method 0, no compression), in the order given, with DOS time/date fixed at
// 1980-01-01 00:00:00, no extra fields, no per-entry comments, no data descriptors, no ZIP64 (sizes
// and offsets must fit in 32 bits, and there are at most 65535 members). NumPy ignores the archive
// comment (<= 65535 bytes). Our reader requires the deterministic writer profile's leading JSON
// field {"schema":"sic-npz-bundle/1.0", but does not validate the remaining comment metadata.
struct NpzRay {
  std::string id;      // full ray id, e.g. d1024-f5-c12 (letters, digits, '.', '_', '-')
  Vector psi;          // binary64 coordinates
  std::string source;  // SHA-256 (hex) of the source ray JSON or decoder FP64 file
  std::string exceptional_group{};  // empty for WH rays
};

std::string npz_comment(std::size_t d, const std::vector<NpzRay>& sorted);

ByteBuffer npz_encode(std::size_t d, std::vector<NpzRay> rays);

struct NpzArray {
  std::string id;
  Vector values;
};

struct NpzContents {
  std::vector<NpzArray> arrays;  // ray id (without ".npy"), in file order
  std::string comment;
};

NpzContents npz_decode(const ByteBuffer& in, const std::string& what = "npz");

// ------------------------------------------------------------------ MAT (Level 5)
// Accumulates named variables and writes a .mat file readable by MATLAB, Octave, SciPy loadmat.
class MatFile {
 public:
  explicit MatFile(const std::string& description);
  void add_complex_column(const std::string& name, const Vector& v);
  void add_scalar(const std::string& name, double x);
  void add_complex_scalar(const std::string& name, Complex z);
  // UTF-8 input; stored as UTF-16 code units, MATLAB's native char storage (invalid UTF-8 throws).
  void add_string(const std::string& name, const std::string& s);
  const ByteBuffer& bytes() const { return bytes_; }

 private:
  ByteBuffer bytes_;
};

// Shared release MAT layout. An empty anchor omits both anchor variables (e.g. Hoggar).
MatFile fiducial_mat(const Vector& psi, const std::string& id, const std::string& rounding,
                     const std::string& error_bound, const std::string& source_sha256,
                     const std::string& anchor = "");

struct MatVariable {
  std::string name;
  std::uint32_t cls = 0;
  std::uint32_t rows = 0, cols = 0;
  bool complex = false;
  std::vector<double> re, im;  // doubles (class double)
  std::string text;            // char arrays
};

// Reads back files written by MatFile (uncompressed, little endian, full-form elements, 2-D).
std::vector<MatVariable> mat_decode(const ByteBuffer& in, const std::string& what = "mat");

const MatVariable& mat_find(const std::vector<MatVariable>& vars, const std::string& name);

Vector mat_psi(const std::vector<MatVariable>& vars);

// Bit-exact comparison of two vectors (distinguishes -0 from +0, as the CSV does).
bool bit_equal(const Vector& a, const Vector& b);

}  // namespace sic::core::interchange
