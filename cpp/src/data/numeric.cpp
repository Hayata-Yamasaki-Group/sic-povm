#include <flint/acb.h>
#include <flint/arb.h>
#include <flint/flint.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/io/npz.hpp"
#include "data/commands.hpp"
#include "sic/bounds/overlap_bounds.hpp"
#include "sic/core/hdf5.hpp"
#include "sic/data/layout.hpp"
#include "sic/sicx/format.hpp"
#include "sic/sicx/io.hpp"
#include "sic/support/cli.hpp"
#include "sic/support/durable_file.hpp"
#include "sic/support/parse_number.hpp"
#include "support/sha256.hpp"

namespace filesystem = std::filesystem;

namespace interchange = sic::core::interchange;
namespace sic::data {
namespace {
static_assert(std::endian::native == std::endian::little, "little-endian host required");
using ByteBuffer = std::vector<unsigned char>;
using Pair = std::array<uint64_t, 2>;  // binary64 bit patterns (re, im)

[[noreturn]] void die(sic::ErrorKind kind, const std::string& m) { throw sic::Error(kind, m); }

ByteBuffer read_bytes(const filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) die(sic::ErrorKind::io, "cannot open " + p.string());
  try {
    ByteBuffer bytes(std::istreambuf_iterator<char>(in), {});
    if (in.bad()) die(ErrorKind::io, "cannot read " + p.string());
    return bytes;
  } catch (const std::ios_base::failure& error) {
    die(ErrorKind::io, error.what());
  }
}
void write_new(const filesystem::path& p, const ByteBuffer& b) {
  if (filesystem::exists(filesystem::symlink_status(p)))
    die(sic::ErrorKind::io, "output exists: " + p.string());
  try {
    support::atomic_write(p, std::span<const uint8_t>(b), false, 0666);
  } catch (const sic::Error& error) {
    if (error.kind() != ErrorKind::io) throw;
    die(ErrorKind::io, "write failed: " + p.string());
  }
}
ByteBuffer to_bytes(const std::string& s) { return ByteBuffer(s.begin(), s.end()); }

uint64_t parse_hex64(const std::string& h) {
  if (h.size() != 16 || h.find_first_not_of("0123456789abcdef") != std::string::npos)
    die(sic::ErrorKind::invalid_format, "bad binary64 hex " + h);
  return support::parse_number<uint64_t>(h, "binary64 hex", ErrorKind::invalid_format, 0,
                                         UINT64_MAX, 16);
}
double as_double(uint64_t b) { return std::bit_cast<double>(b); }
uint64_t as_bits(double x) { return std::bit_cast<uint64_t>(x); }
std::string dec17(uint64_t b) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.16e", as_double(b));
  return buf;
}

// ------------------------------------------------------------------ certified ray file
struct Ray {
  std::string path, id, schema, sha256, anchor, error_bound, rounding;
  uint64_t d = 0;
  std::vector<Pair> v;
};

