// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/model/options.hpp"

#include <climits>

#include "sic/support/parse_number.hpp"
#include "support/resources/platform_snapshot.hpp"

namespace sic::construct {
namespace {
#ifndef SIC_ENABLE_TEST_HOOKS
bool ignored_hook(std::string_view key) {
  return key == "SIC_CLASS_THREADS" || key == "SIC_DEV_MEMORY_MB" || key == "SIC_DEV_KEEP_ALL" ||
         key == "SIC_DEV_SPILL" || key == "SIC_DIRECT_ETA_TEST_FAULT" ||
         key == "SIC_TEST_NEWTON_DEFER_CLASS" || key == "SIC_TEST_NEWTON_DEFER_ENTRY_AFTER_HINT" ||
         key == "SIC_TEST_REP_BAND_BITS" || key == "SIC_TEST_FAIL_SIC_CHECK" ||
         key == "SIC_TEST_KILL_AFTER_WRITE" || key == "SIC_TEST_PRECISION_RETRY" ||
         key == "SIC_TEST_CEIL1_FORCE_PRECISION_REFUSAL" || key == "SIC_FAULT_KILL_AFTER_CHUNKS" ||
         key == "SIC_FAULT_KILL_AT_BYTE" || key == "SIC_TEST_ABORT_AFTER_PARTS";
}
#endif
}  // namespace
ProducerOptions::Environment::Environment(
    std::optional<sic::support::KernelSettings> explicit_settings) {
  if (explicit_settings) {
    values = std::move(*explicit_settings);
#ifndef SIC_ENABLE_TEST_HOOKS
    std::erase_if(values, [](const auto& item) { return ignored_hook(item.first); });
#endif
    for (const auto& [key, value] : values) plan.push_back(key + "=" + value);
    return;
  }
  for (char** e = environ; *e; ++e) {
    const std::string kv = *e;
    if (kv.rfind("SIC_", 0) != 0) continue;
    const auto equal = kv.find('=');
    const std::string key = kv.substr(0, equal);
#ifndef SIC_ENABLE_TEST_HOOKS
    if (ignored_hook(key)) continue;
#endif
    // getenv returns the first value if the environment contains duplicate keys. The plan,
    // however, binds every original entry, including unknown keys and duplicates.
    if (equal != std::string::npos) values.try_emplace(key, kv.substr(equal + 1));
    if (key == "SIC_RAY" || key == "SIC_SHIFTS" || key == "SIC_EXACT_JSON" ||
        key == "SIC_PART_ASSET" || key == "SIC_FAULT_KILL_AFTER_CHUNKS" ||
        key == "SIC_FAULT_KILL_AT_BYTE" || key == "SIC_TEST_ABORT_AFTER_PARTS" ||
        key == "SIC_TEST_PRECISION_RETRY" || key == "SIC_TEST_FAIL_SIC_CHECK" ||
        key == "SIC_TEST_KILL_AFTER_WRITE" || key == "SIC_THREADS" ||
        key == "SIC_SELECTION_THREADS" || key == "SIC_CLASS_THREADS" ||
        key == "SIC_DEV_MEMORY_MB" || key == "SIC_DEV_KEEP_ALL" || key == "SIC_DEV_SPILL" ||
        key == "SIC_MEMORY_MB" || key == "SIC_PROGRESS" || key == "SIC_STOP_FILE" ||
        key == "SIC_SICX_RESUME" || key == "SIC_OUT" || key == "SIC_PHASED_STORAGE" ||
        key == "SIC_REP_BAND_BITS" || key == "SIC_TEST_REP_BAND_BITS" ||
        key == "SIC_ROOT_BACKING" || key == "SIC_NEWTON_MEMORY_OWNERS" ||
        key == "SIC_NEWTON_PHASE_MEMORY" || key == "SIC_SPOOL_LATTICE" ||
        key == "SIC_STREAM_ACTIONS" || key == "SIC_STREAM_REPS" || key == "SIC_STREAM_STEPS" ||
        key == "SIC_COMPACT_WORK" || key == "SIC_STREAM_TABLES")
      continue;
    plan.push_back(kv);
  }
  std::sort(plan.begin(), plan.end());
}

std::optional<bool> ProducerOptions::nonzero(const char* name) const {
  const char* value = raw(name);
  if (!value) return std::nullopt;
  return std::strcmp(value, "0") != 0;
}

std::optional<std::uint64_t> ProducerOptions::optional_unsigned(const char* name) const {
  const char* value = raw(name);
  if (!value) return std::nullopt;
  return sic::support::resources::integer(value, sic::ErrorKind::invalid_argument);
}

std::optional<int> ProducerOptions::optional_threads(const char* name) const {
  const char* value = raw(name);
  if (!value) return std::nullopt;
  return sic::support::resources::thread_count(value, name);
}

bool ProducerOptions::binary(const char* name) const {
  const char* value = raw(name);
  if (value && std::strcmp(value, "0") && std::strcmp(value, "1"))
    throw sic::Error(sic::ErrorKind::invalid_argument, std::string(name) + " must be 0 or 1");
  return value && std::strcmp(value, "1") == 0;
}

const char* ProducerOptions::raw(std::string_view name) const {
#ifndef SIC_ENABLE_TEST_HOOKS
  if (ignored_hook(name)) return nullptr;
#endif
  const auto found = environment_.values.find(name);
  return found == environment_.values.end() ? nullptr : found->second.c_str();
}

ProducerOptions::ProducerOptions(unsigned chosen_exponent, AlignmentOptions chosen_alignment,
                                 sic::construct::ProductionPolicy chosen_policy,
                                 std::optional<sic::support::KernelSettings> settings)
    : environment_(std::move(settings)),
      exponent(chosen_exponent),
      out(raw("SIC_OUT")),
      d2_radicals(raw("SIC_D2_RADICALS")),
      completed_cache_root(raw("SIC_COMPLETED_CLASS_CACHE")),
      group_cache(raw("SIC_GROUP_CACHE")),
      analytic_cache(raw("SIC_ANALYTIC_CACHE")),
      step_selection(raw("SIC_STEP_SELECTION")),
      save_step_selection(raw("SIC_SAVE_STEP_SELECTION")),
      stop_file(raw("SIC_STOP_FILE")),
      entry_arb(present("SIC_ENTRY_ARB")),
      entry_arb_one(one("SIC_ENTRY_ARB")),
      phase_one(one("SIC_PHASED_STORAGE")),
      stream_tables_one(one("SIC_STREAM_TABLES")),
      progress(present("SIC_PROGRESS")),
      memory_audit(one("SIC_NEWTON_MEMORY_AUDIT")),
      memory_owners(one("SIC_NEWTON_MEMORY_OWNERS")),
      phase_admission(one("SIC_NEWTON_PHASE_MEMORY")),
#ifdef SIC_ENABLE_TEST_HOOKS
      defer_entry(present("SIC_TEST_NEWTON_DEFER_ENTRY_AFTER_HINT")),
#endif
      ghost_debug(present("SIC_GHOST_DEBUG")),
      step_debug(present("SIC_STEP_DEBUG")),
      path_debug(present("SIC_PATH_DEBUG")),
      tower_debug(present("SIC_TDEBUG")),
      check_support_periods(present("SIC_CHECK_SUPPORT_PERIODS")),
      check_entry_groups(present("SIC_CHECK_ENTRY_GROUPS")),
      resume(one("SIC_SICX_RESUME")),
      phase(nonzero("SIC_PHASED_STORAGE")),
      stream_actions(nonzero("SIC_STREAM_ACTIONS")),
      stream_reps(nonzero("SIC_STREAM_REPS")),
      stream_steps(nonzero("SIC_STREAM_STEPS")),
      compact_work(nonzero("SIC_COMPACT_WORK")),
      root_backing(nonzero("SIC_ROOT_BACKING")),
      stream_tables(nonzero("SIC_STREAM_TABLES")),
      group_resident_entries(nonzero("SIC_GROUP_RESIDENT_ENTRIES")),
      spool_lattice(nonzero("SIC_SPOOL_LATTICE")),
      order([this] {
        const char* value = raw("SIC_ORDER");
        if (!value) return Order::automatic;
        if (std::strcmp(value, "standard") == 0) return Order::standard;
        if (std::strcmp(value, "maximal") == 0) return Order::maximal;
        return Order::invalid;
      }()),
      requested_threads{
          [this] { return sic::support::resources::requested_threads(raw("SIC_THREADS")); }},
      require_completed_cache{
          [this] { return CompletedCacheRequirement::parse(raw("SIC_REQUIRE_COMPLETED_CACHE")); }},
      generator_block{[this] { return unsigned_or("SIC_GENERATOR_BLOCK", 262144); }},
      first_pass_bits{[this] { return unsigned_or("SIC_FIRST_PASS_BITS", 128); }},
      matrix_bits{[this] { return optional_unsigned("SIC_NEWTON_MATRIX_BITS"); }},
      selection_threads{[this] { return optional_threads("SIC_SELECTION_THREADS"); }},
      class_threads{[this] { return optional_threads("SIC_CLASS_THREADS"); }},
#ifdef SIC_ENABLE_TEST_HOOKS
      defer_class{[this] {
        return unsigned_or("SIC_TEST_NEWTON_DEFER_CLASS",
                           std::numeric_limits<sic::support::resources::Count>::max());
      }},
#endif
      seed_a{[this] {
        const char* value = raw("SIC_SEED_A");
        return value ? sic::support::parse_number<double>(value, "SIC_SEED_A",
                                                          sic::ErrorKind::invalid_argument)
                     : 2.0;
      }},
      direct_eta_check{[this] { return binary("SIC_DIRECT_ETA_CHECK"); }},
#ifdef SIC_ENABLE_TEST_HOOKS
      direct_eta_fault{[this] {
        const char* value = raw("SIC_DIRECT_ETA_TEST_FAULT");
        if (!value) return std::string();
        const std::string fault = value;
        if (fault != "plan" && fault != "evaluate" && fault != "nonfinite" &&
            fault != "admission" && fault != "stop")
          throw sic::Error(
              sic::ErrorKind::invalid_argument,
              "SIC_DIRECT_ETA_TEST_FAULT must be plan, evaluate, nonfinite, admission or stop");
        return fault;
      }},
#endif
      rep_band_bits{[this] { return optional_unsigned("SIC_REP_BAND_BITS"); }},
      policy(std::move(chosen_policy)),
      alignment(std::move(chosen_alignment)) {
  if (!alignment.train || !alignment.candidates || alignment.sweeps < 0 ||
      alignment.train > policy.alignment_samples ||
      alignment.candidates > policy.alignment_samples ||
      static_cast<unsigned>(alignment.sweeps) > policy.alignment_sweeps)
    throw sic::Error(sic::ErrorKind::invalid_argument, "alignment exceeds production policy");
  (void)policy.dimension.checked(exponent);
}
}  // namespace sic::construct

