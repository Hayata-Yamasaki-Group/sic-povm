// SPDX-License-Identifier: AGPL-3.0-or-later
#include <iostream>
#include <type_traits>

#include "core/io/mat.hpp"
#include "core/io/npz.hpp"
#include "sic/core/clifford.hpp"
#include "sic/core/fiducial_io.hpp"
#include "sic/error.hpp"
void require(bool b) {
  if (!b) throw std::runtime_error("core policy assertion");
}
template <class F>
void refuses(F f) {
  try {
    f();
  } catch (const std::exception&) {
    return;
  }
  throw std::runtime_error("expected refusal");
}
int main() {
  static_assert(!std::is_constructible_v<sic::Error, std::string>);
  static_assert(std::is_constructible_v<sic::Error, sic::ErrorKind, std::string>);
  static_assert(!std::is_constructible_v<sic::core::SicVectorStream, sic::core::SicFrame&&>);
  try {
    refuses([] { sic::core::clifford::CliffordAction action((int64_t{1} << 24) + 1); });
    std::istringstream huge("1000000000000\n");
    refuses([&] { (void)sic::core::read_vector(huge); });
    using namespace sic::core::interchange;
    ByteBuffer b;
    refuses([&] {
      mat5::matrix(b, "x", mat5::mx_double_class, false, uint64_t{1} << 31, 1, mat5::mi_double,
                   nullptr, nullptr, 0);
    });
    require(b.empty());
    refuses([&] { mat5::element(b, mat5::mi_double, nullptr, uint64_t{1} << 32); });
    require(b.empty());
    refuses([&] { mat5::admit_matrix(uint64_t{1} << 28, 1, 1, uint64_t{1} << 31, true); });
    refuses([] { admit_zip_profile(128, (uint64_t{1} << 31) + 1); });
    admit_zip_profile(2, 1024);
    MatFile m("small");
    m.add_complex_column("psi", {{1, 2}, {3, 4}});
    require(mat_decode(m.bytes()).front().rows == 2);
    std::cout << "core policy: 9 controls PASS\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
