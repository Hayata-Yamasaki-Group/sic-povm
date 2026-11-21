// SPDX-License-Identifier: AGPL-3.0-or-later
// CTest hoggar_data: CHECKS the committed Hoggar data files (data/d8/hoggar); it generates nothing.
//   usage: sic-test-data-hoggar-data DATA_HOGGAR_DIR
// 1. exact.json coordinates (Gaussian integers) -> exact norm and all 63 non-identity overlaps =
// 1/9 and the 64 lines
//    pairwise equiangular (exact integer arithmetic, sic/data/hoggar.hpp), plus negative controls.
// 2. c0.json fp64 / fp32 / hp are consistent with the exact coordinates (correct rounding; exact
// decimal containment).
// 3. c0.csv, c0.npy and c0.mat carry the same binary64 values as c0.json fp64 (bit equal); c0.h5
// has the HDF5 signature and, with HDF5 enabled, the schema attribute.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "sic/data/hoggar.hpp"

#ifdef SIC_HAVE_HDF5
#include <hdf5.h>
#endif

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << what << "\n";
  }
}
std::string slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    ++failures;
    std::cerr << "FAIL: cannot read " << path << "\n";
    return {};
  }
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
// Quoted strings of the array that follows the chain of keys, e.g. {"fp64","values"}.
std::vector<std::string> array_strings(const std::string& t, const std::vector<std::string>& keys) {
  std::size_t pos = 0;
  for (const auto& k : keys) {
    pos = t.find("\"" + k + "\":", pos);
    if (pos == std::string::npos) return {};
    pos += k.size() + 3;
  }
  std::vector<std::string> out;
  int depth = 0;
  for (; pos < t.size(); ++pos) {
    if (t[pos] == '[')
      ++depth;
    else if (t[pos] == ']') {
      if (--depth == 0) break;
    } else if (t[pos] == '"' && depth > 0) {
      const std::size_t e = t.find('"', pos + 1);
      out.push_back(t.substr(pos + 1, e - pos - 1));
      pos = e;
    }
  }
  return out;
}
double f64_of_hex(const std::string& h) {
  std::uint64_t u = std::stoull(h, nullptr, 16);
  double x;
  std::memcpy(&x, &u, 8);
  return x;
}
float f32_of_hex(const std::string& h) {
  std::uint32_t u = static_cast<std::uint32_t>(std::stoul(h, nullptr, 16));
  float x;
  std::memcpy(&x, &u, 4);
  return x;
}

template <class T>
bool correctly_rounded(T x, long double exact) {  // |x - exact| <= distance to either neighbour
  const long double e = std::fabs(static_cast<long double>(x) - exact);
  const long double lo =
      std::fabs(static_cast<long double>(std::nextafter(x, static_cast<T>(-INFINITY))) - exact);
  const long double hi =
      std::fabs(static_cast<long double>(std::nextafter(x, static_cast<T>(INFINITY))) - exact);
  return e <= lo && e <= hi;
}
}  // namespace

