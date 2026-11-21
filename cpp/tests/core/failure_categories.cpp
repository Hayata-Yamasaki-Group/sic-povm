// SPDX-License-Identifier: AGPL-3.0-or-later
// Exact failure categories at the numerical and interchange boundaries.
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <sstream>

#include "assertions.hpp"
#include "core/io/mat.hpp"
#include "core/io/npz.hpp"
#include "core/numeric/verify.hpp"
#include "sic/core/fiducial_io.hpp"
#include "sic/core/hdf5.hpp"
#include "sic/core/sic_frame.hpp"
#include "sic/core/verify.hpp"
#include "sic/search/clifford.hpp"
#ifdef SIC_HAVE_HDF5
#include <hdf5.h>
#if defined(__linux__) && __SIZEOF_POINTER__ == 8
#include <dlfcn.h>
#include <sys/syscall.h>

#include <cerrno>
static bool fail_h5_read = false;
static off_t h5_read_fault_offset = 0;
static bool fail_h5_flush = false;
static bool fail_h5_close = false;
static unsigned h5_close_calls = 0;
static bool fail_h5_space = false;
static bool fail_h5_dims = false;
extern "C" hid_t H5Dget_space(hid_t id) {
  if (fail_h5_space) return -1;
  static const auto real = reinterpret_cast<hid_t (*)(hid_t)>(dlsym(RTLD_NEXT, "H5Dget_space"));
  return real(id);
}
extern "C" int H5Sget_simple_extent_dims(hid_t id, hsize_t* dims, hsize_t* max_dims) {
  if (fail_h5_dims) return -1;
  static const auto real = reinterpret_cast<int (*)(hid_t, hsize_t*, hsize_t*)>(
      dlsym(RTLD_NEXT, "H5Sget_simple_extent_dims"));
  return real(id, dims, max_dims);
}
extern "C" herr_t H5Fflush(hid_t id, H5F_scope_t scope) {
  if (fail_h5_flush) return -1;
  static const auto real =
      reinterpret_cast<herr_t (*)(hid_t, H5F_scope_t)>(dlsym(RTLD_NEXT, "H5Fflush"));
  return real(id, scope);
}
extern "C" herr_t H5Fclose(hid_t id) {
  ++h5_close_calls;
  static const auto real = reinterpret_cast<herr_t (*)(hid_t)>(dlsym(RTLD_NEXT, "H5Fclose"));
  const auto result = real(id);
  if (fail_h5_close && result >= 0) {
    fail_h5_close = false;
    return -1;
  }
  return result;
}
extern "C" ssize_t pread(int fd, void* data, size_t count, off_t offset) {
  if (fail_h5_read && offset >= h5_read_fault_offset) {
    errno = EIO;
    return -1;
  }
  return syscall(SYS_pread64, fd, data, count, offset);
}
extern "C" ssize_t pread64(int fd, void* data, size_t count, off64_t offset) {
  return pread(fd, data, count, offset);
}
#endif
#endif

namespace io = sic::core::interchange;
using sic::ErrorKind;
using sic::core::Vector;

template <class F>
void expect(ErrorKind kind, F&& operation) {
  bool caught = false;
  try {
    operation();
  } catch (const sic::Error& error) {
    caught = true;
    if (error.kind() != kind)
      std::cerr << "expected " << sic::error_kind_name(kind) << ", got "
                << sic::error_kind_name(error.kind()) << ": " << error.what() << '\n';
    SIC_CHECK(error.kind() == kind);
  } catch (const std::exception& error) {
    std::cerr << "uncategorized failure: " << error.what() << '\n';
  }
  SIC_CHECK(caught);
}

class BrokenBuffer : public std::streambuf {
 public:
  enum class Cause { ios, runtime, allocation, overflow };
  explicit BrokenBuffer(Cause cause = Cause::ios) : cause_(cause) {}

