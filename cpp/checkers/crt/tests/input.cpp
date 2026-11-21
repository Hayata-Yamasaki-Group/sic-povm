// SPDX-License-Identifier: AGPL-3.0-or-later
#include <iostream>
#include <cstdlib>

#include "test_context.hpp"
#include "test_general.hpp"
using namespace sic_check::crt::test;
using namespace sic_check::crt::sicx;
namespace filesystem = std::filesystem;
int main(int argc, char** argv) {
  try {
    if (argc == 3 && string(argv[1]) == "--write-general-fixtures") {
      filesystem::create_directories(argv[2]);
      for (Uint64 d : {2, 3, 5, 6, 7, 9, 12, 16}) {
        vector<Uint64> orders;
        for (auto g : brute_generators(d)) orders.push_back(g.order);
        auto data = fixture::faithful_units(d, orders).encode();
        std::ofstream out(
            filesystem::path(argv[2]) / ("d" + std::to_string(d) + "-synthetic-exact.sicx"),
            std::ios::binary);
        out.write((const char*)data.data(), data.size());
        if (!out) throw std::runtime_error("fixture output failed");
      }
      return 0;
    }
    if (argc == 3 && string(argv[1]) == "--write-fixtures") {
      filesystem::create_directories(argv[2]);
      for (Uint64 d : {4, 8})
        for (bool finalized : {false, true})
          for (bool grouped : {false, true}) {
            fixture::File f(d, false, grouped);
            if (grouped) f.rebuild_physical(true);
            if (finalized) f.finalize();
            auto bytes = f.encode();
            string name = "d" + std::to_string(d) + (grouped ? "-grouped" : "") +
                          (finalized ? "-final" : "-exact") + ".sicx";
            std::ofstream out(filesystem::path(argv[2]) / name, std::ios::binary);
            out.write((const char*)bytes.data(), bytes.size());
            if (!out) throw std::runtime_error("fixture output failed");
          }
      return 0;
    }
    auto pattern = (filesystem::temp_directory_path() / "sicx-crt-input-XXXXXX").string();
    if (!::mkdtemp(pattern.data())) throw std::runtime_error("cannot create input test directory");
    scratch = pattern;
    string group = argc > 1 ? argv[1] : "all";
    if (group == "all" || group == "valid") valid_cases();
    if (group == "all" || group == "reject") rejection_cases();
    if (group == "all" || group == "boundaries") boundary_cases();
    if (group == "all" || group == "partial") partial_cases();
    if (group == "all" || group == "amendments") amendment_cases();
    if (group == "all" || group == "hybrid") hybrid_cases();
    if (group == "all" || group == "primitives") primitive_cases();
    if (group == "all" || group == "targeted") targeted_cases();
    if (group == "all" || group == "fuzz") fuzz_cases();
    if (group == "all" || group == "general") general_cases();
    filesystem::remove_all(scratch);
    std::cout << "PASS " << group << ": " << passed << " checks\n";
    flint_cleanup();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL after " << passed << " checks: " << e.what() << "\nfixtures: " << scratch
              << '\n';
    return 1;
  }
}
