// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstring>
#include <exception>
#include <map>
#include <optional>

#include "construct/components/production_policy.hpp"
#include "exact/storage/completed_class_cache.hpp"
#include "sic/support/kernel_settings.hpp"
extern char** environ;
namespace sic::construct {
using sic::exact::CompletedCacheRequirement;
}
#include "construct/model/alignment_options.hpp"
namespace sic::construct {

// Parse once and preserve the result for startup validation and stage access.
template <class T>
class ParsedProducerOption {
  T value_{};
  std::exception_ptr error_;

 public:
  template <class Parse>
  explicit ParsedProducerOption(Parse parse);
  const T& get() const {
    if (error_) std::rethrow_exception(error_);
    return value_;
  }
};

struct ProducerOptions {
 private:
  struct Environment {
    std::map<std::string, std::string, std::less<>> values;
    std::vector<std::string> plan;

    explicit Environment(
        std::optional<sic::support::KernelSettings> explicit_settings = std::nullopt);
  } environment_;

  bool present(const char* name) const;
  bool one(const char* name) const;
  std::optional<bool> nonzero(const char* name) const;
  std::uint64_t unsigned_or(const char* name, std::uint64_t fallback) const;
  std::optional<std::uint64_t> optional_unsigned(const char* name) const;
  std::optional<int> optional_threads(const char* name) const;
  bool binary(const char* name) const;

 public:
  const char* raw(std::string_view name) const;
  // Validate setting syntax and context-free ranges before any construction work.
  void validate_settings() const;
  const sic::support::KernelSettings& kernel_settings() const { return environment_.values; }
  const std::vector<std::string>& plan_environment() const { return environment_.plan; }
  unsigned exponent;
  long initial_bits = 0;
  unsigned seed = 7;

  const char* out;
  const char* d2_radicals;
  const char* completed_cache_root;
  const char* group_cache;
  const char* analytic_cache;
  const char* step_selection;
  const char* save_step_selection;
  const char* stop_file;
  const bool entry_arb;
  const bool entry_arb_one;
  const bool phase_one;
  const bool stream_tables_one;
  const bool progress;
  const bool memory_audit;
  const bool memory_owners;
  const bool phase_admission;
#ifdef SIC_ENABLE_TEST_HOOKS
  const bool defer_entry;
#endif
  const bool ghost_debug;
  const bool step_debug;
  const bool path_debug;
  const bool tower_debug;
  const bool check_support_periods;
  const bool check_entry_groups;
  const bool resume;
  const std::optional<bool> phase;
  const std::optional<bool> stream_actions;
  const std::optional<bool> stream_reps;
  const std::optional<bool> stream_steps;
  const std::optional<bool> compact_work;
  const std::optional<bool> root_backing;
  const std::optional<bool> stream_tables;
  const std::optional<bool> group_resident_entries;
  const std::optional<bool> spool_lattice;

  enum class Order { automatic, standard, maximal, invalid };
  const Order order;

  const ParsedProducerOption<int> requested_threads;
  const ParsedProducerOption<CompletedCacheRequirement> require_completed_cache;
  const ParsedProducerOption<sic::support::resources::Count> generator_block;
  const ParsedProducerOption<sic::support::resources::BitCount> first_pass_bits;
  const ParsedProducerOption<std::optional<sic::support::resources::BitCount>> matrix_bits;
  const ParsedProducerOption<std::optional<int>> selection_threads;
  const ParsedProducerOption<std::optional<int>> class_threads;
#ifdef SIC_ENABLE_TEST_HOOKS
  const ParsedProducerOption<sic::support::resources::Count> defer_class;
#endif
  const ParsedProducerOption<double> seed_a;
  const ParsedProducerOption<bool> direct_eta_check;
#ifdef SIC_ENABLE_TEST_HOOKS
  const ParsedProducerOption<std::string> direct_eta_fault;
#endif
  const ParsedProducerOption<std::optional<sic::support::resources::BitCount>> rep_band_bits;

  sic::construct::ProductionPolicy policy;
  AlignmentOptions alignment;
  ProducerOptions(unsigned chosen_exponent, AlignmentOptions chosen_alignment = {},
                  sic::construct::ProductionPolicy chosen_policy = {},
                  std::optional<sic::support::KernelSettings> settings = std::nullopt);
  ProducerOptions(const ProducerOptions&) = delete;
  ProducerOptions& operator=(const ProducerOptions&) = delete;
};

}  // namespace sic::construct

#include "construct/model/options.tpp"