 private:
  [[noreturn]] void raise() {
    switch (cause_) {
      case Cause::ios:
        throw std::ios_base::failure("stream fault");
      case Cause::runtime:
        throw std::runtime_error("buffer fault");
      case Cause::allocation:
        throw std::bad_alloc();
      case Cause::overflow:
        throw std::overflow_error("native overflow");
    }
    std::terminate();
  }
  int_type underflow() override { raise(); }
  int_type overflow(int_type) override { raise(); }
  std::streamsize xsputn(const char*, std::streamsize) override { raise(); }
  Cause cause_;
};

template <class E, class F>
void expect_standard(F&& operation) {
  bool caught = false;
  try {
    operation();
  } catch (const E&) {
    caught = true;
  }
  SIC_CHECK(caught);
}

static void put32(io::ByteBuffer& bytes, std::size_t at, std::uint32_t value) {
  for (int j = 0; j < 4; ++j) bytes.at(at + j) = static_cast<unsigned char>(value >> (8 * j));
}

// Tiny, well-formed foreign ZIP profiles; no external compressor or fixture.
static io::ByteBuffer foreign_zip(bool deflated, bool zip64) {
  const std::string name = "x.npy";
  const auto data = io::npy_encode({1.0});
  io::ByteBuffer payload = data, extra;
  if (deflated) {
    // One final uncompressed DEFLATE block, wrapped as ZIP compression method 8.
    payload = {1};
    io::zip::p16(payload, data.size());
    io::zip::p16(payload, 65535 - data.size());
    payload.insert(payload.end(), data.begin(), data.end());
  }
  if (zip64) {
    io::zip::p16(extra, 1);
    io::zip::p16(extra, 16);
    for (const auto size : {data.size(), payload.size()}) {
      io::zip::p32(extra, size);
      io::zip::p32(extra, 0);
    }
  }
  io::ByteBuffer out;
  const auto crc = io::crc32(data.data(), data.size());
  io::zip::p32(out, 0x04034b50);
  io::zip::p16(out, zip64 ? 45 : 20);
  io::zip::p16(out, 0);
  io::zip::p16(out, deflated ? 8 : 0);
  io::zip::p16(out, 0x1234);
  io::zip::p16(out, 0x5b6d);
  io::zip::p32(out, crc);
  io::zip::p32(out, zip64 ? UINT32_MAX : payload.size());
  io::zip::p32(out, zip64 ? UINT32_MAX : data.size());
  io::zip::p16(out, name.size());
  io::zip::p16(out, extra.size());
  out.insert(out.end(), name.begin(), name.end());
  out.insert(out.end(), extra.begin(), extra.end());
  out.insert(out.end(), payload.begin(), payload.end());
  const auto cp = out.size();
  io::zip::p32(out, 0x02014b50);
  io::zip::p16(out, zip64 ? 45 : 20);
  io::zip::p16(out, zip64 ? 45 : 20);
  io::zip::p16(out, 0);
  io::zip::p16(out, deflated ? 8 : 0);
  io::zip::p16(out, 0x1234);
  io::zip::p16(out, 0x5b6d);
  io::zip::p32(out, crc);
  io::zip::p32(out, payload.size());
  io::zip::p32(out, data.size());
  io::zip::p16(out, name.size());
  for (int j = 0; j < 4; ++j) io::zip::p16(out, 0);
  io::zip::p32(out, 0);
  io::zip::p32(out, 0);
  out.insert(out.end(), name.begin(), name.end());
  const auto central_bytes = out.size() - cp;
  io::zip::p32(out, 0x06054b50);
  io::zip::p16(out, 0);
  io::zip::p16(out, 0);
  io::zip::p16(out, 1);
  io::zip::p16(out, 1);
  io::zip::p32(out, central_bytes);
  io::zip::p32(out, cp);
  io::zip::p16(out, 0);
  return out;
}