Ray load_ray(const filesystem::path& path) {
  std::string text;
  {
    const ByteBuffer b = read_bytes(path);
    text.assign(b.begin(), b.end());
  }
  while (!text.empty() && text.back() == '\n') text.pop_back();
  const sic::support::Json j = sic::support::JsonParser::parse(text);
  Ray r;
  r.path = path.string();
  r.schema = j.at("schema").as_str();
  if (r.schema != "sic-fiducial-json/1.0" && r.schema != "sic-hoggar-fiducial-json/1.0")
    die(sic::ErrorKind::unsupported, r.path + ": unsupported schema " + r.schema);
  r.id = j.at("id").as_str();
  r.d = support::parse_number<uint64_t>(j.at("d").as_str(), "ray dimension",
                                        ErrorKind::invalid_format);
  r.sha256 = sic::support::sha256::file_hex(path);
  if (j.at("labels").has("anchor")) r.anchor = j.at("labels").at("anchor").as_str();
  if (!r.anchor.empty())
    support::parse_number<uint64_t>(r.anchor, "ray anchor", ErrorKind::invalid_format, 0,
                                    UINT64_MAX - 1);
  const auto& f = j.at("fp64");
  r.error_bound = f.at("error_bound").as_str();
  r.rounding = f.at("rounding").as_str();
  const auto& values = f.at("values").as_array();
  const auto& decimal = f.at("decimal").as_array();
  if (values.size() != r.d || decimal.size() != r.d)
    die(sic::ErrorKind::invalid_format, r.path + ": fp64 has not d values");
  for (size_t k = 0; k < r.d; ++k) {
    const auto& x = values[k].as_array();
    const auto& t = decimal[k].as_array();
    if (x.size() != 2 || t.size() != 2)
      die(sic::ErrorKind::invalid_format, r.path + ": fp64 entry is not a pair");
    Pair p{parse_hex64(x[0].as_str()), parse_hex64(x[1].as_str())};
    for (int part = 0; part < 2; ++part) {
      if (dec17(p[part]) != t[part].as_str())
        die(sic::ErrorKind::invalid_format,
            r.path + ": fp64 decimal " + t[part].as_str() + " is not the %.16e form of its bits");
      if (!std::isfinite(as_double(p[part])))
        die(sic::ErrorKind::invalid_format, r.path + ": non-finite fp64 value");
    }
    r.v.push_back(p);
  }
  return r;
}

// The core owns every numerical writer. Checks below retain their separate readers.
sic::core::Vector vector_of(const std::vector<Pair>& v) {
  sic::core::Vector out;
  for (const auto& x : v) out.emplace_back(as_double(x[0]), as_double(x[1]));
  return out;
}
std::vector<Pair> pairs_of(const sic::core::Vector& v) {
  std::vector<Pair> out;
  for (const auto& x : v) out.push_back({as_bits(x.real()), as_bits(x.imag())});
  return out;
}
std::string csv_encode(const std::vector<Pair>& v) {
  std::string s = "index,real,imag\n";
  for (size_t j = 0; j < v.size(); ++j)
    s += std::to_string(j) + "," + dec17(v[j][0]) + "," + dec17(v[j][1]) + "\n";
  return s;
}

// ------------------------------------------------------------------ readers (checks)
uint32_t get16(const ByteBuffer& b, size_t at) {
  if (at + 2 > b.size()) die(sic::ErrorKind::invalid_format, "truncated");
  return b[at] | (uint32_t(b[at + 1]) << 8);
}
uint32_t get32(const ByteBuffer& b, size_t at) { return get16(b, at) | (get16(b, at + 2) << 16); }
uint64_t get64(const ByteBuffer& b, size_t at) {
  return get32(b, at) | (uint64_t(get32(b, at + 4)) << 32);
}

std::vector<Pair> read_npy_bytes(const ByteBuffer& b, const std::string& what) {
  if (b.size() < 10 || std::memcmp(b.data(), "\x93NUMPY\x01\x00", 8) != 0)
    die(sic::ErrorKind::invalid_format, what + ": not an NPY 1.0 file");
  const size_t hl = get16(b, 8);
  if ((10 + hl) % 16 || b.size() < 10 + hl)
    die(sic::ErrorKind::invalid_format, what + ": bad NPY header length");
  const std::string dict(b.begin() + 10, b.begin() + 10 + static_cast<long>(hl));
  const std::string want = "{'descr': '<c16', 'fortran_order': False, 'shape': (";
  if (dict.compare(0, want.size(), want) != 0 || dict.back() != '\n')
    die(sic::ErrorKind::invalid_format, what + ": NPY header is not '<c16' C order");
  const auto end = dict.find(',', want.size());
  const size_t n = support::parse_number<size_t>(
      std::string_view(dict).substr(want.size(), end - want.size()), "NPY dimension",
      ErrorKind::invalid_format, 0, (SIZE_MAX - 10 - hl) / 16);
  if (dict.compare(want.size() + std::to_string(n).size(), 4, ",), ") != 0)
    die(sic::ErrorKind::invalid_format, what + ": NPY shape is not (d,)");
  if (b.size() != 10 + hl + 16 * n)
    die(sic::ErrorKind::invalid_format, what + ": NPY data size does not match the shape");
  std::vector<Pair> v(n);
  for (size_t j = 0; j < n; ++j)
    v[j] = {get64(b, 10 + hl + 16 * j), get64(b, 10 + hl + 16 * j + 8)};
  return v;
}

