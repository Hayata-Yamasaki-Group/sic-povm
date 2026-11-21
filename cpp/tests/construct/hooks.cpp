// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdlib>
#include <iostream>

#include "construct/components/components.hpp"
#include "construct/model/memory_plan.hpp"
#include "construct/model/options.hpp"
#include "construct/model/run_services.hpp"
#include "construct/runtime/construction.hpp"
#include "exact/arithmetic/qk.hpp"

namespace {
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
constexpr const char* kKeys[] = {"SIC_CLASS_THREADS",
                                 "SIC_DEV_MEMORY_MB",
                                 "SIC_DEV_KEEP_ALL",
                                 "SIC_DEV_SPILL",
                                 "SIC_DIRECT_ETA_TEST_FAULT",
                                 "SIC_TEST_NEWTON_DEFER_CLASS",
                                 "SIC_TEST_NEWTON_DEFER_ENTRY_AFTER_HINT",
                                 "SIC_TEST_REP_BAND_BITS",
                                 "SIC_TEST_FAIL_SIC_CHECK",
                                 "SIC_TEST_KILL_AFTER_WRITE",
                                 "SIC_TEST_PRECISION_RETRY",
                                 "SIC_TEST_CEIL1_FORCE_PRECISION_REFUSAL",
                                 "SIC_FAULT_KILL_AFTER_CHUNKS",
                                 "SIC_FAULT_KILL_AT_BYTE",
                                 "SIC_TEST_ABORT_AFTER_PARTS"};
}  // namespace

int main(int argc, char** argv) {
  using namespace sic::construct;
  if (argc == 3 && std::string(argv[1]) == "ambient") {
    ProducerOptions options(static_cast<unsigned>(std::stoul(argv[2])));
    return run(options, {}, {});
  }
  // Evidence driver for the real embedded cache/resume API, with ordinary
  // explicit storage settings. No developer selector is needed in OFF mode.
  if (argc == 5) {
    const std::string root = argv[1];
    sic::support::KernelSettings cache_settings{{"SIC_OUT", argv[2]},
                                                {"SIC_THREADS", "2"},
                                                {"SIC_CLASS_THREADS", "2"},
                                                {"SIC_ENTRY_ARB", "1"},
                                                {"SIC_ORDER", "standard"},
                                                {"SIC_MEMORY_MB", "2048"},
                                                {"SIC_GROUP_CACHE", root + "/group.bin"},
                                                {"SIC_ANALYTIC_CACHE", root + "/analytic"},
                                                {"SIC_COMPLETED_CLASS_CACHE", root + "/completed"},
                                                {"SIC_PHASED_STORAGE", "1"},
                                                {"SIC_STREAM_ACTIONS", "1"},
                                                {"SIC_STREAM_REPS", "1"},
                                                {"SIC_STREAM_STEPS", "1"},
                                                {"SIC_COMPACT_WORK", "1"},
                                                {"SIC_STREAM_TABLES", "1"},
                                                {"SIC_ROOT_BACKING", "1"},
                                                {"SIC_NEWTON_MEMORY_OWNERS", "1"},
                                                {"SIC_NEWTON_PHASE_MEMORY", "1"},
                                                {"SIC_SPOOL_LATTICE", "1"},
                                                {"SIC_REP_BAND_BITS", "0"}};
    if (std::string(argv[3]) == "required") cache_settings["SIC_REQUIRE_COMPLETED_CACHE"] = "1";
    if (std::string(argv[4]) == "resume") cache_settings["SIC_SICX_RESUME"] = "1";
    ProducerOptions options(4, {}, {}, cache_settings);
    return run(options, {}, {});
  }
  require(argc == 1, "unexpected hook fixture arguments");
  for (const char* key : kKeys) unsetenv(key);
  const ProducerOptions clean(2);
  // Process-only writer/transport faults must not change the authenticated resume plan.
  for (const char* key :
       {"SIC_FAULT_KILL_AFTER_CHUNKS", "SIC_FAULT_KILL_AT_BYTE", "SIC_TEST_ABORT_AFTER_PARTS"}) {
    setenv(key, "1", 1);
    const ProducerOptions fault(2);
    require(fault.plan_environment() == clean.plan_environment(),
            "process-only fault changed the resume plan");
    unsetenv(key);
  }
  sic::support::KernelSettings settings{{"SIC_NEWTON_MATRIX_BITS", "777"},
                                        {"SIC_MEMORY_MB", "256"},
                                        {"SIC_DEV_MEMORY_MB", "64"},
                                        {"SIC_DEV_SPILL", "1"}};
  {
    sic::support::KernelSettingsScope scope(&settings);
    sic::support::resources::Budget budget;
#ifdef SIC_ENABLE_TEST_HOOKS
    require(budget.user_cap == 64 * sic::support::resources::kMib, "ON developer cap");
    require(memory_control() == MemoryControl::spill, "ON spill selector");
#else
    require(budget.user_cap == 256 * sic::support::resources::kMib, "OFF real cap");
    require(memory_control() == MemoryControl::automatic, "OFF developer selector");
#endif
  }
  for (const char* value : {"1", "", "invalid"}) {
    for (const char* key : kKeys) {
      setenv(key, value, 1);
      settings[key] = value;
    }
    const ProducerOptions explicit_options(2, {}, {}, settings);
    const ProducerOptions ambient(2);
    // Matrix precision remains an internal typed/explicit library setting.
    require(explicit_options.matrix_bits.get() == 777, "explicit matrix precision lost");
#ifndef SIC_ENABLE_TEST_HOOKS
    require(ambient.plan_environment() == clean.plan_environment(), "ambient plan polluted");
    for (const char* key : kKeys) {
      require(!ambient.raw(key) && !explicit_options.raw(key), "OFF hook retained");
      require(!ambient.kernel_settings().contains(key) &&
                  !explicit_options.kernel_settings().contains(key),
              "OFF settings polluted");
      for (const auto& item : explicit_options.plan_environment())
        require(!item.starts_with(std::string(key) + "="), "explicit plan polluted");
    }
    sic::exact::QuadraticContext context{5};
    sic::exact::QuadraticNumber x(sic::exact::Rational(0), sic::exact::Rational(1));
    require(sic::exact::ceil1(context, x) == 3, "OFF ambient ceil fault activated");
#else
    require(ambient.raw(kKeys[0]) && explicit_options.raw(kKeys[0]), "ON hook filtered");
#endif
    for (const char* key : kKeys) unsetenv(key);
  }
  std::cout << "PASS effective hook settings, caps, selectors, ceil and plan isolation\n";
}
