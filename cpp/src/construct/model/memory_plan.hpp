// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "sic/support/kernel_settings.hpp"
#include "support/resources.hpp"

namespace sic::construct {

// Execution decisions never enter the exact-data or resume identity. Costs are engineering
// estimates, not numerical bounds. The allocation gates remain authoritative.
enum class Retention { keep, spill, recompute };
enum class MemoryControl { automatic, keep_all, spill };

MemoryControl memory_control();

struct MemoryItem {
  sic::support::resources::Bytes bytes = 0;
  bool can_spill = true, can_recompute = false;
  double recompute_seconds = 0;
  unsigned reads = 1;
};

class MemoryPlanner {
  using Bytes = sic::support::resources::Bytes;

 public:
  struct IoPressure {
    std::string scope;
    sic::support::resources::Microseconds some_us = 0, full_us = 0;
  };
  struct IoFeedback {
    bool known = false, pressure = false;
    double some_fraction = 0, full_fraction = 0;
    std::string scope;
  };
  struct IoWindow {
    using Clock = std::chrono::steady_clock;
    bool initialized = false;
    Clock::time_point started{}, last_seen{};
    Bytes baseline_bytes = 0, last_bytes = 0;
    std::optional<IoPressure> baseline_pressure, last_pressure;

    void reset(Clock::time_point now, Bytes bytes, const std::optional<IoPressure>& pressure);
    IoFeedback observe(Clock::time_point now, Bytes bytes,
                       const std::optional<IoPressure>& pressure, double& cost_rate);
  };

 private:
  const sic::support::resources::Budget& budget_;
  MemoryControl control_;
  Bytes retained_ = 0;
  Bytes reservation_ = 0, reservation_anchor_ = 0;
  bool reservation_valid_ = false;
  std::string last_phase_;
  IoWindow io_window_;
  double io_bytes_per_second_ = 500.0 * sic::support::resources::kMib;

 public:
  explicit MemoryPlanner(const sic::support::resources::Budget& budget,
                         MemoryControl control = memory_control());
  MemoryControl control() const { return control_; }
  void reset() {
    retained_ = 0;
    reservation_valid_ = false;
  }
  Bytes retained() const { return retained_; }

  struct PhaseReservation {
    Bytes structural = 0, present = 0, owned = 0, rss = 0, committed = 0;
    Bytes future() const;
    Bytes resident() const {
      return std::max({rss, owned, sic::support::resources::add(present, committed)});
    }
    Bytes total() const { return sic::support::resources::add(resident(), future()); }
    Bytes unknown() const { return rss > present ? rss - present : 0; }
  };
  // Anchor at a quiescent preallocation boundary. Active scratch consumes this reservation;
  // it must not replace it, nor be added again merely because RSS rose during the phase.
  void reserve(Bytes total);
  Bytes reserved() const;
  Retention decide_reserved(std::string_view name, const MemoryItem& item);

  // A backed retry need not retain the previous dense numerical table as its seed.
  // Real source precision floors Newton's workspace and inverse arithmetic; changing
  // that metadata is unsafe. Rebuild the verified coarse seed when it is cheaper.
  static bool recompute_retry_seed(bool retry, bool backed,
                                   sic::support::resources::BitCount source_bits,
                                   sic::support::resources::BitCount coarse_bits,
                                   sic::support::resources::BitCount matrix_hint) {
    return retry && backed && source_bits > std::max(coarse_bits, matrix_hint);
  }

  // Reserve future scratch before retaining optional items. Charge retained items cumulatively,
  // including items not materialized yet. The deliberate RSS overlap is conservative.
  Retention decide(std::string_view name, const MemoryItem& item, Bytes mandatory,
                   std::optional<Bytes> client_cap = {}, bool anchored = false);

  static const char* label(Retention choice);
  static Retention choose(MemoryControl control, const MemoryItem& item, Bytes available,
                          double bandwidth);

  template <class Forecast>
  int phase_workers(int requested, Forecast&& forecast) const;

  static Bytes io_bytes();

  static std::optional<IoPressure> parse_io_pressure(std::string_view text, std::string scope);

  template <class Reader>
  static std::optional<IoPressure> load_io_pressure(const std::vector<std::filesystem::path>& paths,
                                                    Reader&& reader);

  static std::optional<IoPressure> read_io_pressure();

  // PSI totals measure stalled wall time, not traffic volume. Cost estimation is independent:
  // quiet compute intervals must not turn healthy I/O into a congestion signal.
  static IoFeedback io_feedback(const std::optional<IoPressure>& previous,
                                const std::optional<IoPressure>& current, double seconds,
                                Bytes completed_bytes, double& cost_rate);

  // Quiescent boundaries only: callbacks must preserve immutable backing or a deterministic
  // recomputation recipe. No worker may hold references to a released cache.
  int adjust(std::string_view phase, int workers, Bytes per_worker,
             const std::function<void()>& release_caches = {});

  int apply_runtime(std::string_view phase, int workers, Bytes per_worker,
                    const std::function<void()>& release_caches, const IoFeedback& feedback,
                    Bytes io);
};

}  // namespace sic::construct

#include "construct/model/memory_plan.tpp"
