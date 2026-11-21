// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact::storage {
template <class Census>
void Source::resident_metadata(Census& census) const {
  if (private_) {
    if (census.first(private_) && private_->spool && census.first(private_->spool))
      census.allocation(64 * 1024);
    census.dynamic(private_ticket_);
  }
}

template <class Consumer>
void Source::read_blocks(Consumer consume, size_t block_size) const {
  if (!block_size || block_size > 65536) throw std::runtime_error("invalid source read block");
  if (!private_) throw std::runtime_error("empty exact source");
  private_->check();
  const auto active = private_->spool;
  active->read_blocks(
      private_ticket_,
      [&](size_t first, std::span<Leaf> block) {
        if (!private_->valid) throw std::runtime_error("expired private attempt");
        consume(first, block);
        if (!private_->valid) throw std::runtime_error("expired private attempt");
      },
      block_size);
}

template <class Census>
void Private::resident_metadata(Census& census) const {
  if (census.first(state_) && state_->spool && census.first(state_->spool))
    census.allocation(64 * 1024);
}

template <class Generate>
Source Private::append_generated(size_t count, Generate generate) {
  state_->check();
  const auto state = state_;
  const auto active = state_->spool;
  auto ticket = active->append_generated(count, [&](size_t first, std::span<Leaf> block) {
    state->check();
    generate(first, block);
    state->check();
  });
  state->check();
  return issue(std::move(ticket));
}

}  // namespace sic::exact::storage