std::vector<Pair> read_csv(const filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) die(ErrorKind::io, "cannot open " + p.string());
  std::string line;
  if (!std::getline(in, line) || line != "index,real,imag")
    die(in.bad() ? ErrorKind::io : ErrorKind::invalid_format,
        p.string() + ": CSV header is not index,real,imag");
  std::vector<Pair> v;
  while (std::getline(in, line)) {
    std::stringstream s(line);
    std::string a, b, c, extra;
    if (!std::getline(s, a, ',') || !std::getline(s, b, ',') || !std::getline(s, c, ',') ||
        std::getline(s, extra, ','))
      die(sic::ErrorKind::invalid_format, p.string() + ": bad CSV row " + line);
    if (a != std::to_string(v.size()))
      die(sic::ErrorKind::invalid_format, p.string() + ": CSV index out of order");
    const double re =
        support::parse_number<double>(b, p.string() + ": CSV real", ErrorKind::invalid_format);
    const double im =
        support::parse_number<double>(c, p.string() + ": CSV imaginary", ErrorKind::invalid_format);
    // Strict form: the field must be the "%.16e" text of the value it parses to (no nan, inf, hex,
    // spaces).
    if (dec17(as_bits(re)) != b || dec17(as_bits(im)) != c)
      die(sic::ErrorKind::invalid_format,
          p.string() + ": CSV field is not in the %.16e form: " + line);
    v.push_back({as_bits(re), as_bits(im)});
  }
  if (in.bad()) die(ErrorKind::io, "cannot read " + p.string());
  return v;
}

// Level-5 MAT reader: walks the top-level elements and returns variable "psi" (complex double
// column).
std::vector<Pair> read_mat(const filesystem::path& p) {
  const ByteBuffer b = read_bytes(p);
  if (b.size() < 128 || std::memcmp(b.data(), "MATLAB 5.0 MAT-file", 19) != 0 || b[126] != 'I' ||
      b[127] != 'M')
    die(sic::ErrorKind::invalid_format, p.string() + ": not a little-endian Level-5 MAT file");
  auto sub = [&](size_t& at, size_t end, uint32_t& type, size_t& off, size_t& n) {
    const uint32_t tag = get32(b, at);
    if (tag >> 16) {  // small data element
      type = tag & 0xFFFF;
      n = tag >> 16;
      off = at + 4;
      at += 8;
    } else {
      type = tag;
      n = get32(b, at + 4);
      off = at + 8;
      at = off + ((n + 7) / 8) * 8;
    }
    if (off + n > end || at > end)
      die(sic::ErrorKind::invalid_format, p.string() + ": MAT element overruns");
  };
  size_t at = 128;
  while (at < b.size()) {
    const uint32_t type = get32(b, at);
    const size_t n = get32(b, at + 4), start = at + 8, end = start + n;
    if (end > b.size()) die(ErrorKind::invalid_format, p.string() + ": truncated MAT element");
    if (type != 14)
      die(ErrorKind::unsupported,
          p.string() + ": unsupported MAT element (compressed files are not read)");
    at = end;
    size_t q = start, off = 0, len = 0;
    uint32_t t = 0;
    sub(q, end, t, off, len);
    if (t != 6 || len != 8) die(sic::ErrorKind::invalid_format, p.string() + ": MAT array flags");
    const uint32_t flags = get32(b, off);
    sub(q, end, t, off, len);
    if (t != 5 || len != 8) die(sic::ErrorKind::invalid_format, p.string() + ": MAT dims");
    const uint32_t rows = get32(b, off), cols = get32(b, off + 4);
    sub(q, end, t, off, len);
    const std::string name(b.begin() + static_cast<long>(off),
                           b.begin() + static_cast<long>(off + len));
    if (name != "psi") continue;
    if ((flags & 0xFF) != 6 || !(flags & 0x0800) || cols != 1)
      die(sic::ErrorKind::invalid_format, p.string() + ": psi is not a complex double column");
    size_t ro = 0, rl = 0, io = 0, il = 0;
    sub(q, end, t, ro, rl);
    if (t != 9 || rl != 8ull * rows)
      die(sic::ErrorKind::invalid_format, p.string() + ": psi real part is not miDOUBLE[d]");
    sub(q, end, t, io, il);
    if (t != 9 || il != 8ull * rows)
      die(sic::ErrorKind::invalid_format, p.string() + ": psi imaginary part is not miDOUBLE[d]");
    std::vector<Pair> v(rows);
    for (size_t j = 0; j < rows; ++j) v[j] = {get64(b, ro + 8 * j), get64(b, io + 8 * j)};
    return v;
  }
  die(sic::ErrorKind::invalid_format, p.string() + ": no variable psi");
}

