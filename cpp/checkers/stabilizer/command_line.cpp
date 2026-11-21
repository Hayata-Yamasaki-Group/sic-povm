// SPDX-License-Identifier: AGPL-3.0-or-later
#include "command_line.hpp"

#include <flint/acb.h>
#include <flint/arb.h>
#include <flint/fmpq.h>
#include <flint/qqbar.h>
#include <flint/ulong_extras.h>

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "certificate.hpp"

namespace sic_check::stabilizer {
namespace {
constexpr slong kDefaultPrecision = 384;
constexpr int kDefaultThresholdExponent = -100;
}  // namespace

int run_command(int argc, char** argv) {
  for (int version_arg = 1; version_arg < argc; ++version_arg) {
    if (std::string(argv[version_arg]) == "--version") {
      if (argc != 2) {
        std::cerr << "sic-certify-stabilizer: --version requires no other arguments\n";
        return 2;
      }
      std::cout << "{\"name\":\"sic-certify-stabilizer\",\"schema\":\"sic-tool-version/"
                   "1.0\",\"source_commit\":\""
                << SIC_SOURCE_COMMIT << "\",\"version\":\"1.0.0\"}\n";
      return 0;
    }
  }
  const std::string help =
      "usage: sic-certify-stabilizer STABILIZER_FILE [options]\n"
      "  --catalogue JSON|TSV    ray catalogue (default REPO/data/catalogue.json)\n"
      "  --repo DIR              data root (default installed data, then current directory)\n"
      "  --prec BITS             ball precision, 64..100000 (default " +
      std::to_string(kDefaultPrecision) +
      ")\n"
      "  --threshold T           positive overlap tolerance (default 2^" +
      std::to_string(kDefaultThresholdExponent) +
      ")\n"
      "  --d2-exact SICRAD        also verify d2 elements exactly from radical data\n"
      "  --version               print canonical tool identity and exit 0\n"
      "  --help                  print help to stdout and exit 0\n";
  std::string stab_path, root, catalogue, d2_exact;
  slong prec = kDefaultPrecision;
  double threshold = std::ldexp(1.0, kDefaultThresholdExponent);
  bool usage = false;
  for (int i = 1; i < argc && !usage; ++i) {
    const std::string a = argv[i];
    if (a == "--help") {
      std::cout << help;
      return 0;
    }
    const bool more = i + 1 < argc;
    if (a == "--catalogue" && more)
      catalogue = argv[++i];
    else if (a == "--repo" && more)
      root = argv[++i];
    else if (a == "--prec" && more) {
      const std::string value = argv[++i];
      auto parsed = std::from_chars(value.data(), value.data() + value.size(), prec, 10);
      usage = value.empty() || value[0] == '-' || parsed.ec != std::errc{} ||
              parsed.ptr != value.data() + value.size();
    } else if (a == "--threshold" && more) {
      const std::string value = argv[++i];
      char* end = nullptr;
      errno = 0;
      threshold = std::strtod(value.c_str(), &end);
      usage = value.empty() || end != value.c_str() + value.size() || errno == ERANGE ||
              !std::isfinite(threshold);
    } else if (a == "--d2-exact" && more)
      d2_exact = argv[++i];
    else if (stab_path.empty() && !a.empty() && a[0] != '-')
      stab_path = a;
    else
      usage = true;
  }
  if (usage || stab_path.empty() || prec < 64 || prec > 100000 || !(threshold > 0)) {
    std::cerr << "usage: " << argv[0]
              << " STABILIZER_FILE [--catalogue JSON|TSV] [--repo DIR] [--prec BITS="
              << kDefaultPrecision << "] [--threshold T=2^" << kDefaultThresholdExponent
              << "]"
                 " [--d2-exact SICRAD]\n";
    return 2;
  }
  try {
    if (root.empty()) {
      namespace fs = std::filesystem;
      std::error_code ec;
      const auto exe = fs::read_symlink("/proc/self/exe", ec);
      if (!ec) {
        const auto installed = (exe.parent_path() / SIC_DATA_ROOT_FROM_BINDIR).lexically_normal();
        if (fs::is_regular_file(installed / "data/catalogue.json", ec)) root = installed.string();
      }
      ec.clear();
      if (root.empty() && fs::is_regular_file("data/catalogue.json", ec)) root = ".";
      if (root.empty()) throw std::runtime_error("cannot find data catalogue; specify --repo DIR");
    }
    if (catalogue.empty()) catalogue = root + "/data/catalogue.json";
    return certify(stab_path, root, catalogue, d2_exact, prec, threshold);
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    return 2;
  }
}

}  // namespace sic_check::stabilizer
