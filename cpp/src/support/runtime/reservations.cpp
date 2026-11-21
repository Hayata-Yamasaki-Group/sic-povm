// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/runtime/reservations.hpp"

namespace sic::support {
Reservations::Lease& Reservations::Lease::operator=(Lease&& other) noexcept {
  if (this != &other) {
    release();
    state_ = std::move(other.state_);
    bytes_ = std::exchange(other.bytes_, 0);
  }
  return *this;
}

bool Reservations::Lease::try_resize(Bytes requested) {
  if (!state_) throw std::logic_error("resizing a moved memory lease");
  std::lock_guard lock(state_->mutex);
  if (requested > bytes_) {
    const Bytes extra = requested - bytes_;
    if (extra > state_->capacity - state_->used) return false;
    const Bytes next = state_->used + extra;
    if (state_->fresh_test && !state_->fresh_test(next)) return false;
    state_->used = next;
  } else {
    state_->used -= bytes_ - requested;
  }
  bytes_ = requested;
  return true;
}

void Reservations::Lease::release() noexcept {
  if (!state_) return;
  {
    std::lock_guard lock(state_->mutex);
    state_->used -= bytes_;
  }
  bytes_ = 0;
  state_.reset();
}

Reservations::Reservations(Bytes capacity, std::function<bool(Bytes)> fresh_test)
    : state_(std::make_shared<State>(capacity, std::move(fresh_test))) {
  if (!capacity) throw std::invalid_argument("zero memory reservation capacity");
}

std::optional<Reservations::Lease> Reservations::try_acquire(Bytes requested) {
  if (!requested) throw std::invalid_argument("zero initial memory reservation");
  std::lock_guard lock(state_->mutex);
  if (requested > state_->capacity - state_->used) return std::nullopt;
  const Bytes next = state_->used + requested;
  if (state_->fresh_test && !state_->fresh_test(next)) return std::nullopt;
  // Construct the owner before committing, even though shared_ptr copying
  // and the private Lease constructor are noexcept.
  Lease lease(state_, requested);
  state_->used = next;
  return std::optional<Lease>(std::move(lease));
}

}  // namespace sic::support

namespace sic::support {
Reservations::Bytes Reservations::used() const {
  std::lock_guard lock(state_->mutex);
  return state_->used;
}

}  // namespace sic::support