std::vector<Pair> read_fp64_txt(const filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) die(ErrorKind::io, "cannot open " + p.string());
  std::vector<Pair> v;
  std::vector<std::string> t;
  for (std::string x; in >> x;) t.push_back(x);
  if (in.bad()) die(ErrorKind::io, "cannot read " + p.string());
  if (t.size() % 2)
    die(sic::ErrorKind::invalid_format, p.string() + ": odd number of tokens in fp64 text");
  for (size_t i = 0; i < t.size(); i += 2) v.push_back({parse_hex64(t[i]), parse_hex64(t[i + 1])});
  return v;
}

std::map<std::string, ByteBuffer> read_zip(const ByteBuffer& b, std::string& comment,
                                           const std::string& what) {
  if (b.size() < 22) die(sic::ErrorKind::invalid_format, what + ": not a ZIP file");
  size_t eocd = std::string::npos;
  for (size_t back = 22; back <= std::min<size_t>(b.size(), 22 + 65535); ++back) {
    const size_t at = b.size() - back;
    if (get32(b, at) == 0x06054b50 && at + 22 + get16(b, at + 20) == b.size()) {
      eocd = at;
      break;
    }
  }
  if (eocd == std::string::npos)
    die(sic::ErrorKind::invalid_format, what + ": no end of central directory");
  comment.assign(b.begin() + static_cast<long>(eocd + 22), b.end());
  const size_t count = get16(b, eocd + 10);
  size_t cp = get32(b, eocd + 16);
  std::map<std::string, ByteBuffer> out;
  for (size_t k = 0; k < count; ++k) {
    if (get32(b, cp) != 0x02014b50) die(ErrorKind::invalid_format, what + ": bad central entry");
    if (get16(b, cp + 10) != 0)
      die(ErrorKind::unsupported, what + ": unsupported central entry (only stored members)");
    const uint32_t crc = get32(b, cp + 16), size = get32(b, cp + 20);
    const size_t nlen = get16(b, cp + 28), xlen = get16(b, cp + 30), clen = get16(b, cp + 32),
                 loff = get32(b, cp + 42);
    if (cp + 46 + nlen > b.size())
      die(sic::ErrorKind::invalid_format, what + ": central entry name overruns");
    if (get32(b, cp + 24) != size)
      die(ErrorKind::invalid_format, what + ": stored member sizes differ");
    if (get16(b, cp + 8) & 8) die(ErrorKind::unsupported, what + ": unsupported data descriptor");
    const std::string name(b.begin() + static_cast<long>(cp + 46),
                           b.begin() + static_cast<long>(cp + 46 + nlen));
    if (get32(b, loff) != 0x04034b50 || get16(b, loff + 8) != 0)
      die(sic::ErrorKind::invalid_format, what + ": bad local header " + name);
    const size_t data = loff + 30 + get16(b, loff + 26) + get16(b, loff + 28);
    if (data + size > b.size())
      die(sic::ErrorKind::invalid_format, what + ": member overruns " + name);
    ByteBuffer m(b.begin() + static_cast<long>(data), b.begin() + static_cast<long>(data + size));
    if (interchange::crc32(m.data(), m.size()) != crc)
      die(sic::ErrorKind::integrity, what + ": CRC mismatch in " + name);
    if (!out.emplace(name, std::move(m)).second)
      die(sic::ErrorKind::invalid_format, what + ": duplicate member " + name);
    cp += 46 + nlen + xlen + clen;
  }
  return out;
}