int main(int argc, char** argv) {
  using namespace sic::data::hoggar;
  if (argc != 2) {
    std::cerr << "usage: sic-test-data-hoggar-data DIR\n";
    return 2;
  }
  const std::string dir = std::string(argv[1]) + "/";

  // ---- 1. exact check of the committed exact.json
  const std::string ex = slurp(dir + "exact.json");
  check(ex.find("\"schema\":\"sic-hoggar-exact-json/1.0\"") != std::string::npos,
        "exact.json schema identity");
  const auto cs = array_strings(ex, {"coordinates_unnormalized"});
  check(cs.size() == 16, "exact.json has 8 Gaussian integers");
  for (const auto& c : cs)
    check(c.find('/') == std::string::npos,
          "exact.json coordinates are Gaussian integers (no fractions) in this file");
  Vec8 v{};
  if (cs.size() == 16)
    for (int j = 0; j < 8; ++j) v[j] = {std::stoll(cs[2 * j]), std::stoll(cs[2 * j + 1])};
  check(ex.find("\"radicand\":\"12\"") != std::string::npos &&
            ex.find("\"scalar\":\"1/sqrt(12)\"") != std::string::npos,
        "exact.json states the 1/sqrt(12) normalization");
  const Report r = verify_exact(v);
  check(r.ok, "exact verification: " + r.message);
  check(r.norm_squared == 12 && r.overlaps_checked == 63 && r.pairs_checked == 2016,
        "exact verification counts");
  std::cout << "exact: <v|v> = " << r.norm_squared << ", overlaps checked " << r.overlaps_checked
            << ", pairs checked " << r.pairs_checked << "\n";
  {
    Vec8 bad = v;
    bad[0] = {1, 2};
    check(!verify_exact(bad).ok, "negative control (1+2i,1,...) must fail");
  }
  {
    Vec8 bad = v;
    bad[7] = {1, 1};
    check(!verify_exact(bad).ok, "negative control (perturbed) must fail");
  }
  // convention sanity: P_{1,0} (a = 001 flips the least significant tensor factor) swaps
  // coordinates x and x^1.
  {
    const Vec8 w = pauli_apply(1, 0, v);
    check(w[0] == v[1] && w[1] == v[0], "P_{a=001,b=0} is the shift on the last tensor factor");
  }
  {
    const Vec8 w = pauli_apply(0, 4, v);
    check(w[3] == v[3] && w[4] == Gauss{-v[4].re, -v[4].im},
          "Z on tensor factor 1 acts on the most significant bit");
  }

  // ---- 2. c0.json against the exact coordinates
  const std::string js = slurp(dir + "c0.json");
  check(js.find("\"schema\":\"sic-hoggar-fiducial-json/1.0\"") != std::string::npos,
        "c0.json schema identity");
  const long double s = 1.0L / std::sqrt(12.0L);
  const auto exact_re = [&](int j) { return j == 0 ? -s : s; };
  const auto exact_im = [&](int j) { return j == 0 ? 2 * s : 0.0L; };
  const auto f64 = array_strings(js, {"fp64", "values"});
  const auto f32 = array_strings(js, {"fp32", "values"});
  check(f64.size() == 16 && f32.size() == 16, "c0.json fp64/fp32 have 8 coordinates");
  std::vector<double> bits;
  for (int j = 0; j < 8 && f64.size() == 16 && f32.size() == 16; ++j) {
    const double a = f64_of_hex(f64[2 * j]), b = f64_of_hex(f64[2 * j + 1]);
    check(correctly_rounded(a, exact_re(j)) && std::fabs(a - exact_re(j)) < 0x1p-54L,
          "fp64 re correct rounding, j=" + std::to_string(j));
    check(j == 0 ? correctly_rounded(b, exact_im(j)) && std::fabs(b - exact_im(j)) < 0x1p-54L
                 : f64[2 * j + 1] == "0000000000000000",
          "fp64 im, j=" + std::to_string(j));
    bits.push_back(a);
    bits.push_back(b);
    const float fa = f32_of_hex(f32[2 * j]), fb = f32_of_hex(f32[2 * j + 1]);
    check(correctly_rounded(fa, exact_re(j)) && std::fabs(fa - exact_re(j)) < 0x1p-25L,
          "fp32 re correct rounding, j=" + std::to_string(j));
    check(j == 0 ? correctly_rounded(fb, exact_im(j)) && std::fabs(fb - exact_im(j)) < 0x1p-25L
                 : f32[2 * j + 1] == "00000000",
          "fp32 im, j=" + std::to_string(j));
  }
  // hp: every nonzero coordinate encloses the exact value (decided exactly); exactly-zero parts
  // have radius 0.
  const auto hp = array_strings(js, {"hp", "values"});
  check(hp.size() == 32, "c0.json hp has 8 coordinates");
  for (int j = 0; j < 8 && hp.size() == 32; ++j) {
    const auto mag = [](std::string m) { return m[0] == '-' ? m.substr(1) : m; };
    const std::string zero = "0." + std::string(80, '0');
    check(hp[4 * j][0] == (j == 0 ? '-' : '0'), "hp re sign, j=" + std::to_string(j));
    check(hp_contains_sqrt_ratio(mag(hp[4 * j]), hp[4 * j + 1], 1, 12),
          "hp re contains exact value, j=" + std::to_string(j));
    if (j == 0)
      check(hp_contains_sqrt_ratio(mag(hp[2]), hp[3], 1, 3), "hp im(0) contains 2/sqrt(12)");
    else
      check(hp[4 * j + 2] == zero && hp[4 * j + 3] == zero,
            "hp im exactly zero with radius 0, j=" + std::to_string(j));
    for (int k : {1, 3}) {
      const std::string& rad = hp[4 * j + k];
      check(
          rad.size() == 82 && rad.compare(0, 2, "0.") == 0 &&
              rad.compare(2, 77, std::string(77, '0')) == 0,
          "hp radius < 2^-253, j=" + std::to_string(j));  // < 1e-77 < 2^-253*... (2^-253 = 8.6e-77)
    }
  }

  // ---- 3. binary files carry the same binary64 values
  const auto csv = slurp(dir + "c0.csv");
  {
    std::istringstream in(csv);
    std::string line;
    std::getline(in, line);
    check(line == "index,real,imag", "csv header");
    for (int j = 0; j < 8 && bits.size() == 16; ++j) {
      std::getline(in, line);
      const std::size_t c1 = line.find(','), c2 = line.find(',', c1 + 1);
      const double re = std::strtod(line.substr(c1 + 1, c2 - c1 - 1).c_str(), nullptr),
                   im = std::strtod(line.substr(c2 + 1).c_str(), nullptr);
      check(std::memcmp(&re, &bits[2 * j], 8) == 0 && std::memcmp(&im, &bits[2 * j + 1], 8) == 0,
            "csv row " + std::to_string(j) + " bit-equal to fp64");
    }
  }
  const auto npy = slurp(dir + "c0.npy");
  check(npy.size() > 128 && npy.compare(1, 5, "NUMPY") == 0 &&
            npy.find("'<c16'") != std::string::npos && npy.find("(8,)") != std::string::npos,
        "npy header");
  if (bits.size() == 16 && npy.size() >= 128 + 128) {
    const std::size_t off =
        10 + (static_cast<unsigned char>(npy[8]) | (static_cast<unsigned char>(npy[9]) << 8));
    check(npy.size() == off + 128 && std::memcmp(npy.data() + off, bits.data(), 128) == 0,
          "npy data bit-equal to fp64 (interleaved re,im)");
  }
  const auto mat = slurp(dir + "c0.mat");
  if (bits.size() == 16) {
    double re[8], im[8];
    for (int j = 0; j < 8; ++j) {
      re[j] = bits[2 * j];
      im[j] = bits[2 * j + 1];
    }
    check(mat.find(std::string(reinterpret_cast<char*>(re), 64)) != std::string::npos &&
              mat.find(std::string(reinterpret_cast<char*>(im), 64)) != std::string::npos,
          "mat psi columns bit-equal to fp64");
    check(mat.compare(0, 15, "MATLAB 5.0 MAT-") == 0, "mat header");
  }
  const auto h5 = slurp(dir + "c0.h5");
  check(h5.compare(0, 8, std::string("\x89HDF\r\n\x1a\n", 8)) == 0, "h5 signature");

#ifdef SIC_HAVE_HDF5
  const hid_t hf = H5Fopen((dir + "c0.h5").c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
  const hid_t hd = hf >= 0 ? H5Dopen2(hf, "psi", H5P_DEFAULT) : -1;
  const hid_t ha = hd >= 0 ? H5Aopen(hd, "schema", H5P_DEFAULT) : -1;
  const hid_t ht = ha >= 0 ? H5Aget_type(ha) : -1;
  const hid_t hs = ha >= 0 ? H5Aget_space(ha) : -1;
  char* value = nullptr;
  const bool valid = hs >= 0 && H5Sget_simple_extent_type(hs) == H5S_SCALAR && ht >= 0 &&
                     H5Tget_class(ht) == H5T_STRING && H5Tis_variable_str(ht) > 0 &&
                     H5Aread(ha, ht, &value) >= 0 && value &&
                     std::string(value) == "sic-hoggar-fiducial-json/1.0";
  check(valid, "c0.h5 psi schema attribute");
  if (value) H5free_memory(value);
  if (hs >= 0) H5Sclose(hs);
  if (ht >= 0) H5Tclose(ht);
  if (ha >= 0) H5Aclose(ha);
  if (hd >= 0) H5Dclose(hd);
  if (hf >= 0) H5Fclose(hf);
#endif

  // ---- manifest
  const std::string mf = slurp(dir + "manifest.json");
  for (const char* want :
       {"\"schema\":\"sic-hoggar-manifest/1.0\"", "\"group\":\"3-qubit Pauli group\"",
        "\"id\":\"d8-hoggar-c0\"", "\"counted_in_catalogue\":false",
        "\"provenance\":\"added directly as data"})
    check(mf.find(want) != std::string::npos, std::string("manifest.json contains ") + want);

  if (failures) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "hoggar_data: all checks passed\n";
  return 0;
}
