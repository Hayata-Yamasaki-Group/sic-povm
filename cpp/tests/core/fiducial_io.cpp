// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/core/fiducial_io.hpp"

#include <filesystem>
#include <sstream>

#include "assertions.hpp"
namespace interchange = sic::core::interchange;
template <class F>
bool refused(F f) {
  try {
    f();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}
int main(int argc, char** argv) {
  SIC_CHECK(argc == 2);
  if (argc != 2) return 1;
  const std::string root = argv[1];
  std::filesystem::create_directories(root);
  const sic::core::Vector psi{{1.0, -0.0}, {0x1p-1022, 0x1.123456789abcdep-10}};
  const auto npy = interchange::npy_encode(psi);
  for (unsigned char version : {1, 2, 3}) {
    auto bytes = npy;
    if (version != 1) {
      const std::size_t old_len = bytes[8] + 256 * bytes[9];
      bytes[6] = version;
      bytes.insert(bytes.begin() + 10, 2, 0);
      bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(10 + old_len - 2),
                  bytes.begin() + static_cast<std::ptrdiff_t>(10 + old_len));
      const std::size_t len = old_len - 2;
      for (unsigned j = 0; j < 4; ++j) bytes[8 + j] = static_cast<unsigned char>(len >> (8 * j));
    }
    const auto path = root + "/version" + std::to_string(version) + ".npy";
    interchange::write_file(path, bytes);
    SIC_CHECK(interchange::bit_equal(sic::core::files::read_fiducial(path), psi));
    bytes.pop_back();
    SIC_CHECK(refused([&] { interchange::npy_decode(bytes); }));
  }
  auto short_header = npy;
  short_header[8] = 0;
  short_header[9] = 0;
  SIC_CHECK(refused([&] { interchange::npy_decode(short_header); }));
  auto truncated_shape = npy;
  const std::string dict = "{'descr': '<c16', 'fortran_order': False, 'shape': (2";
  truncated_shape.resize(10 + dict.size());
  std::copy(dict.begin(), dict.end(), truncated_shape.begin() + 10);
  truncated_shape[8] = static_cast<unsigned char>(dict.size());
  SIC_CHECK(refused([&] { interchange::npy_decode(truncated_shape); }));
  const auto csv = root + "/input.csv";
  for (const std::string text :
       {"index,real,imag\n0,1,0\n1,-0,1e-100\n", "index,real,imag\r\n0,1,0\r\n1,-0,1e-100\r\n"}) {
    interchange::write_file(csv, text);
    SIC_CHECK(sic::core::files::read_fiducial(csv).size() == 2);
  }
  for (const std::string text :
       {"index,real,imagGARBAGE\n0,1,0\n", "index,real,imag\n", "index,real,imag\n0,nan,0\n",
        "index,real,imag\n0,1,0,\n", "index,real,imag\n1,1,0\n"}) {
    interchange::write_file(csv, text);
    SIC_CHECK(refused([&] { sic::core::files::read_fiducial(csv); }));
  }
  for (const std::string text : {"2\n1 0\n0 0\ntrailing", "2\n1 0\n", "2\ninf 0\n0 0\n"}) {
    std::istringstream input(text);
    SIC_CHECK(refused([&] { sic::core::read_vector(input); }));
  }
  std::filesystem::remove_all(root);
  return sic_test::report();
}