std::vector<Pair> read_any(const filesystem::path& p) {
  const std::string n = p.filename().string();
  auto ends = [&](const std::string& s) {
    return n.size() >= s.size() && n.compare(n.size() - s.size(), s.size(), s) == 0;
  };
  if (ends(".npy")) return read_npy_bytes(read_bytes(p), p.string());
  if (ends(".csv")) return read_csv(p);
  if (ends(".mat")) return read_mat(p);
  if (ends(".fp64.txt")) return read_fp64_txt(p);
  if (ends(".h5")) {
#ifdef SIC_HAVE_HDF5
    return pairs_of(interchange::read_h5(p.string()));
#else
    die(sic::ErrorKind::unsupported, "HDF5 input requires a build with libhdf5");
#endif
  }
  die(sic::ErrorKind::invalid_format, p.string() + ": unknown numerical format");
}

size_t compare(const Ray& r, const std::vector<Pair>& v, const std::string& what) {
  if (v.size() != r.v.size())
    die(sic::ErrorKind::integrity, what + ": " + std::to_string(v.size()) +
                                       " values, ray file has " + std::to_string(r.v.size()));
  size_t bad = 0;
  for (size_t j = 0; j < v.size(); ++j)
    if (v[j] != r.v[j]) {
      if (++bad <= 3)
        std::cerr << what << ": coordinate " << j << " differs from " << r.path << '\n';
    }
  return bad;
}

std::set<std::string> split_list(const std::string& s) {
  std::set<std::string> out;
  std::stringstream in(s);
  for (std::string x; std::getline(in, x, ',');) out.insert(x);
  return out;
}

std::string stem_of(const filesystem::path& p) {
  std::string n = p.filename().string();
  if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0) n.resize(n.size() - 5);
  return n;
}

int numeric_write(const sic::support::cli::Arguments& args) {
  args.positionals(2, 2);
  const std::set<std::string> formats = split_list(args.get("--formats", kDefaultNumericFormats));
  sic::support::cli::require(!formats.empty(), "--formats selects no format");
  for (const auto& f : formats)
    sic::support::cli::require(f == "npy" || f == "mat" || f == "csv" || f == "h5",
                               "unknown format " + f);
  if (formats.contains("h5") && !interchange::have_h5())
    die(sic::ErrorKind::unsupported, "HDF5 output requires a build with libhdf5");
  const Ray r = load_ray(args.pos[0]);
  const filesystem::path out = args.pos[1];
  const std::string stem = stem_of(args.pos[0]);
  for (const auto& f : formats)
    if (filesystem::exists(filesystem::symlink_status(out / (stem + "." + f))))
      die(sic::ErrorKind::io, "output exists");
  filesystem::create_directories(out);
  for (const auto& f : formats) {
    const filesystem::path p = out / (stem + "." + f);
    if (f == "npy")
      write_new(p, interchange::npy_encode(vector_of(r.v)));
    else if (f == "csv")
      write_new(p, to_bytes(csv_encode(r.v)));
    else if (f == "mat") {
      const auto m = interchange::fiducial_mat(vector_of(r.v), r.id, r.rounding, r.error_bound,
                                               r.sha256, r.anchor);
      write_new(p, m.bytes());
    } else if (f == "h5") {
#ifdef SIC_HAVE_HDF5
      if (filesystem::exists(filesystem::symlink_status(p)))
        die(sic::ErrorKind::io, "output exists: " + p.string());
      interchange::write_h5(p.string(), vector_of(r.v),
                            {{"ray_id", r.id},
                             {"rounding", r.rounding},
                             {"error_bound", r.error_bound},
                             {"source_sha256", r.sha256}},
                            {{"d", static_cast<double>(r.d)}});
#else
      die(sic::ErrorKind::unsupported, "HDF5 output requires a build with libhdf5");
#endif
    } else
      die(sic::ErrorKind::invalid_argument, "unknown format " + f);
    std::cout << "WROTE " << p.filename().string() << '\n';
  }
  return 0;
}

