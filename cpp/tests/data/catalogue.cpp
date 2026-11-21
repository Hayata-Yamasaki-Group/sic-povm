// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/data/catalogue.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

int main(int argc, char** argv) {
  try {
    if (argc != 3) throw std::runtime_error("expected release data and work paths");
    const std::filesystem::path data(argv[1]), work(argv[2]);
    const auto rays = sic::data::read_catalogue((data / "catalogue.json").string());
    const auto base =
        std::find_if(rays.begin(), rays.end(), [](const auto& r) { return r.label == "d2-m1-c0"; });
    if (base == rays.end() || base->d != 2 ||
        base->csv != std::filesystem::absolute(data / "d2/m1/c0.csv").string())
      throw std::runtime_error("interim release catalogue bindings differ");
    std::ifstream in(data / "catalogue.json");
    const std::string original{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
    std::filesystem::create_directories(work);
    const auto bad = work / "invalid.json";
    for (const auto& [from, to] :
         {std::pair{"sic-release-catalogue/1.0", "sic-release-catalogue/1"},   // identity-negative
          std::pair{"sic-release-catalogue/1.0", "sic-release-catalogue/99"},  // identity-negative
          std::pair{"\"d\":\"2\"", "\"d\":\"3\""},
          std::pair{"\"id\":\"d2-m1\"", "\"id\":\"d2-m01\""},
          std::pair{"\"label\":\"c0\"", "\"label\":\"c00\""},
          std::pair{"d2/m1/c0.csv", "../fixture.csv"}}) {
      auto text = original;
      const auto at = text.find(from);
      if (at == std::string::npos) throw std::runtime_error("missing mutation target");
      text.replace(at, std::string(from).size(), to);
      {
        std::ofstream out(bad);
        out << text;
      }
      bool refused = false;
      try {
        (void)sic::data::read_catalogue(bad.string());
      } catch (const std::exception&) {
        refused = true;
      }
      if (!refused) throw std::runtime_error("invalid release catalogue accepted");
    }
    std::filesystem::remove(bad);
    std::cout << "PASS release catalogue and refusal controls\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
