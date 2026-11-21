// SPDX-License-Identifier: AGPL-3.0-or-later
// Test-only POSIX interposition: required fail-closed paths for OS/backend failures.
#include <dlfcn.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "../src/format/metadata.hpp"
#include "../src/format/sicx_input.hpp"
#include "test_writer.hpp"
namespace {
std::string fault;
template <class F>
F original(const char* name) {
  auto p = dlsym(RTLD_NEXT, name);
  if (!p) std::abort();
  return reinterpret_cast<F>(p);
}
}  // namespace
extern "C" off_t ftello(FILE* file) {
  if (fault == "ftello") {
    errno = EIO;
    return -1;
  }
  return original<off_t (*)(FILE*)>("ftello")(file);
}
extern "C" int fstat(int fd, struct stat* st) {
  if (fault == "fstat") {
    errno = EIO;
    return -1;
  }
  return original<int (*)(int, struct stat*)>("fstat")(fd, st);
}
extern "C" int fmpz_set_str(fmpz_t out, const char* s, int base) {
  if (fault == "fmpz_set_str") return -1;
  return original<int (*)(fmpz*, const char*, int)>("fmpz_set_str")(out, s, base);
}
extern "C" int arb_intersection(arb_t out, const arb_t x, const arb_t y, slong prec) {
  if (fault == "arb_intersection") return 0;
  return original<int (*)(arb_ptr, arb_srcptr, arb_srcptr, slong)>("arb_intersection")(out, x, y,
                                                                                       prec);
}
extern "C" int _qqbar_roots_poly_squarefree(qqbar_ptr out, qqbar_srcptr c, slong len, slong degree,
                                            slong bits) {
  if (fault == "qqbar_roots") return 0;
  return original<int (*)(qqbar_ptr, qqbar_srcptr, slong, slong, slong)>(
      "_qqbar_roots_poly_squarefree")(out, c, len, degree, bits);
}
int main(int argc, char** argv) {
  using namespace sic_check::crt::sicx;
  auto pattern = (std::filesystem::temp_directory_path() / "crt-fault-XXXXXX").string();
  if (!::mkdtemp(pattern.data())) {
    std::cerr << "cannot create fault test directory\n";
    return 1;
  }
  struct Cleanup {
    std::filesystem::path directory;
    ~Cleanup() {
      std::error_code error;
      std::filesystem::remove_all(directory, error);
    }
  } cleanup{pattern};
  const auto path = cleanup.directory / "input.sicx";
  try {
    if (argc != 2) throw std::runtime_error("usage: sic-test-crt-faults FAULT");
    string name = argv[1], expected;
    bool refusal = false;
    auto b = fixture::odd().encode();
    {
      std::ofstream out(path, std::ios::binary);
      out.write((const char*)b.data(), b.size());
    }
    Input in;
    in.open(path.string(), {}, false);
    AlgebraicNumber root;
    qqbar_sqrt_ui(root.v, 2);
    A enclosure;
    if (name == "ftello")
      expected = "file size failed";
    else if (name == "fstat")
      expected = "source snapshot stat failed";
    else if (name == "fmpz_set_str")
      expected = "Arb integer";
    else if (name == "arb_intersection") {
      expected = "algebraic interval refinement failed";
      refusal = true;
    } else if (name == "qqbar_roots") {
      expected = "exact algebraic odd-root isolation limit";
      refusal = true;
    } else
      throw std::runtime_error("unknown fault");
    bool caught = false;
    fault = name;
    try {
      if (name == "ftello" || name == "fstat") {
        File f;
        f.open(path.string());
      } else if (name == "fmpz_set_str") {
        BallString parsed("1 0 0 0");
      } else if (name == "arb_intersection")
        in.real_enclosure(enclosure.v, root, 256);
      else
        in.check_branches_algebraic();
    } catch (const Refuse& e) {
      fault.clear();
      if (!refusal || string(e.what()).find(expected) == string::npos) throw;
      caught = true;
    } catch (const Reject& e) {
      fault.clear();
      if (refusal || string(e.what()).find(expected) == string::npos) throw;
      caught = true;
    }
    fault.clear();
    if (!caught) throw std::runtime_error("backend failure was silently accepted: " + name);
    std::filesystem::remove(path);
    std::cout << "PASS backend fault " << name << ": " << expected << '\n';
    return 0;
  } catch (const std::exception& e) {
    fault.clear();
    std::filesystem::remove(path);
    std::cerr << "FAIL backend fault: " << e.what() << '\n';
    return 1;
  }
}
