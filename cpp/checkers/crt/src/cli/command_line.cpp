// SPDX-License-Identifier: AGPL-3.0-or-later
#include "command_line.hpp"

#include <cstdio>
#include <limits>

#include "../certificate/crt_certificate.hpp"
#include "../format/canonical_json.hpp"
#include "../runtime/diagnostics.hpp"
#include "../runtime/parallel_for.hpp"
#include "option_values.hpp"
namespace sic_check::crt {
using namespace sic_check::crt::sicx;
// log2(2^a + 2^b) <= max + 1

namespace {
constexpr Uint64 kDefaultSeed = 1;
constexpr Int64 kDefaultIntegralMaxDimension = 8192;

const string kUsage =
    "usage: sic-certify-crt FILE.sicx [options]\n"
    "Unconditional CRT exactness certificate: proves that every Galois word (hence every ray) of "
    "the multiplet\n"
    "stored in FILE.sicx is an exact Weyl-Heisenberg SIC fiducial (d = 2^s >= 4).\n"
    "options:\n"
    "  --exact-root HEX        expected trusted exact root (64 lowercase hex digits)\n"
    "  --file-root HEX         expected trusted file root\n"
    "  --sha256 HEX            expected trusted whole-file SHA-256\n"
    "  --threads T             worker threads (default " +
    std::to_string(kDefaultThreads) +
    ")\n"
    "  --seed S                seed for the choice of primes (default " +
    std::to_string(kDefaultSeed) +
    "; any distinct primes give "
    "a valid proof)\n"
    "  --bound-only            compute bounds and prime count using split/reconstruction primes; "
    "skip residual certificate primes\n"
    "  --integral-max-dim N    run Lemma I (integral generators) at levels with 2[F_j:K] <= N "
    "(default " +
    std::to_string(kDefaultIntegralMaxDimension) +
    ")\n"
    "  --no-shift-hints        ignore the EXMT generator_rescaling hints (start every level at J = "
    "0)\n"
    "  --primes K              run K primes instead of the bound (timing diagnostic; never a "
    "certificate: exit 3)\n"
    "  --prec P                initial ball precision in bits (default: max_bits + 192, doubled on "
    "failure)\n"
    "  --inject rep|act|conj   fault injection for negative-control tests (must fail)\n"
    "  --version               print canonical tool identity and exit 0\n"
    "  --help                  print this help\n"
    "exit codes: 0 proved (or --bound-only done), 1 failed/refused/error, 2 usage, 3 incomplete "
    "(--primes), 4 undetected fault injection\n";

}  // namespace

int run_command(int argc, char** argv) {
  for (int version_arg = 1; version_arg < argc; ++version_arg) {
    if (std::string(argv[version_arg]) == "--version") {
      if (argc != 2) {
        std::fputs("sic-certify-crt: --version requires no other arguments\n", stderr);
        return 2;
      }
      std::printf(
          "{\"name\":\"sic-certify-crt\",\"schema\":\"sic-tool-version/1.0\","
          "\"source_commit\":\"%s\",\"version\":\"1.0.0\"}\n",
          SIC_SOURCE_COMMIT);
      return 0;
    }
  }
  try {
    if (argc >= 2 && string(argv[1]) == "--help") {
      std::fputs(kUsage.c_str(), stdout);
      return 0;
    }
    if (argc < 2 || argv[1][0] == '-') {
      std::fputs(kUsage.c_str(), stderr);
      return 2;
    }
    string path = argv[1];
    Trust input_trust;
    string inject;
    g_threads = kDefaultThreads;
    Uint64 seed = kDefaultSeed;
    bool bound_only = false, no_hints = false;
    Int64 int_max_dim = kDefaultIntegralMaxDimension;
    Int64 force_primes = -1;
    slong prec0 = 0;
    for (int a = 2; a < argc; ++a) {
      const string s = argv[a];
      if (s == "--exact-root" && a + 1 < argc)
        input_trust.exact = trusted_hash(argv[++a]);
      else if (s == "--file-root" && a + 1 < argc)
        input_trust.file = trusted_hash(argv[++a]);
      else if (s == "--sha256" && a + 1 < argc)
        input_trust.whole = trusted_hash(argv[++a]);
      else if (s == "--threads" && a + 1 < argc)
        g_threads =
            static_cast<unsigned>(number(argv[++a], 1, std::numeric_limits<unsigned>::max()));
      else if (s == "--seed" && a + 1 < argc)
        seed = number(argv[++a], 0, UINT64_MAX);
      else if (s == "--bound-only")
        bound_only = true;
      else if (s == "--integral-max-dim" && a + 1 < argc)
        int_max_dim = number(argv[++a], 0, Uint64(1) << 31);
      else if (s == "--primes" && a + 1 < argc)
        force_primes = number(argv[++a], 1, Uint64(1) << 20);
      else if (s == "--prec" && a + 1 < argc)
        prec0 = number(argv[++a], 64, Uint64(1) << 20);
      else if (s == "--inject" && a + 1 < argc) {
        inject = argv[++a];
        if (inject != "rep" && inject != "act" && inject != "conj")
          throw UsageError("--inject requires rep, act or conj");
      } else if (s == "--no-shift-hints")
        no_hints = true;
      else if (s == "--help") {
        std::fputs(kUsage.c_str(), stdout);
        return 0;
      } else {
        std::fprintf(stderr, "unknown argument %s\n%s", s.c_str(), kUsage.c_str());
        return 2;
      }
    }
    if (bound_only && force_primes >= 0)
      throw UsageError("--primes cannot be combined with --bound-only");
    return certify(path, input_trust, inject, seed, bound_only, no_hints, int_max_dim, force_primes,
                   prec0);
  } catch (const UsageError& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 2;
  } catch (const std::exception& e) {
    std::printf("%s\n", e.what());
    return 1;
  }
}

}  // namespace sic_check::crt
