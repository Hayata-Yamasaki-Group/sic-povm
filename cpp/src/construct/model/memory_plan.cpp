// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/model/memory_plan.hpp"

#include "sic/error.hpp"
#include "support/admitted_worker_team.hpp"

namespace sic::construct {
MemoryControl memory_control() {
#ifndef SIC_ENABLE_TEST_HOOKS
  return MemoryControl::automatic;
#else
  const auto enabled = [](const char* key) {
    const char* value = sic::support::kernel_setting(key);
    if (value && std::string_view(value) != "0" && std::string_view(value) != "1")
      throw sic::Error(sic::ErrorKind::invalid_argument, std::string(key) + " must be 0 or 1");
    return value && std::string_view(value) == "1";
  };
  const bool keep = enabled("SIC_DEV_KEEP_ALL"), spill = enabled("SIC_DEV_SPILL");
  if (keep && spill)
    throw sic::Error(sic::ErrorKind::invalid_argument, "conflicting developer memory controls");
  return keep ? MemoryControl::keep_all : spill ? MemoryControl::spill : MemoryControl::automatic;
#endif
}

void MemoryPlanner::IoWindow::reset(Clock::time_point now, Bytes bytes,
                                    const std::optional<IoPressure>& pressure) {
  initialized = true;
  started = last_seen = now;
  baseline_bytes = last_bytes = bytes;
  baseline_pressure = last_pressure = pressure;
}

MemoryPlanner::IoFeedback MemoryPlanner::IoWindow::observe(
    Clock::time_point now, Bytes bytes, const std::optional<IoPressure>& pressure,
    double& cost_rate) {
  IoFeedback unknown;
  if (pressure) unknown.scope = pressure->scope;
  const bool comparable =
      (!last_pressure && !pressure) ||
      (last_pressure && pressure && last_pressure->scope == pressure->scope &&
       pressure->some_us >= last_pressure->some_us && pressure->full_us >= last_pressure->full_us);
  if (!initialized || now < last_seen || bytes < last_bytes || !comparable) {
    reset(now, bytes, pressure);
    return unknown;
  }
  last_seen = now;
  last_bytes = bytes;
  last_pressure = pressure;
  const double seconds = std::chrono::duration<double>(now - started).count();
  if (seconds < 1) return unknown;
  const auto feedback = MemoryPlanner::io_feedback(baseline_pressure, pressure, seconds,
                                                   bytes - baseline_bytes, cost_rate);
  reset(now, bytes, pressure);
  return feedback;
}

MemoryPlanner::MemoryPlanner(const sic::support::resources::Budget& budget, MemoryControl control)
    : budget_(budget), control_(control) {
  const auto bytes = io_bytes();
  const auto pressure = read_io_pressure();
  io_window_.reset(IoWindow::Clock::now(), bytes, pressure);
}

sic::support::resources::Bytes MemoryPlanner::PhaseReservation::future() const {
  if (present > structural) throw std::logic_error("phase ownership credit exceeds model");
  return structural - present;
}

void MemoryPlanner::reserve(Bytes total) {
  budget_.require("adaptive resident plus future workspace", total);
  reservation_ = total;
  reservation_anchor_ = retained_;
  reservation_valid_ = true;
}

sic::support::resources::Bytes MemoryPlanner::reserved() const {
  if (!reservation_valid_ || retained_ < reservation_anchor_)
    throw std::logic_error("missing adaptive phase reservation");
  return sic::support::resources::add(reservation_, retained_ - reservation_anchor_);
}

Retention MemoryPlanner::decide(std::string_view name, const MemoryItem& item, Bytes mandatory,
                                std::optional<Bytes> client_cap, bool anchored) {
  const auto current = sic::support::resources::inspect();
  const Bytes cap = client_cap ? std::min(budget_.limit(), *client_cap) : budget_.limit();
  const Bytes base =
      sic::support::resources::add(std::max(current.rss, mandatory), anchored ? 0 : retained_);
  const Bytes available = cap > base ? cap - base : 0;
  budget_.require("adaptive mandatory phase and retained items", base);
  const auto choice = choose(control_, item, available, io_bytes_per_second_);
  if (choice == Retention::keep) {
    if (sic::support::resources::add(base, item.bytes) > cap)
      throw sic::support::resources::AdmissionError("retained memory item exceeds client budget");
    budget_.require("adaptive retained item and phase workspace",
                    sic::support::resources::add(base, item.bytes));
    retained_ = sic::support::resources::add(retained_, item.bytes);
  }
  std::printf("memory item: %.*s plan=%s bytes=%llu mandatory=%llu retained=%llu budget=%llu\n",
              (int)name.size(), name.data(), label(choice), (unsigned long long)item.bytes,
              (unsigned long long)mandatory, (unsigned long long)retained_,
              (unsigned long long)cap);
  return choice;
}

const char* MemoryPlanner::label(Retention choice) {
  switch (choice) {
    case Retention::keep:
      return "keep";
    case Retention::spill:
      return "spill";
    case Retention::recompute:
      return "recompute";
  }
  throw std::logic_error("invalid memory plan");
}

Retention MemoryPlanner::choose(MemoryControl control, const MemoryItem& item, Bytes available,
                                double bandwidth) {
  if (control == MemoryControl::keep_all ||
      (control == MemoryControl::automatic && item.bytes <= available))
    return Retention::keep;
  if (item.can_spill && (!item.can_recompute || control == MemoryControl::spill ||
                         (double)item.bytes * (1.0 + item.reads) / std::max(1.0, bandwidth) <=
                             item.recompute_seconds))
    return Retention::spill;
  if (item.can_recompute) return Retention::recompute;
  if (!item.can_spill)
    throw sic::support::resources::AdmissionError("mandatory memory item does not fit");
  return Retention::spill;
}

sic::support::resources::Bytes MemoryPlanner::io_bytes() {
  Bytes total = 0;
  if (const auto text = sic::support::resources::read("/proc/self/io")) {
    std::istringstream stream(*text);
    for (std::string key, value; stream >> key >> value;)
      if (key == "read_bytes:" || key == "write_bytes:")
        total = sic::support::resources::add(total, sic::support::resources::integer(value));
  }
  return total;
}

std::optional<MemoryPlanner::IoPressure> MemoryPlanner::parse_io_pressure(std::string_view text,
                                                                          std::string scope) {
  std::optional<sic::support::resources::Microseconds> some, full;
  std::istringstream lines{std::string(text)};
  try {
    for (std::string line; std::getline(lines, line);) {
      std::istringstream fields(line);
      std::string kind, token;
      fields >> kind;
      while (fields >> token)
        if (token.starts_with("total=")) {
          const auto total = sic::support::resources::integer(std::string_view(token).substr(6));
          if (kind == "some") some = total;
          if (kind == "full") full = total;
        }
    }
  } catch (const std::exception&) {
    return {};
  }
  if (!some || !full) return {};
  return IoPressure{std::move(scope), *some, *full};
}

std::optional<MemoryPlanner::IoPressure> MemoryPlanner::read_io_pressure() {
#if defined(__linux__)

  std::vector<std::filesystem::path> candidates;
  const auto membership = sic::support::resources::read("/proc/self/cgroup"),
             mounts = sic::support::resources::read("/proc/self/mountinfo");
  if (membership && mounts) {
    try {
      const auto paths = sic::support::resources::cgroup_ancestors(*membership, *mounts);
      if (!paths.empty()) {
        candidates.push_back(paths.front() / "io.pressure");
      }
    } catch (const std::exception&) { /* Optional signal; memory limits are checked separately. */
    }
  }
  candidates.emplace_back("/proc/pressure/io");
  return load_io_pressure(candidates,
                          [](const auto& path) { return sic::support::resources::read(path); });
#endif
  return {};
}

MemoryPlanner::IoFeedback MemoryPlanner::io_feedback(const std::optional<IoPressure>& previous,
                                                     const std::optional<IoPressure>& current,
                                                     double seconds, Bytes completed_bytes,
                                                     double& cost_rate) {
  IoFeedback result;
  if (current) result.scope = current->scope;
  if (!std::isfinite(seconds) || seconds < 1) return result;
  if (completed_bytes >= sic::support::resources::kMib)
    cost_rate = .75 * cost_rate + .25 * completed_bytes / seconds;
  if (!previous || !current || previous->scope != current->scope ||
      current->some_us < previous->some_us || current->full_us < previous->full_us)
    return result;
  const double elapsed_us = seconds * 1e6;
  result.known = true;
  result.some_fraction = std::min(1.0, (current->some_us - previous->some_us) / elapsed_us);
  result.full_fraction = std::min(1.0, (current->full_us - previous->full_us) / elapsed_us);
  result.pressure =
      result.full_fraction >= .10 || (result.some_fraction >= .50 && result.full_fraction >= .05);
  return result;
}

int MemoryPlanner::adjust(std::string_view phase, int workers, Bytes per_worker,
                          const std::function<void()>& release_caches) {
  const Bytes io = io_bytes();
  const auto stalls = read_io_pressure();
  const auto feedback =
      io_window_.observe(IoWindow::Clock::now(), io, stalls, io_bytes_per_second_);
  return apply_runtime(phase, workers, per_worker, release_caches, feedback, io);
}

int MemoryPlanner::apply_runtime(std::string_view phase, int workers, Bytes per_worker,
                                 const std::function<void()>& release_caches,
                                 const IoFeedback& feedback, Bytes io) {
  if (phase != last_phase_) {
    // Completed phases leave allocator pages behind after releasing scratch. Return
    // those free pages before measuring headroom for the next phase's live buffers.
    sic::support::release_local_worker_teams();
    sic::support::resources::release_free_heap();
    last_phase_.assign(phase);
  }
  auto current = sic::support::resources::inspect();
  const bool pressure = current.rss > budget_.limit() / 10 * 8;
  if (pressure && release_caches) {
    release_caches();
    sic::support::resources::release_free_heap();
  }
  current = sic::support::resources::inspect();
  if (pressure || feedback.pressure) workers = std::max(1, workers / 2);
  workers = budget_.workers(workers, current.rss, std::max<Bytes>(1, per_worker));
  std::printf(
      "memory runtime: %.*s rss=%llu io=%llu pressure=%d io_pressure=%d workers=%d "
      "io_stall_known=%d io_some=%.4f io_full=%.4f io_scope=%s\n",
      (int)phase.size(), phase.data(), (unsigned long long)current.rss, (unsigned long long)io,
      pressure, feedback.pressure, workers, feedback.known, feedback.some_fraction,
      feedback.full_fraction, feedback.scope.empty() ? "unknown" : feedback.scope.c_str());
  return workers;
}

}  // namespace sic::construct

namespace sic::construct {
Retention MemoryPlanner::decide_reserved(std::string_view name, const MemoryItem& item) {
  return decide(name, item, reserved(), {}, true);
}
}  // namespace sic::construct