int numeric_check(const sic::support::cli::Arguments& args) {
  args.positionals(2, SIZE_MAX);
  const Ray r = load_ray(args.pos[0]);
  size_t bad = 0;
  for (size_t i = 1; i < args.pos.size(); ++i) {
    const size_t b = compare(r, read_any(args.pos[i]), args.pos[i]);
    std::cout << (b ? "DIFFERS " : "IDENTICAL ")
              << filesystem::path(args.pos[i]).filename().string() << " (" << r.d
              << " binary64 pairs vs " << r.id << ")\n";
    bad += b;
  }
  if (bad) {
    std::cout << "NUMERIC-CHECK FAIL\n";
    return 1;
  }
  std::cout << "NUMERIC-CHECK PASS " << r.id << " files=" << args.pos.size() - 1 << '\n';
  return 0;
}

struct Member {
  Ray ray;
  std::string group;  // non-empty: exceptional (not Weyl-Heisenberg covariant)
};

std::vector<Member> members_of(const sic::support::cli::Arguments& args, size_t first,
                               bool groups) {
  for (const auto& values : args.all("--exceptional"))
    sic::support::cli::require(
        values.size() == (groups ? 2u : 1u),
        "--exceptional needs RAY.json" + std::string(groups ? " GROUP" : ""));
  std::vector<Member> m;
  for (size_t i = first; i < args.pos.size(); ++i) m.push_back({load_ray(args.pos[i]), ""});
  for (const auto& values : args.all("--exceptional"))
    m.push_back({load_ray(values[0]), groups ? values[1] : "exceptional"});
  std::sort(m.begin(), m.end(),
            [](const Member& a, const Member& b) { return a.ray.id < b.ray.id; });
  for (size_t i = 0; i < m.size(); ++i) {
    const auto& id = m[i].ray.id;
    if (id.empty() || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
          return std::isalnum(c) || c == '-' || c == '_' || c == '.';
        }))
      die(sic::ErrorKind::invalid_format, "unsafe ray id " + id);
    if (i && m[i - 1].ray.id == id) die(sic::ErrorKind::invalid_argument, "duplicate ray id " + id);
  }
  return m;
}

int npz(const sic::support::cli::Arguments& args) {
  args.positionals(2, SIZE_MAX);
  sic::support::cli::require(args.pos.size() > 2 || args.has("--exceptional"),
                             "expected at least one ray");
  const uint64_t d = sic::support::cli::number<uint64_t>(args.pos[1], "D", 1);
  const auto m = members_of(args, 2, true);
  std::vector<interchange::NpzRay> rays;
  size_t wh = 0;
  for (const auto& x : m) {
    if (x.ray.d != d)
      die(sic::ErrorKind::integrity, x.ray.path + ": wrong dimension for this bundle");
    rays.push_back({x.ray.id, vector_of(x.ray.v), x.ray.sha256, x.group});
    if (x.group.empty()) ++wh;
  }
  write_new(args.pos[0], interchange::npz_encode(d, rays));
  std::cout << "WROTE " << filesystem::path(args.pos[0]).filename().string()
            << " members=" << rays.size() << " wh_rays=" << wh << '\n';
  return 0;
}

