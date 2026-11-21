// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/storage/leaf_source.hpp"

namespace sic::exact::storage::detail {
void PrivateState::check() const {
  if (!pthread_equal(coordinator, pthread_self()))
    throw std::runtime_error("private source requires its coordinator thread");
  if (!valid || !spool) throw std::runtime_error("expired private attempt");
}

}  // namespace sic::exact::storage::detail
namespace sic::exact::storage {
Count Source::reader_forecast(size_t) const {
  if (!*this) return 0;
  return sic::support::resources::leaf_block_bytes(std::min<size_t>(65536, count()), max_bits());
}

Source::Cursor::Cursor(Source source, size_t start)
    : private_(source.private_), count_(source.count()), consumed_(start) {
  if (!source || start > count_) throw std::runtime_error("invalid exact-source cursor");
  private_->check();
  private_cursor_ =
      std::make_unique<LeafSpool::Cursor>(*private_->spool, source.private_ticket_, start);
}

const Leaf& Source::Cursor::next() {
  if (failed_) throw std::runtime_error("failed exact-source cursor cannot resume");
  try {
    if (consumed_ == count_) throw std::runtime_error("exact-source cursor past end");
    private_->check();
    ++consumed_;
    return private_cursor_->next();
  } catch (...) {
    failed_ = true;
    throw;
  }
}

Source Private::issue(LeafSpool::Ticket ticket) {
  Source result;
  result.private_ = state_;
  result.private_ticket_ = std::move(ticket);
  return result;
}

void Private::invalidate() {
  if (!pthread_equal(state_->coordinator, pthread_self()))
    throw std::runtime_error("private invalidation requires its coordinator thread");
  state_->valid = false;
  state_->spool.reset();
}

Source Private::concatenate(const std::vector<Source>& sources) {
  state_->check();
  if (sources.size() > 1048576)
    throw sic::Error(sic::ErrorKind::resource, "private source composite limit");
  sic::support::resources::Budget().require_additional(
      "private source composite metadata",
      sic::support::resources::add(
          128, sic::support::resources::mul(2 * sizeof(LeafSpool::Ticket), sources.size())));
  std::vector<LeafSpool::Ticket> tickets;
  tickets.reserve(sources.size());
  for (const auto& source : sources) {
    if (source.private_ != state_)
      throw std::runtime_error("private exact-source attempt mismatch");
    tickets.push_back(source.private_ticket_);
  }
  return issue(state_->spool->concatenate(std::move(tickets)));
}

}  // namespace sic::exact::storage
