// SPDX-License-Identifier: AGPL-3.0-or-later
// Callback and prepared-seed ownership. Slot forecasts remain independent.
#pragma once

#include <span>

#include "parallel.hpp"
#include "support/runtime/reservations.hpp"

namespace sic::exact::phase_memory {

enum class ClassOutcome { numerical_failure, complete, resource_deferred, in_progress };
sic::support::resources::Bytes callback_workspace(sic::support::resources::Count dimension,
                                                  sic::support::resources::Bytes private_slot,
                                                  sic::support::resources::Bytes packed_output,
                                                  int workers, bool replay);

// The retained seed must die BEFORE its grant. Moving/replacing a pending
// record obeys the same rule, unlike default memberwise move assignment.
template <class Seed>
class PreparedSeed {
 public:
  PreparedSeed() = default;
  PreparedSeed(const PreparedSeed&) = delete;
  PreparedSeed& operator=(const PreparedSeed&) = delete;
  PreparedSeed(PreparedSeed&& other) noexcept
      : grant_(std::move(other.grant_)), seed_(std::move(other.seed_)) {}
  PreparedSeed& operator=(PreparedSeed&& other) noexcept;
  void reset() noexcept;
  void assign(std::unique_ptr<Seed> seed,
              std::optional<sic::support::Reservations::Lease> grant = {});
  explicit operator bool() const noexcept { return bool(seed_); }
  Seed* operator->() const noexcept { return seed_.get(); }
  Seed& operator*() const { return *seed_; }
  sic::support::resources::Bytes granted() const noexcept { return grant_ ? grant_->bytes() : 0; }
  bool try_grow(sic::support::resources::Bytes bytes);

 private:
  std::optional<sic::support::Reservations::Lease> grant_;
  std::unique_ptr<Seed> seed_;
};

// A callback's output may be allocated in a shared table whose lifetime is
// longer than the local grant. The caller must destroy/park that allocation
// before relinquishing the callback grant, on success AND exception paths.
template <class Cleanup>
class CallbackRollback {
 public:
  explicit CallbackRollback(Cleanup cleanup) : cleanup_(std::move(cleanup)) {}
  CallbackRollback(const CallbackRollback&) = delete;
  ~CallbackRollback() { cleanup_(); }

 private:
  Cleanup cleanup_;
};

// All grants are acquired before dispatch. Partial admission dispatches only
// granted entries; refusals are drained later without increasing the original
// native share. Unclaimed grants outlive parallel_for's worker completion if a
// callback throws. No callback is launched without its grant when pool != null.
template <class Requirement, class Refused, class Callback>
void dispatch_callbacks(std::span<const long> classes, int workers,
                        sic::support::Reservations* pool, Requirement requirement, Refused refused,
                        Callback callback);

// Exactly one drained attempt per deferred class in this batch. A second
// refusal remains typed and may enter separately admitted fallback; there is
// no wait for another owner's memory and no unbounded unchanged retry loop.
template <class Outcome, class Retry>
void drain_callbacks(std::span<const long> classes, Outcome outcome, Retry retry);

}  // namespace sic::exact::phase_memory

#include "exact/runtime/newton_class_memory.tpp"