namespace sic::construct {
bool ProducerOptions::present(const char* name) const { return raw(name) != nullptr; }
bool ProducerOptions::one(const char* name) const {
  const char* value = raw(name);
  return value && std::strcmp(value, "1") == 0;
}
std::uint64_t ProducerOptions::unsigned_or(const char* name, std::uint64_t fallback) const {
  const char* value = raw(name);
  return value ? sic::support::resources::integer(value, sic::ErrorKind::invalid_argument)
               : fallback;
}
}  // namespace sic::construct

namespace sic::construct {
void ProducerOptions::validate_settings() const {
  (void)requested_threads.get();
  (void)require_completed_cache.get();
  (void)selection_threads.get();
  (void)class_threads.get();
  (void)seed_a.get();
  (void)direct_eta_check.get();
  const auto range = [&](const char* key, uint64_t value, uint64_t low, uint64_t high) {
    if (value < low || value > high)
      throw sic::Error(sic::ErrorKind::invalid_argument,
                       std::string(key) + " is outside the supported range");
  };
  range("SIC_GENERATOR_BLOCK", generator_block.get(), 1, 1048576);
  range("SIC_FIRST_PASS_BITS", first_pass_bits.get(), 1, WORD_MAX / 16);
  if (const auto value = matrix_bits.get())
    range("SIC_NEWTON_MATRIX_BITS", *value, 64, WORD_MAX / 16);
  if (const auto value = rep_band_bits.get()) range("SIC_REP_BAND_BITS", *value, 0, 8);
  if (order == Order::invalid)
    throw sic::Error(sic::ErrorKind::invalid_argument, "SIC_ORDER must be standard or maximal");
  for (const char* key :
       {"SIC_PHASED_STORAGE", "SIC_STREAM_ACTIONS", "SIC_STREAM_REPS", "SIC_STREAM_STEPS",
        "SIC_COMPACT_WORK", "SIC_ROOT_BACKING", "SIC_STREAM_TABLES", "SIC_GROUP_RESIDENT_ENTRIES",
        "SIC_SPOOL_LATTICE", "SIC_SICX_RESUME", "SIC_NEWTON_MEMORY_AUDIT",
        "SIC_NEWTON_MEMORY_OWNERS", "SIC_NEWTON_PHASE_MEMORY"})
    (void)binary(key);
  if (const char* value = raw("SIC_MEMORY_MB")) (void)sic::support::resources::megabytes(value);
  if (const char* value = raw("SIC_KRAWCZYK_FIRST_BITS"))
    range("SIC_KRAWCZYK_FIRST_BITS",
          sic::support::resources::integer(value, sic::ErrorKind::invalid_argument), 2,
          WORD_MAX / 16);
  if (const char* value = raw("SIC_PARALLEL_GRAIN"))
    range("SIC_PARALLEL_GRAIN",
          sic::support::resources::integer(value, sic::ErrorKind::invalid_argument), 1, LONG_MAX);
#ifdef SIC_ENABLE_TEST_HOOKS
  (void)defer_class.get();
  (void)direct_eta_fault.get();
  for (const char* key : {"SIC_DEV_KEEP_ALL", "SIC_DEV_SPILL", "SIC_TEST_FAIL_SIC_CHECK"})
    (void)binary(key);
  if (binary("SIC_DEV_KEEP_ALL") && binary("SIC_DEV_SPILL"))
    throw sic::Error(sic::ErrorKind::invalid_argument, "conflicting developer memory controls");
  if (const char* value = raw("SIC_DEV_MEMORY_MB")) (void)sic::support::resources::megabytes(value);
  if (const char* value = raw("SIC_TEST_REP_BAND_BITS"))
    range("SIC_TEST_REP_BAND_BITS",
          sic::support::resources::integer(value, sic::ErrorKind::invalid_argument), 0, 8);
  for (const char* key :
       {"SIC_FAULT_KILL_AFTER_CHUNKS", "SIC_FAULT_KILL_AT_BYTE", "SIC_TEST_ABORT_AFTER_PARTS"})
    if (const char* value = raw(key))
      (void)sic::support::resources::integer(value, sic::ErrorKind::invalid_argument);
#endif
}
}  // namespace sic::construct
