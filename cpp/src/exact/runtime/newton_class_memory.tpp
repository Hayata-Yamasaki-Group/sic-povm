// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact::phase_memory {
template <class Requirement, class Refused, class Callback>
void dispatch_callbacks(std::span<const long> classes, int workers,
                        sic::support::Reservations* pool, Requirement requirement, Refused refused,
                        Callback callback) {
  std::vector<std::optional<sic::support::Reservations::Lease>> grants(classes.size());
  if (pool)
    for (size_t q = 0; q < classes.size(); ++q) {
      sic::support::resources::Bytes bytes;
      try {
        bytes = requirement(classes[q]);
      } catch (const std::overflow_error&) {
        refused(classes[q], std::numeric_limits<sic::support::resources::Bytes>::max());
        continue;
      }
      if (!bytes) continue;
      grants[q] = pool->try_acquire(bytes);
      if (!grants[q]) refused(classes[q], bytes);
    }
  parallel_for(classes.size(), workers, [&](long q) {
    if (pool && !grants[q]) return;
    callback(classes[q], std::move(grants[q]));
  });
}

template <class Outcome, class Retry>
void drain_callbacks(std::span<const long> classes, Outcome outcome, Retry retry) {
  for (long cls : classes)
    if (outcome(cls) == ClassOutcome::resource_deferred) retry(cls);
}

}  // namespace sic::exact::phase_memory

namespace sic::exact::phase_memory {
template <class Seed>
PreparedSeed<Seed>& PreparedSeed<Seed>::operator=(PreparedSeed&& other) noexcept {
  if (this != &other) {
    reset();
    grant_ = std::move(other.grant_);
    seed_ = std::move(other.seed_);
  }
  return *this;
}

template <class Seed>
void PreparedSeed<Seed>::reset() noexcept {
  seed_.reset();
  grant_.reset();
}

template <class Seed>
void PreparedSeed<Seed>::assign(std::unique_ptr<Seed> seed,
                                std::optional<sic::support::Reservations::Lease> grant) {
  reset();
  grant_ = std::move(grant);
  seed_ = std::move(seed);
}

template <class Seed>
bool PreparedSeed<Seed>::try_grow(sic::support::resources::Bytes bytes) {
  return grant_ && (bytes <= grant_->bytes() || grant_->try_resize(bytes));
}

}  // namespace sic::exact::phase_memory
