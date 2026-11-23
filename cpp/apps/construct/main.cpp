#include "sic/support/tool_version.hpp"
// SPDX-License-Identifier: AGPL-3.0-or-later
// Native construction CLI; orchestration lives in sic::construct.
#include <cstdlib>
#include <string_view>

#include "construct/components/components.hpp"
#include "construct/model/options.hpp"
#include "construct/model/run_services.hpp"
#include "construct/runtime/command_line.hpp"
#include "construct/runtime/construction.hpp"
#include "construct/runtime/runtime.hpp"
#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace {
void configure_process_heap() {
#if defined(__GLIBC__)
  // Explicit allocator settings own the process policy, including empty values.
  for (char** entry = environ; *entry; ++entry)
    if (std::string_view(*entry).starts_with("MALLOC_")) return;
  if (const char* configured = std::getenv("GLIBC_TUNABLES")) {
    std::string_view remaining(configured);
    while (!remaining.empty()) {
      const auto end = remaining.find(':');
      if (remaining.substr(0, end).starts_with("glibc.malloc.")) return;
      if (end == std::string_view::npos) break;
      remaining.remove_prefix(end + 1);
    }
  }
  // glibc's explicit trim scans arena bins but leaves non-main top chunks.
  // Bound their retention without routing medium-sized temporaries through mmap.
  // Setting either threshold freezes adaptation; install the pair explicitly.
  // Only this standalone CLI opts in, before any construction threads start.
  constexpr int kHeapTrimThresholdBytes = 16 * 1024 * 1024;
  constexpr int kHeapMmapThresholdBytes = 32 * 1024 * 1024;
  if (mallopt(M_MMAP_THRESHOLD, kHeapMmapThresholdBytes) == 1)
    (void)mallopt(M_TRIM_THRESHOLD, kHeapTrimThresholdBytes);
#endif
}
}  // namespace

int main(int argc, char** argv) {
  sic::support::cli::prepare_stdout();
  if (const auto status = sic::support::cli::version_request(argc, argv, "sic-construct"))
    return *status;
  try {
    sic::construct::ProducerCommandLine command(argc, argv);
    if (command.list_orders)
      return sic::support::cli::finish_stdout(sic::support::cli::kSuccess, "sic-construct");
    command.install_kernel_options(
        environ);  // CLI owns process setup; library runs never mutate it.
    configure_process_heap();
    setvbuf(stdout, nullptr, _IOLBF, 0);
    sic::support::admission::LogScope logging(command.log_admissions);
    sic::construct::ProducerOptions options(command.exponent, command.alignment, command.policy);
    options.rays_out = command.rays_out;
    const int result = sic::construct::run(options, {}, {});
    return sic::support::cli::finish_stdout(
        result == 0 ? sic::support::cli::kSuccess : sic::support::cli::kFailure, "sic-construct");
  } catch (const std::exception& error) {
    return sic::support::cli::report(sic::construct::ProducerCommandLine::parser(), error,
                                     "sic-construct");
  }
}
