// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::decode {
template <class Body>
void Pool::run(size_t count, const Body& body, size_t inline_below, bool physical) {
  if (!count) return;
  if (current_pool() != this && std::this_thread::get_id() != owner_)
    throw std::logic_error("Pool::run requires its owner thread or a job of this pool");
  if (workers_ == 1 || count < inline_below || inside()) {
    WorkerScope scope(this, current_pool() == this ? worker_id() : 0);
    for (size_t i = 0; i < count; ++i) {
      sic::support::check_stop();
      body(i);
    }
    return;
  }
  run_erased(
      count, [](const void* context, size_t i) { (*static_cast<const Body*>(context))(i); }, &body,
      physical);
}
template <class Body>
void Pool::run_grouped(size_t count, const Body& body, size_t inline_below) {
  const size_t grain =
      count < inline_below ? std::max<size_t>(1, count) : compute_group_size(count);
  run(
      count / grain + (count % grain != 0),
      [&](size_t group) {
        const size_t first = group * grain;
        const size_t last = first + std::min(grain, count - first);
        for (size_t i = first; i < last; ++i) {
          sic::support::check_stop();
          body(i);
        }
      },
      2, true);
}
}  // namespace sic::decode
