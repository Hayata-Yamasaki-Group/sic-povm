// SPDX-License-Identifier: AGPL-3.0-or-later
// Container verification only. Mathematical CRT certification remains in crt_certificate.cpp.
#include <iostream>
#include <limits>

#include "src/cli/option_values.hpp"
#include "src/format/container.hpp"
#include "src/format/sicx_input.hpp"
#include "src/runtime/diagnostics.hpp"

using namespace sic_check::crt::sicx;
int main(int argc, char** argv) {
  for (int version_arg = argc - 1; version_arg > 0; --version_arg) {
    if (std::string(argv[version_arg]) == "--version") {
      if (argc != 2) {
        std::cerr << "sic-check-crt-input: --version requires no other arguments\n";
        return 2;
      }
      std::cout << "{\"name\":\"sic-check-crt-input\",\"schema\":\"sic-tool-version/"
                   "1.0\",\"source_commit\":\""
                << SIC_SOURCE_COMMIT << "\",\"version\":\"1.0.0\"}\n";
      return 0;
    }
  }
  if (argc == 2 && std::string(argv[1]) == "--help") {
    std::cout
        << "usage: sic-check-crt-input FILE [--exact-root HEX] [--file-root HEX] [--sha256 "
           "HEX] [--partial-element ID | --partial-chunk ID | --partial-metadata]\n  --help  "
           "Print help and exit 0.\n  --version  Print canonical tool identity and exit 0.\n"
           "Element/chunk requests may be combined and repeated; --partial-metadata is exclusive.\n"
           "Exit codes: 0 scoped pass, 1 invalid data, 2 usage, 3 resource/precision refusal.\n";
    return 0;
  }
  try {
    if (argc < 2 || argv[1][0] == '-') {
      std::cerr
          << "usage: sic-check-crt-input FILE [--exact-root HEX] [--file-root HEX] [--sha256 HEX] "
             "[--partial-element ID | --partial-chunk ID | --partial-metadata]\n";
      return 2;
    }
    Trust trust;
    bool full = true, metadata_only = false;
    vector<size_t> elements, chunks;
    for (int i = 2; i < argc; ++i) {
      string a = argv[i];
      if (a == "--partial-metadata") {
        full = false;
        metadata_only = true;
      } else if (i + 1 < argc) {
        string value = argv[++i];
        if (a == "--exact-root")
          trust.exact = sic_check::crt::trusted_hash(value);
        else if (a == "--file-root")
          trust.file = sic_check::crt::trusted_hash(value);
        else if (a == "--sha256")
          trust.whole = sic_check::crt::trusted_hash(value);
        else if (a == "--partial-element") {
          full = false;
          elements.push_back(sic_check::crt::number(value, 0, std::numeric_limits<size_t>::max()));
        } else if (a == "--partial-chunk") {
          full = false;
          chunks.push_back(sic_check::crt::number(value, 0, std::numeric_limits<size_t>::max()));
        } else
          throw sic_check::crt::UsageError("unknown option");
      } else
        throw sic_check::crt::UsageError("missing option argument");
    }
    if (metadata_only && (!elements.empty() || !chunks.empty()))
      throw sic_check::crt::UsageError(
          "--partial-metadata cannot be combined with element/chunk requests");
    Input in;
    in.open(argv[1], trust, full);
    if (!full) {
      for (size_t id : elements) in.element(id);
      for (size_t ci : chunks) in.verify_chunk(ci);
    }
    std::cout << in.scope.describe() << "\nexact root " << hex(in.exact_root) << "\nfile root "
              << hex(in.merkle) << "\n";
    return 0;
  } catch (const sic_check::crt::UsageError& e) {
    std::cerr << e.what() << '\n';
    return 2;
  } catch (const Refuse& e) {
    std::cerr << e.what() << '\n';
    return 3;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