int npz_check(const sic::support::cli::Arguments& args) {
  args.positionals(1, SIZE_MAX);
  sic::support::cli::require(args.pos.size() > 1 || args.has("--exceptional"),
                             "expected at least one ray");
  std::string comment;
  const auto zip = read_zip(read_bytes(args.pos[0]), comment, args.pos[0]);
  interchange::require_npz_comment_identity(comment, args.pos[0]);
  const auto m = members_of(args, 1, false);
  size_t bad = 0;
  for (const auto& x : m) {
    const auto it = zip.find(x.ray.id + ".npy");
    if (it == zip.end()) {
      std::cerr << args.pos[0] << ": no member " << x.ray.id << ".npy\n";
      ++bad;
      continue;
    }
    bad += compare(x.ray, read_npy_bytes(it->second, x.ray.id + ".npy"),
                   std::string(args.pos[0]) + ":" + x.ray.id + ".npy");
    if (comment.find("\"" + x.ray.id + "\":") == std::string::npos) {
      std::cerr << "archive comment lacks " << x.ray.id << '\n';
      ++bad;
    }
    if (!x.group.empty() && comment.find("\"exceptional\":{") == std::string::npos) {
      std::cerr << "no exceptional section\n";
      ++bad;
    }
  }
  if (zip.size() != m.size()) {
    std::cerr << args.pos[0] << ": " << zip.size() << " members, expected " << m.size() << '\n';
    ++bad;
  }
  std::cout << (bad ? "NPZ-CHECK FAIL " : "NPZ-CHECK PASS ")
            << filesystem::path(args.pos[0]).filename().string() << " members=" << zip.size()
            << '\n';
  return bad ? 1 : 0;
}

int catalogue_multiplets(const sic::support::cli::Arguments& args) {
  args.positionals(1, 1);
  std::string text;
  {
    const ByteBuffer b = read_bytes(args.pos[0]);
    text.assign(b.begin(), b.end());
  }
  while (!text.empty() && text.back() == '\n') text.pop_back();
  const sic::support::Json c = sic::support::JsonParser::parse(text);
  if (c.at("schema").as_str() != "sic-release-catalogue/1.0")
    die(sic::ErrorKind::unsupported, "unsupported catalogue schema");
  for (const auto& dim : c.at("dimensions").as_array())
    for (const auto& m : dim.at("multiplets").as_array())
      std::cout << "multiplet " << m.at("id").as_str() << ' ' << m.at("sicx").at("sha256").as_str()
                << ' ' << m.at("sicx").at("exact_root").as_str() << '\n';
  if (c.has("exceptional"))
    for (const auto& e : c.at("exceptional").as_array())
      std::cout << "exceptional " << e.at("id").as_str() << '\n';
  return 0;
}

}  // namespace
int run(const std::string& command, const sic::support::cli::Arguments& args) try {
  if (command == "layout") {
    args.positionals(2, 2);
    sic::data::ReleaseOptions options;
    options.multiplet = args.numeric<uint64_t>("--multiplet", 0, 1);
    options.threads =
        args.numeric<unsigned>("--threads", kDefaultLayoutThreads, 1, kMaximumLayoutThreads);
    sic::support::cli::require(options.multiplet != 0, "layout requires --multiplet N");
    std::cout << sic::data::release_layout(args.pos[0], args.pos[1], options).string() << '\n';
    return 0;
  }
  if (command == "catalogue") {
    args.positionals(1, 1);
    write_new(filesystem::path(args.pos[0]) / "catalogue.json",
              to_bytes(sic::support::dump_json(sic::data::release_catalogue(args.pos[0])) + "\n"));
    return 0;
  }
  if (command == "write") return numeric_write(args);
  if (command == "check") return numeric_check(args);
  if (command == "bundle") return npz(args);
  if (command == "check-bundle") return npz_check(args);
  if (command == "list") return catalogue_multiplets(args);
  if (command == "formats") {
    args.positionals(0, 0);
    std::cout << "npy mat csv" << (interchange::have_h5() ? " h5" : "") << "\n";
    return 0;
  }
  throw sic::support::cli::UsageError("expected a sic-data command");
} catch (const filesystem::filesystem_error& error) {
  throw sic::Error(ErrorKind::io, error.what());
}
}  // namespace sic::data
