// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/resources/platform_snapshot.hpp"
#include "decode/pool.hpp"

#include "decode/placement.hpp"

namespace sic::decode {
Pool::Pool(unsigned workers, const Placement& placement)
    : place_(placement), workers_(std::max(1u, workers)) {
  const Placement& place = place_;
  // Groups: maximal runs of consecutive workers on the same node.
  group_of_.resize(workers_);
  for (unsigned t = 0; t < workers_; ++t) {
    if (t == 0 || place.node(t, workers_) != place.node(t - 1, workers_)) ++groups_;
    group_of_[t] = groups_ - 1;
  }
  physical_workers_ = physical_workers(place_, workers_);
  ranges_ = std::vector<Range>(groups_);
  if (!inside()) initialize();
}

void Pool::initialize() {
  if (initialized_) return;
  CPU_ZERO(&owner_mask_);
  owner_mask_saved_ = sched_getaffinity(0, sizeof owner_mask_, &owner_mask_) == 0;
  if (!place_.bind(0, workers_)) bind_failures_.fetch_add(1);
  try {
    team_.run(
        workers_,
        [&](unsigned t) {
          if (t && !place_.bind(t, workers_)) bind_failures_.fetch_add(1);
        },
        {}, sic::support::WorkerTeam::CacheLifetime::retained);
    initialized_ = true;
  } catch (...) {
    shutdown();
    throw;
  }
}

void Pool::shutdown() {
  team_.shutdown();
  // Worker FLINT caches and decoder-owned arrays are now released. Return
  // their free pages before another decoder creates a fresh worker team.
  sic::support::resources::release_free_heap();
  if (owner_mask_saved_ && place_.pin) sched_setaffinity(0, sizeof owner_mask_, &owner_mask_);
}

size_t Pool::group_size(size_t count) const {
  const size_t groups = 4 * static_cast<size_t>(workers_);
  return std::max<size_t>(1, count / groups + (count % groups != 0));
}

size_t Pool::compute_group_size(size_t count, size_t minimum) const {
  const size_t width = std::max<size_t>(1, physical_workers_.size());
  return std::max(minimum, count / width + (count % width != 0));
}

void Pool::run_erased(size_t count, Job job, const void* body, bool physical) {
  initialize();
  const size_t participants =
      std::min<size_t>(count, physical ? physical_workers_.size() : workers_);
  const auto worker = [&](size_t q) {
    return physical ? physical_workers_[q] : static_cast<unsigned>(q);
  };
  if (busy_.exchange(true, std::memory_order_acquire))
    throw std::logic_error("Pool::run called concurrently");
  struct Busy {
    std::atomic<bool>& b;
    ~Busy() { b.store(false, std::memory_order_release); }
  } busy{busy_};
  {
    // The previous dispatch is joined, so the owner alone writes these fields.
    // Per-worker release publication below exposes them to the next dispatch.
    job_ = job;
    body_ = body;
    // Contiguous range per group, proportional to its workers; chunk size so
    // that each worker takes about four groups (at least one index).
    size_t begin = 0, before = 0;
    for (unsigned g = 0; g < groups_; ++g) {
      size_t members = 0;
      for (size_t q = 0; q < participants; ++q) members += group_of_[worker(q)] == g;
      const size_t end = count * (before + members) / participants;
      ranges_[g].next.store(begin, std::memory_order_relaxed);
      ranges_[g].end = end;
      begin = end;
      before += members;
    }
    grain_ = group_size(count);
    failed_.store(false, std::memory_order_relaxed);
    error_ = nullptr;
  }
  team_.run(
      static_cast<unsigned>(participants), [&](unsigned t) { work(t); },
      physical ? std::span<const unsigned>(physical_workers_.data(), participants)
               : std::span<const unsigned>{},
      sic::support::WorkerTeam::CacheLifetime::retained);
  job_ = nullptr;
  body_ = nullptr;
  auto error = std::exchange(error_, {});
  if (error) std::rethrow_exception(error);
}

void Pool::work(unsigned t) {
  WorkerScope scope(this, t);
  const unsigned home = group_of_[t];
  for (unsigned k = 0; k < groups_ && !failed_.load(std::memory_order_relaxed); ++k) {
    Range& r = ranges_[(home + k) % groups_];  // own group first, then steal
    for (;;) {
      if (failed_.load(std::memory_order_relaxed)) break;
      const size_t i = r.next.fetch_add(grain_, std::memory_order_relaxed);
      if (i >= r.end) break;
      const size_t last = std::min(r.end, i + grain_);
      try {
        sic::support::check_stop();
        for (size_t j = i; j < last; ++j) job_(body_, j);
      } catch (...) {
        std::lock_guard lock(mutex_);
        if (!error_) error_ = std::current_exception();
        failed_.store(true);
        break;
      }
    }
  }
}

uint64_t resident_bytes() {
  std::ifstream in("/proc/self/statm");
  uint64_t size = 0, resident = 0;
  const long page = sysconf(_SC_PAGESIZE);
  if (!(in >> size >> resident) || page <= 0 || resident > UINT64_MAX / static_cast<uint64_t>(page))
    throw sic::Error(ErrorKind::resource, "cannot measure decoder resident memory");
  return resident * static_cast<uint64_t>(page);
}

bool MemoryBudget::try_reserve(uint64_t bytes) {
  std::lock_guard lock(mutex_);
  const bool admitted = fits(baseline_ + reserved_ + admitted_, bytes);
  sic::support::admission::record("decoder-reserve", "stage arrays", admitted ? "admit" : "refuse",
                                  baseline_ + reserved_ + admitted_, bytes, budget_);
  if (!admitted) return false;
  reserved_ += bytes;
  ++revision_;
  note_peak();
  return true;
}

void MemoryBudget::acquire(uint64_t bytes, unsigned worker, bool live_group) {
  std::unique_lock lock(mutex_);
  if (!fits(baseline_ + reserved_, bytes)) {
    sic::support::admission::record("decoder-task", "transient", "refuse", baseline_ + reserved_,
                                    bytes, budget_);
    throw sic::Error(ErrorKind::resource, "memory budget: a task exceeds the budget");
  }
  bool waited = false;
  for (;;) {
    if (fits(baseline_ + reserved_ + admitted_, bytes)) {
      // Production groups always refresh RSS, including the first owner.
      if (admitted_ == 0 && !live_group) break;
      const auto revision = revision_;
      const auto outstanding = admitted_;
      const auto granted = granted_;
      lock.unlock();
      const auto resident = resident_bytes();
      lock.lock();
      if (revision != revision_) continue;
      // Charge every owner active at capture plus every grant made during the
      // read, including owners already released into the reader/allocator cache.
      // This bounds growth after the sample without retrying on every grant.
      const auto growth = granted_ - granted;
      if (fits(baseline_ + reserved_ + admitted_, bytes) && fits(resident, outstanding) &&
          fits(resident + outstanding, growth) && fits(resident + outstanding + growth, bytes))
        break;
      if (!admitted_) {
        if (outstanding || growth) continue;  // completed owners: refresh before refusing
        throw sic::Error(ErrorKind::resource, "memory budget too small for live task/group peak");
      }
      ++rss_waits_;
      waited = true;
      released_.wait_for(lock, std::chrono::milliseconds(5));
      continue;
    }
    waited = true;
    released_.wait(lock);
  }
  if (bytes > UINT64_MAX - granted_)
    throw sic::Error(ErrorKind::resource, "memory budget grant counter overflow");
  const auto next_granted = granted_ + bytes;
  if (waited) ++waits_;
  sic::support::admission::record("decoder-task", "transient",
                                  waited ? "admit-after-wait" : "admit",
                                  baseline_ + reserved_ + admitted_, bytes, budget_);
  admitted_ += bytes;
  granted_ = next_granted;
  note_peak();
  if (worker < per_worker_.size()) {
    per_worker_[worker] += bytes;
    peak_worker_[worker] = std::max(peak_worker_[worker], per_worker_[worker]);
  }
}

void MemoryBudget::release(uint64_t bytes, unsigned worker) {
  {
    std::lock_guard lock(mutex_);
    admitted_ -= bytes;
    if (worker < per_worker_.size()) per_worker_[worker] -= bytes;
  }
  released_.notify_all();
}

ReserveGuard::ReserveGuard(MemoryBudget& b, uint64_t bytes, const char* what)
    : b_(b), bytes_(bytes) {
  if (!b_.try_reserve(bytes_)) {
    std::ostringstream message;
    message << "memory budget too small for " << what << ": needs " << (bytes_ >> 20)
            << " MiB more, " << (b_.available() >> 20) << " MiB of " << (b_.budget() >> 20)
            << " MiB available";
    throw sic::Error(ErrorKind::resource, message.str());
  }
}

ReserveGuard::~ReserveGuard() { b_.unreserve(bytes_); }

}  // namespace sic::decode

