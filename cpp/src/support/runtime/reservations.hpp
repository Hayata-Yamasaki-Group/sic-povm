// SPDX-License-Identifier: AGPL-3.0-or-later
// Owned class reservations. Not yet used by production admission.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

#include "support/resources/checked_bytes.hpp"

namespace sic::support {

// Capacity excludes separately admitted resident objects. Every participating
// owner must retain its grant until its allocations have been destroyed. Grow
// before allocating; shrink only after releasing storage. This pool cannot
// account for allocations made by code that does not participate in it.
//
// The optional fresh gate runs inside the grant mutex, before committing a
// larger aggregate obligation. It receives ALL grants, including obligations
// not allocated yet. It must not call this pool recursively. For example,
// Budget::require_additional(total) is conservative (it double-counts already
// allocated grants), while a bare RSS+requested check can overgrant.
// Each lease has one owner: mutation, moves, bytes() and destruction of that
// same lease must not race. A gate must own its captured context for as long
// as ANY lease survives the facade. It must not release/resize/move another
// lease from this pool either: those operations also acquire its mutex.
class Reservations {
  using Bytes = sic::support::resources::Bytes;
  struct State {
    std::mutex mutex;
    const Bytes capacity;
    Bytes used = 0;
    std::function<bool(Bytes)> fresh_test;
    State(Bytes cap, std::function<bool(Bytes)> gate)
        : capacity(cap), fresh_test(std::move(gate)) {}
  };

 public:
  class Lease {
   public:
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    Lease(Lease&& other) noexcept
        : state_(std::move(other.state_)), bytes_(std::exchange(other.bytes_, 0)) {}
    Lease& operator=(Lease&& other) noexcept;
    ~Lease() { release(); }

    Bytes bytes() const noexcept { return bytes_; }

    // Nonblocking refusal lets the caller tear down and requeue rather than
    // wait while holding memory required by another owner's next phase.
    bool try_resize(Bytes requested);

   private:
    friend class Reservations;
    std::shared_ptr<State> state_;
    Bytes bytes_ = 0;
    Lease(std::shared_ptr<State> state, Bytes bytes) noexcept
        : state_(std::move(state)), bytes_(bytes) {}
    void release() noexcept;
  };

  explicit Reservations(Bytes capacity, std::function<bool(Bytes)> fresh_test = {});
  Reservations(const Reservations&) = delete;
  Reservations& operator=(const Reservations&) = delete;

  std::optional<Lease> try_acquire(Bytes requested);
  Bytes used() const;
  Bytes capacity() const noexcept { return state_->capacity; }

 private:
  std::shared_ptr<State> state_;
};

}  // namespace sic::support