int main() {
  auto pattern =
      (std::filesystem::temp_directory_path() / "sic-failure-categories-XXXXXX").string();
  const char* created = ::mkdtemp(pattern.data());
  SIC_CHECK(created != nullptr);
  if (!created) return sic_test::report();
  const std::filesystem::path root(created);
  const auto path = (root / "input").string();
  const auto absent = (root / "absent" / "file").string();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  const Vector finite{1.0, 2.0};
  const sic::core::WeylHeisenberg wh(2);
  Vector output;
  expect(ErrorKind::invalid_argument, [] { sic::core::WeylHeisenberg invalid(0); });
  expect(ErrorKind::invalid_argument, [&] { wh.apply(0, 0, {}); });
  expect(ErrorKind::invalid_argument, [&] { wh.matrix_element({}, 0, 0, finite); });
  expect(ErrorKind::invalid_argument, [&] { wh.overlap_row(0, {}, output); });
  Vector alias = finite;
  expect(ErrorKind::invalid_argument, [&] { wh.overlap_row(0, alias, alias); });
  expect(ErrorKind::invalid_argument, [&] { wh.cross_overlap_row(0, {}, finite, output); });
  expect(ErrorKind::invalid_argument, [&] { wh.cross_overlap_row(0, finite, {}, output); });
  expect(ErrorKind::invalid_argument, [&] { wh.cross_overlap_row(0, alias, finite, alias); });
  expect(ErrorKind::invalid_argument, [&] { wh.cross_overlap_row(0, finite, alias, alias); });
  expect(ErrorKind::invalid_argument, [&] { wh.overlaps({}); });
  // Exercise native index admission without allocating a 32 GiB input vector.
  SIC_CHECK(sic::core::detail::checked_verify_dimension(std::numeric_limits<int>::max()) ==
            std::numeric_limits<int>::max());
  expect(ErrorKind::invalid_argument, [] {
    sic::core::detail::checked_verify_dimension(std::size_t{std::numeric_limits<int>::max()} + 1);
  });
  expect(ErrorKind::invalid_argument, [] { sic::core::verify_fiducial({}); });
  expect(ErrorKind::invalid_argument, [] { sic::core::verify_fiducial(Vector(2)); });
  for (double bad_value : {nan, inf})
    expect(ErrorKind::invalid_argument, [&] { sic::core::verify_fiducial({bad_value}); });
  expect(ErrorKind::invalid_argument, [] {
    const auto maximum = std::numeric_limits<double>::max();
    sic::core::verify_fiducial({maximum, maximum});
  });
  namespace clifford = sic::core::clifford;
  expect(ErrorKind::invalid_argument, [] { clifford::inverse_mod(2, 4); });
  expect(ErrorKind::invalid_argument, [] { clifford::canonical_lift({0, 0, 0, 0}, 2, 1); });
  expect(ErrorKind::invalid_argument, [] { clifford::factorize({0, 0, 0, 0}, 2); });
  expect(ErrorKind::invalid_argument, [] { clifford::CliffordAction invalid(0); });
  expect(ErrorKind::invalid_argument, [] { clifford::CliffordAction invalid(2, 1); });
  expect(ErrorKind::invalid_argument,
         [] { clifford::CliffordAction invalid((clifford::Int{1} << 30) + 1, UINT64_MAX); });
  const clifford::CliffordAction action(2);
  expect(ErrorKind::invalid_argument, [&] { action.apply({}, {}, output); });
  expect(ErrorKind::invalid_argument, [] { sic::core::Dft invalid(0); });
  const sic::core::Dft dft(2);
  expect(ErrorKind::invalid_argument, [&] { dft.transform(output, 1); });
  Vector values = finite;
  expect(ErrorKind::invalid_argument, [&] { dft.transform(values, 0); });
  expect(ErrorKind::invalid_argument, [&] { sic::search::clifford_maps({}, {}); });
  expect(ErrorKind::invalid_argument, [&] { sic::search::clifford_maps(finite, {1.0}); });
  for (const auto& v : {Vector{}, Vector(2), Vector{nan}, Vector{inf}, Vector{{1, nan}}}) {
    expect(ErrorKind::invalid_argument, [&] { sic::core::normalized_copy(v); });
    expect(ErrorKind::invalid_argument, [&] { sic::core::SicFrame frame(v); });
  }
  expect(ErrorKind::resource, [&] { sic::core::SicFrame frame(finite, true, 1); });
  SIC_CHECK(sic::core::normalized_copy(finite).size() == finite.size());
  for (double bad : {nan, inf}) {
    expect(ErrorKind::invalid_argument, [&] { io::require_finite({bad}, "output"); });
    expect(ErrorKind::invalid_argument, [&] { io::npy_encode({bad}); });
    expect(ErrorKind::invalid_argument, [&] { io::npz_encode(1, {{"ray", {bad}, ""}}); });
    expect(ErrorKind::invalid_argument, [&] {
      std::ostringstream out;
      sic::core::write_vector(out, {bad});
    });
    io::MatFile mat("test");
    expect(ErrorKind::invalid_argument, [&] { mat.add_scalar("x", bad); });
    expect(ErrorKind::invalid_argument, [&] { mat.add_complex_scalar("x", {1, bad}); });
    expect(ErrorKind::invalid_argument, [&] { mat.add_complex_column("x", {bad}); });
    auto npy = io::npy_encode({1.0});
    std::memcpy(npy.data() + npy.size() - 16, &bad, 8);
    expect(ErrorKind::invalid_format, [&] { io::npy_decode(npy); });
    auto vars = io::mat_decode(io::fiducial_mat({1.0}, "ray", "", "", "").bytes());
    vars[0].re[0] = bad;
    expect(ErrorKind::invalid_format, [&] { io::mat_psi(vars); });
  }
  for (const std::string text :
       {"", "x", "0", "2\n1 0", "1\n1 0 extra", "1\nnan 0", "1\n1e999 0"}) {
    expect(ErrorKind::invalid_format, [&] {
      std::istringstream in(text);
      sic::core::read_vector(in);
    });
  }
  for (const std::string token : {"nan", "inf", "1e999"}) {
    try {
      std::istringstream in("1\n" + token + " 0");
      sic::core::read_vector(in);
      SIC_CHECK(false);
    } catch (const sic::Error& error) {
      SIC_CHECK(error.kind() == ErrorKind::invalid_format);
      SIC_CHECK(std::string(error.what()).find("token '" + token + "'") != std::string::npos);
      SIC_CHECK(std::string(error.what()).find("finite representable") != std::string::npos);
    }
  }
  {
    std::istringstream in("1\n1 0\n");
    in.exceptions(std::ios::badbit | std::ios::failbit);
    SIC_CHECK(sic::core::read_vector(in) == Vector{1.0});
  }
  expect(ErrorKind::resource, [&] {
    std::istringstream in("1\n1 0");
    sic::InputLimits limits;
    limits.maximum_bytes = 1;
    sic::core::read_vector(in, limits);
  });
  expect(ErrorKind::resource, [&] {
    std::istringstream in("2\n1 0\n1 0");
    sic::InputLimits limits;
    limits.maximum_dimension = 1;
    sic::core::read_vector(in, limits);
  });
  for (bool exceptions : {false, true}) {
    expect(ErrorKind::io, [&] {
      BrokenBuffer buffer;
      std::istream in(&buffer);
      if (exceptions) in.exceptions(std::ios::badbit);
      sic::core::read_vector(in);
    });
    expect(ErrorKind::io, [&] {
      std::ostringstream out;
      out.setstate(std::ios::badbit);
      if (exceptions) {
        try {
          out.exceptions(std::ios::badbit);
        } catch (const std::ios_base::failure&) {
        }
      }
      sic::core::write_vector(out, finite);
    });
  }
  for (auto cause : {BrokenBuffer::Cause::ios, BrokenBuffer::Cause::runtime}) {
    for (bool exceptions : {false, true}) {
      expect(ErrorKind::io, [&] {
        BrokenBuffer buffer(cause);
        std::istream in(&buffer);
        if (exceptions) in.exceptions(std::ios::badbit);
        sic::core::read_vector(in);
      });
      expect(ErrorKind::io, [&] {
        BrokenBuffer buffer(cause);
        std::ostream out(&buffer);
        if (exceptions) out.exceptions(std::ios::badbit);
        sic::core::write_vector(out, finite);
      });
    }
  }
  auto native_stream_failure = [&](BrokenBuffer::Cause cause, bool reading) {
    BrokenBuffer buffer(cause);
    if (reading) {
      std::istream in(&buffer);
      in.exceptions(std::ios::badbit);
      sic::core::read_vector(in);
    } else {
      std::ostream out(&buffer);
      out.exceptions(std::ios::badbit);
      sic::core::write_vector(out, finite);
    }
  };
  for (bool reading : {false, true}) {
    expect_standard<std::bad_alloc>(
        [&] { native_stream_failure(BrokenBuffer::Cause::allocation, reading); });
    expect_standard<std::overflow_error>(
        [&] { native_stream_failure(BrokenBuffer::Cause::overflow, reading); });
  }
  expect(ErrorKind::io, [&] { io::read_file(absent); });
  expect(ErrorKind::io, [&] { sic::core::read_vector_file(absent); });
  expect(ErrorKind::io, [&] { io::write_file(absent, "data"); });
  expect(ErrorKind::io, [&] { io::write_file("/dev/full", "data"); });
  // The owner creates a regular temporary and reaches write(2), which the kernel
  // refuses with EFBIG. Isolate the file-size limit and SIGXFSZ disposition.
  const auto limited = (root / "limited").string();
  io::write_file(limited, "previous");
  const pid_t child = fork();
  SIC_CHECK(child >= 0);
  if (child == 0) {
    const rlimit limit{1, 1};
    if (signal(SIGXFSZ, SIG_IGN) == SIG_ERR || setrlimit(RLIMIT_FSIZE, &limit)) _exit(2);
    try {
      io::write_file(limited, "payload exceeds the file-size limit");
    } catch (const sic::Error& error) {
      _exit(error.kind() == ErrorKind::io &&
                    std::string(error.what()) == "write failed for " + limited
                ? 0
                : 3);
    } catch (...) {
      _exit(4);
    }
    _exit(5);
  }
  if (child > 0) {
    int status = 0;
    SIC_CHECK(waitpid(child, &status, 0) == child);
    SIC_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    const auto preserved = io::read_file(limited);
    SIC_CHECK(std::string(preserved.begin(), preserved.end()) == "previous");
    for (const auto& entry : std::filesystem::directory_iterator(root))
      SIC_CHECK(entry.path().filename().string().find("limited.tmp-") == std::string::npos);
  }
  expect(ErrorKind::io, [&] { io::read_file(root.string()); });
  for (const std::string text :
       {"", "wrong header", "index,real,imag\nbad row\n", "index,real,imag\n1,1,0\n",
        "index,real,imag\n", "index,real,imag\n0,nan,0\n"}) {
    io::write_file(path, text);
    expect(ErrorKind::invalid_format, [&] { io::read_csv(path); });
  }
  expect(ErrorKind::invalid_format, [&] { io::npy_decode({}); });
  expect(ErrorKind::invalid_format, [&] { io::mat_decode({}); });
  expect(ErrorKind::invalid_format, [&] { io::mat_find({}, "psi"); });
  expect(ErrorKind::invalid_format, [&] {
    io::MatFile mat("test");
    mat.add_scalar("psi", 1);
    io::mat_psi(io::mat_decode(mat.bytes()));
  });
  // The writer accepts UTF-8; the decoder accepts UTF-16 wire content.
  for (const auto& invalid : {std::string("\xff"), std::string("\xe2"), std::string("\xe2\x20\xa0"),
                              std::string("\xe0\x80\x80")}) {
    expect(ErrorKind::invalid_argument, [&] {
      io::MatFile mat("test");
      mat.add_string("s", invalid);
    });
  }
  for (const std::uint16_t surrogate : {0xd800, 0xdc00}) {
    io::MatFile mat("test");
    mat.add_string("s", "x");
    auto bytes = mat.bytes();
    // Last full-form element is the two-byte UTF-16 payload plus six padding bytes.
    bytes[bytes.size() - 8] = static_cast<unsigned char>(surrogate);
    bytes[bytes.size() - 7] = static_cast<unsigned char>(surrogate >> 8);
    expect(ErrorKind::invalid_format, [&] { io::mat_decode(bytes); });
  }
  {
    std::string name(20, 'x');
    name[0] = 'P';
    name[1] = 'K';
    name[2] = 6;
    name[3] = 7;
    const auto ordinary = io::zip_store_decode(io::zip_store_encode({{name, {}}}));
    SIC_CHECK(ordinary.size() == 1 && ordinary[0].name == name);
  }
  const auto zip = io::zip_store_encode({{"x.npy", io::npy_encode({1.0})}},
                                        "{\"schema\":\"sic-npz-bundle/1.0\",}");
  // Locate the central directory by its signature, independent of the comment length.
  std::size_t cp = 0;
  for (std::size_t j = 0; j + 4 < zip.size(); ++j)
    if (zip[j] == 0x50 && zip[j + 1] == 0x4b && zip[j + 2] == 1 && zip[j + 3] == 2) cp = j;
  SIC_CHECK(cp > 0);
  // Timestamp-only deviation is an unsupported deterministic profile, too.
  for (const auto& profile : {std::pair{false, false}, std::pair{true, false},
                              std::pair{false, true}, std::pair{true, true}}) {
    auto foreign = foreign_zip(profile.first, profile.second);
    expect(ErrorKind::unsupported, [&] { io::zip_store_decode(foreign); });
    // A contradiction takes precedence over any unsupported feature or timestamp.
    foreign[10] ^= 1;
    expect(ErrorKind::invalid_format, [&] { io::zip_store_decode(foreign); });
  }
  expect(ErrorKind::invalid_format, [&] { io::npz_decode({}); });
  auto bad = zip;
  bad[35] ^= 1;
  expect(ErrorKind::integrity, [&] { io::npz_decode(bad); });
  for (const auto at : {std::size_t{8}, cp + 10}) {
    bad = zip;
    bad[at] = 8;
    expect(ErrorKind::invalid_format, [&] { io::npz_decode(bad); });
  }
  bad = zip;
  bad[8] = bad[cp + 10] = 8;
  expect(ErrorKind::unsupported, [&] { io::npz_decode(bad); });
  bad = zip;
  // End record starts immediately after the single central-directory entry.
  put32(bad, cp + 46 + 5 + 16, UINT32_MAX);
  expect(ErrorKind::unsupported, [&] { io::npz_decode(bad); });
  const std::size_t eocd = cp + 46 + 5;
  bad = zip;
  // ZIP64 may be used even when its counts and offsets fit the ordinary fields.
  io::ByteBuffer end64;
  io::zip::p32(end64, 0x06064b50);
  io::zip::p32(end64, 44);
  io::zip::p32(end64, 0);
  io::zip::p16(end64, 45);
  io::zip::p16(end64, 45);
  io::zip::p32(end64, 0);
  io::zip::p32(end64, 0);
  for (int j = 0; j < 2; ++j) {
    io::zip::p32(end64, 1);
    io::zip::p32(end64, 0);
  }
  io::zip::p32(end64, 46 + 5);
  io::zip::p32(end64, 0);
  io::zip::p32(end64, static_cast<std::uint32_t>(cp));
  io::zip::p32(end64, 0);
  io::zip::p32(end64, 0x07064b50);
  io::zip::p32(end64, 0);
  io::zip::p32(end64, static_cast<std::uint32_t>(eocd));
  io::zip::p32(end64, 0);
  io::zip::p32(end64, 1);
  SIC_CHECK(end64.size() == 76);
  bad.insert(bad.begin() + static_cast<std::ptrdiff_t>(eocd), end64.begin(), end64.end());
  expect(ErrorKind::unsupported, [&] { io::npz_decode(bad); });
  bad = zip;
  bad[eocd + 4] = 1;
  expect(ErrorKind::unsupported, [&] { io::npz_decode(bad); });
  bad = zip;
  bad.insert(bad.begin() + static_cast<std::ptrdiff_t>(eocd), 'x');
  bad[cp + 32] = 1;
  put32(bad, eocd + 1 + 12, 46 + 5 + 1);
  expect(ErrorKind::unsupported, [&] { io::npz_decode(bad); });
  {
    std::vector<io::ZipMember> members;
    for (std::size_t j = 0; j < 65535; ++j) members.push_back({std::to_string(j), {}});
    SIC_CHECK(io::zip_store_decode(io::zip_store_encode(members)).size() == members.size());
  }
  expect(ErrorKind::unsupported, [&] { io::admit_zip_profile(65536, 0); });
  expect(ErrorKind::unsupported, [&] { io::admit_zip_profile(0, uint64_t{1} << 31); });
  expect(ErrorKind::unsupported, [&] { io::zip_store_encode(std::vector<io::ZipMember>(65536)); });
  expect(ErrorKind::unsupported, [&] {
    const std::string comment = "{\"schema\":\"unknown\",}";  // identity-negative
    io::npz_decode(io::zip_store_encode({}, comment));
  });
  expect(ErrorKind::resource, [&] { io::zip_store_encode({}, std::string(65536, 'x')); });
  expect(ErrorKind::invalid_argument, [&] { io::zip_store_encode({{"", {}}}); });
  expect(ErrorKind::resource, [&] { io::zip_store_encode({{std::string(65536, 'x'), {}}}); });
  expect(ErrorKind::invalid_argument, [&] { io::npz_encode(1, {}); });
  expect(ErrorKind::invalid_argument, [&] { io::npz_encode(1, {{"../x", {1.0}, ""}}); });
  expect(ErrorKind::invalid_argument, [&] {
    io::npz_encode(1, {{"x", {1.0}, ""}, {"x", {1.0}, ""}});
  });
  expect(ErrorKind::invalid_argument, [&] { io::npz_encode(2, {{"x", {1.0}, ""}}); });
  expect(ErrorKind::invalid_argument, [&] { io::npz_encode(1, {{"x", {1.0}, "", "\n"}}); });
  expect(ErrorKind::resource, [&] { io::npz_encode(1, {{"x", {1.0}, std::string(65536, 'x')}}); });
  expect(ErrorKind::invalid_format, [&] {
    io::npz_decode(io::zip_store_encode({{"x.txt", {}}}, "{\"schema\":\"sic-npz-bundle/1.0\",}"));
  });
#ifdef SIC_HAVE_HDF5
  SIC_CHECK(io::have_h5());
  H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);
  expect(ErrorKind::io, [&] { io::write_h5(absent, finite, {}, {}); });
  expect(ErrorKind::io, [&] { io::read_h5(absent); });
  expect(ErrorKind::io, [&] { io::read_h5(root.string()); });
  const auto blocked_parent = (root / "blocked_parent").string();
  io::write_file(blocked_parent, "regular file");
  expect(ErrorKind::io, [&] { io::write_h5(blocked_parent + "/child.h5", finite, {}, {}); });
  expect(ErrorKind::invalid_argument, [&] { io::write_h5(path, {nan}, {}, {}); });
  io::write_file(path, "malformed HDF5");
  expect(ErrorKind::invalid_format, [&] { io::read_h5(path); });
  {
    const hid_t file = H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    SIC_CHECK(file >= 0);
    H5Fclose(file);
  }
  expect(ErrorKind::invalid_format, [&] { io::read_h5(path); });
  io::write_h5(path, finite, {}, {});
  SIC_CHECK(io::read_h5(path) == finite);
  {
    const hid_t file =
        H5Fcreate((path + "-oversize").c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    const hsize_t dim = sic::kDefaultCoreDimension + 1;
    const hid_t space = H5Screate_simple(1, &dim, nullptr);
    const hid_t ds =
        H5Dcreate2(file, "psi", H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    SIC_CHECK(ds >= 0);
    H5Dclose(ds);
    H5Sclose(space);
    H5Fclose(file);
    // No dataset storage is written: admission must fail before allocation/read.
    expect(ErrorKind::resource, [&] { io::read_h5(path + "-oversize"); });
  }
#if defined(__linux__) && __SIZEOF_POINTER__ == 8
  fail_h5_space = true;
  expect(ErrorKind::invalid_format, [&] { io::read_h5(path); });
  fail_h5_space = false;
  fail_h5_dims = true;
  expect(ErrorKind::invalid_format, [&] { io::read_h5(path); });
  fail_h5_dims = false;
  fail_h5_read = true;
  expect(ErrorKind::io, [&] { io::read_h5(path); });
  h5_read_fault_offset = 2048;
  expect(ErrorKind::io, [&] { io::read_h5(path); });
  fail_h5_read = false;
  fail_h5_flush = true;
  expect(ErrorKind::io, [&] { io::write_h5(path + "-flush", finite, {}, {}); });
  fail_h5_flush = false;
  fail_h5_close = true;
  const auto closes_before_failure = h5_close_calls;
  expect(ErrorKind::io, [&] { io::write_h5(path + "-close", finite, {}, {}); });
  SIC_CHECK(h5_close_calls == closes_before_failure + 1);
  fail_h5_close = false;
#endif
  std::filesystem::resize_file(path, std::filesystem::file_size(path) / 2);
  expect(ErrorKind::invalid_format, [&] { io::read_h5(path); });
  io::write_h5(path, finite, {}, {});
  {
    const hid_t file = H5Fopen(path.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
    const hid_t ds = H5Dopen2(file, "psi", H5P_DEFAULT);
    const hid_t type = H5Dget_type(ds);
    const Vector bad_values{nan, 1.0};
    SIC_CHECK(H5Dwrite(ds, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, bad_values.data()) >= 0);
    H5Tclose(type);
    H5Dclose(ds);
    H5Fclose(file);
  }
  expect(ErrorKind::invalid_format, [&] { io::read_h5(path); });
  {
    const hid_t file = H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    const hsize_t dims[] = {1, 1};
    const hid_t space = H5Screate_simple(2, dims, nullptr);
    const hid_t ds =
        H5Dcreate2(file, "psi", H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    SIC_CHECK(ds >= 0);
    H5Dclose(ds);
    H5Sclose(space);
    H5Fclose(file);
  }
  expect(ErrorKind::invalid_format, [&] { io::read_h5(path); });
  {
    const hid_t file = H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    const hsize_t dim = 1;
    const hid_t space = H5Screate_simple(1, &dim, nullptr);
    const hid_t ds =
        H5Dcreate2(file, "psi", H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    SIC_CHECK(ds >= 0);
    H5Dclose(ds);
    H5Sclose(space);
    H5Fclose(file);
  }
  expect(ErrorKind::invalid_format, [&] { io::read_h5(path); });
#else
  SIC_CHECK(!io::have_h5());
  expect(ErrorKind::unsupported, [&] { io::write_h5(path, finite, {}, {}); });
  expect(ErrorKind::unsupported, [&] { io::read_h5(path); });
#endif
  std::filesystem::remove_all(root);
  return sic_test::report();
}
