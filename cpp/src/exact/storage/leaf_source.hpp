// SPDX-License-Identifier: AGPL-3.0-or-later

// Opaque, bounded exact-leaf sources backed by private spools.
#pragma once
#include "leaf_store.hpp"

namespace sic::exact::storage {
using Count = sic::support::resources::Count;
using sic::exact::Leaf;
using sic::exact::LeafSpool;
namespace resources = sic::support::resources;
namespace detail {
struct PrivateState {
  std::shared_ptr<LeafSpool> spool;
  bool valid = true;
  pthread_t coordinator = pthread_self();
  PrivateState(bool compression, long D0)
      : spool(std::make_shared<LeafSpool>(true, compression, D0)) {}
  void check() const;
};
}  // namespace detail

class Private;
class Source {
  std::shared_ptr<detail::PrivateState> private_;
  LeafSpool::Ticket private_ticket_;
  friend class Private;

 public:
  Source() = default;
  template <class Census>
  void resident_metadata(Census& census) const;
  explicit operator bool() const { return bool(private_); }
  size_t count() const { return private_ticket_.count; }
  slong max_bits() const { return private_ticket_.max_bits; }
  Count reader_forecast(size_t = 0) const;
  class Cursor {
    std::shared_ptr<detail::PrivateState> private_;
    size_t count_ = 0, consumed_ = 0;
    std::unique_ptr<LeafSpool::Cursor> private_cursor_;
    bool failed_ = false;

   public:
    explicit Cursor(Source source, size_t start = 0);
    const Leaf& next();
  };
  template <class Consumer>
  void read_blocks(Consumer consume, size_t block_size = 4096) const;
};

class Private {
  std::shared_ptr<detail::PrivateState> state_;
  Source issue(LeafSpool::Ticket ticket);

 public:
  explicit Private(bool compression = true, long D0 = 0)
      : state_(std::make_shared<detail::PrivateState>(compression, D0)) {}
  Private(const Private&) = delete;
  Private& operator=(const Private&) = delete;
  ~Private() { invalidate(); }
  template <class Census>
  void resident_metadata(Census& census) const;
  void invalidate();
  Count bytes() const {
    state_->check();
    return state_->spool->bytes();
  }
  template <class Generate>
  Source append_generated(size_t count, Generate generate);
  Source concatenate(const std::vector<Source>& sources);
};

}  // namespace sic::exact::storage

#include "exact/storage/leaf_source.tpp"