namespace sic::decode {
MemoryBudget::MemoryBudget(uint64_t budget, unsigned workers)
    : limit_(budget),
      budget_(budget - budget / 16),
      per_worker_(workers, 0),
      peak_worker_(workers, 0) {}
}  // namespace sic::decode

namespace sic::decode {
void MemoryBudget::rebase(uint64_t resident) {
  std::lock_guard lock(mutex_);
  baseline_ = resident;
  ++revision_;
  note_peak();
}
}  // namespace sic::decode

namespace sic::decode {
uint64_t MemoryBudget::fixed() const {
  std::lock_guard lock(mutex_);
  return baseline_ + reserved_;
}
}  // namespace sic::decode

namespace sic::decode {
uint64_t MemoryBudget::available() const {
  std::lock_guard lock(mutex_);
  const uint64_t used = baseline_ + reserved_ + admitted_;
  return budget_ > used ? budget_ - used : 0;
}
}  // namespace sic::decode

namespace sic::decode {
void MemoryBudget::unreserve(uint64_t bytes) {
  {
    std::lock_guard lock(mutex_);
    reserved_ -= bytes;
    ++revision_;
  }
  released_.notify_all();
}
}  // namespace sic::decode

namespace sic::decode {
uint64_t MemoryBudget::peak() const {
  std::lock_guard lock(mutex_);
  return peak_;
}
}  // namespace sic::decode

namespace sic::decode {
uint64_t MemoryBudget::waits() const {
  std::lock_guard lock(mutex_);
  return waits_;
}
}  // namespace sic::decode

namespace sic::decode {
uint64_t MemoryBudget::rss_waits() const {
  std::lock_guard lock(mutex_);
  return rss_waits_;
}
}  // namespace sic::decode

namespace sic::decode {
uint64_t MemoryBudget::peak_per_worker() const {
  std::lock_guard lock(mutex_);
  return peak_worker_.empty() ? 0 : *std::max_element(peak_worker_.begin(), peak_worker_.end());
}
}  // namespace sic::decode
