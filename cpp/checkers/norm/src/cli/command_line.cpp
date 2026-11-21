// SPDX-License-Identifier: AGPL-3.0-or-later
#include "command_line.hpp"

#include <charconv>
#include <cstdio>
#include <limits>

#include "../certificate/norm.hpp"
#include "../format/container.hpp"
#include "../runtime/diagnostics.hpp"
namespace norm = sic_check::norm;
namespace sic_check::norm {
namespace {
constexpr unsigned kDefaultThreads = 1;
constexpr slong kDefaultMaxBits = slong(1) << 27;
struct UsageError : std::runtime_error {
  using std::runtime_error::runtime_error;
};
uint64_t number(std::string_view value, uint64_t minimum, uint64_t maximum) {
  uint64_t result = 0;
  auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, 10);
  if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
      result < minimum || result > maximum)
    throw UsageError("expected unsigned decimal in " + std::to_string(minimum) + ".." +
                     std::to_string(maximum));
  return result;
}
}  // namespace

int run_command(int argc, char** argv) {
  for (int version_arg = argc - 1; version_arg > 0; --version_arg) {
    if (std::string(argv[version_arg]) == "--version") {
      if (argc != 2) {
        std::fputs("sic-certify-norm: --version requires no other arguments\n", stderr);
        return 2;
      }
      std::printf(
          "{\"name\":\"sic-certify-norm\",\"schema\":\"sic-tool-version/1.0\","
          "\"source_commit\":\"%s\",\"version\":\"1.0.0\"}\n",
          SIC_SOURCE_COMMIT);
      return 0;
    }
  }
  const std::string help =
      "usage: sic-certify-norm FILE.sicx [options]\n"
      "  --threads T             worker threads (default " +
      std::to_string(kDefaultThreads) +
      ")\n"
      "  --estimate              estimate the required precision without proving SIC identities\n"
      "  --leaf-estimate         estimate leaf heights without proving SIC identities\n"
      "  --ray I                 check only zero-based ray index I (default all)\n"
      "  --max-bits B            precision cap (default " +
      std::to_string(kDefaultMaxBits) +
      ")\n"
      "  --verify-input          verify the container only\n"
      "  --partial               verify directories and EXMT only\n"
      "  --partial-element ID    partially verify the requested element (repeatable)\n"
      "  --partial-chunk ID      partially verify the requested physical chunk (repeatable)\n"
      "  --exact-root HEX        expected trusted exact root\n"
      "  --file-root HEX         expected trusted file root\n"
      "  --sha256 HEX            expected trusted whole-file SHA-256\n"
      "  --max-dimension D       input dimension limit (default " +
      std::to_string(kDefaultMaxDimension) +
      ")\n"
      "  --max-field-dimension D input field dimension limit (default " +
      std::to_string(kDefaultMaxFieldDimension) +
      ")\n"
      "  --version               print canonical tool identity and exit 0\n"
      "  --help                  print help to stdout and exit 0\n";
  if (argc >= 2 && std::string(argv[1]) == "--help") {
    std::fputs(help.c_str(), stdout);
    return 0;
  }
  try {
    if (argc < 2 || argv[1][0] == '-') {
      std::fputs(help.c_str(), stderr);
      return 2;
    }
    std::string path = argv[1];
    unsigned threads = kDefaultThreads;
    bool estimate = false, leaf_estimate = false, verify_input = false;
    norm::ReadOptions input_options;
    std::optional<size_t> only_ray;
    slong max_bits = kDefaultMaxBits;
    for (int a = 2; a < argc; ++a) {
      const std::string s = argv[a];
      if (s == "--help") {
        std::fputs(help.c_str(), stdout);
        return 0;
      } else if (s == "--threads" && a + 1 < argc)
        threads = static_cast<unsigned>(number(argv[++a], 1, std::numeric_limits<unsigned>::max()));
      else if (s == "--verify-input")
        verify_input = true;
      else if (s == "--partial") {
        verify_input = true;
        input_options.partial = true;
      } else if ((s == "--exact-root" || s == "--file-root" || s == "--sha256") && a + 1 < argc) {
        norm::Hash hash;
        try {
          hash = norm::parse_hash(argv[++a]);
        } catch (const norm::InvalidInput& e) {
          throw UsageError(e.what());
        }
        if (s == "--exact-root")
          input_options.exact_root = hash;
        else if (s == "--file-root")
          input_options.file_root = hash;
        else
          input_options.whole_sha256 = hash;
      } else if ((s == "--partial-element" || s == "--partial-chunk" ||
                  s == "--max-field-dimension" || s == "--max-dimension") &&
                 a + 1 < argc) {
        uint64_t n = number(argv[++a], 0,
                            (s == "--partial-element" || s == "--partial-chunk")
                                ? std::numeric_limits<size_t>::max()
                                : UINT64_MAX);
        if (s == "--max-field-dimension")
          input_options.max_field_dimension = n;
        else if (s == "--max-dimension")
          input_options.max_dimension = n;
        else {
          verify_input = true;
          input_options.partial = true;
          if (s == "--partial-element")
            input_options.elements.push_back(size_t(n));
          else
            input_options.physical_chunks.push_back(size_t(n));
        }
      } else if (s == "--estimate")
        estimate = true;

      else if (s == "--leaf-estimate")
        leaf_estimate = true;
      else if (s == "--ray" && a + 1 < argc)
        only_ray = static_cast<size_t>(number(argv[++a], 0, std::numeric_limits<size_t>::max()));
      else if (s == "--max-bits" && a + 1 < argc)
        max_bits = number(argv[++a], 1, std::numeric_limits<slong>::max());
      else {
        std::fprintf(stderr, "unknown argument %s\n", s.c_str());
        return 2;
      }
    }
    return certify(path, threads, estimate, leaf_estimate, verify_input, input_options, only_ray,
                   max_bits);
  } catch (const UsageError& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 2;
  } catch (const std::exception& e) {
    std::printf("%s\n", e.what());
    return 3;
  }
}

}  // namespace sic_check::norm
