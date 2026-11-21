// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/analytic/direct_eta_rows.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "support/resources/allocation_forecast.hpp"

namespace sic::exact {
using sic::support::resources::add;
using sic::support::resources::mul;

DirectEtaRows::DirectEtaRows(Bytes capacity, size_t domains)
    : capacity_(capacity), domain_capacity_(capacity / std::max<size_t>(1, domains)) {
  if (!domains) throw std::invalid_argument("empty direct row domain count");
}

DirectEtaRows::Bytes DirectEtaRows::row_bytes(long length, slong precision, size_t domain_size) {
  if (length <= 0 || precision <= 0) throw std::invalid_argument("invalid direct row forecast");
  // Outward balls, retained mantissa capacities, node/string storage and allocator
  // padding. Transient DFT work remains in DirectEtaRoot::workspace_bytes().
  const Bytes ball = add(sic::support::resources::complex_bytes(add(precision, 128)), 128);
  return add(mul(length, ball), add(domain_size, sizeof(Key) + sizeof(Row) + 256));
}

DirectEtaRows::Bytes DirectEtaRows::window_bytes(long dimension, Count classes, slong precision) {
  if (dimension < 4 || (dimension & (dimension - 1)) || precision <= 0)
    throw std::invalid_argument("invalid direct row window");
  // At stratum k there are at most k*(d/k)=d keys, each of length d/k.
  // This bound includes all dyadic strata, all authenticated source classes and
  // 256 domain bytes. A fixed ceiling bounds projections even at d1024/d2048.
  Bytes all = 0;
  for (long k = 1; k < dimension; k *= 2)
    all = add(all, mul(dimension, row_bytes(dimension / k, precision, 256)));
  return std::min<Bytes>(sic::support::resources::kGib, mul(classes, all));
}

DirectEtaRows::Batch DirectEtaRows::prepare(const std::string& domain, long dimension,
                                            slong precision, const std::vector<Request>& requests) {
  if (clock_ > std::numeric_limits<Count>::max() - requests.size()) clear();
  Batch batch;
  batch.slots.resize(requests.size());
  for (auto& [key, row] : rows_) row.pinned = false;
  auto key_of = [&](const Request& r) {
    return Key{domain, dimension, precision, r.k, r.a0, r.gamma};
  };
  // Pin every current hit before evicting anything. No returned pointer can be
  // invalidated by another request in this batch, regardless of request order.
  for (const auto& r : requests) {
    const auto found = rows_.find(key_of(r));
    if (found != rows_.end()) found->second.pinned = true;
  }
  std::vector<decltype(rows_)::iterator> victims;
  Bytes domain_bytes = 0;
  for (auto it = rows_.begin(); it != rows_.end(); ++it)
    if (it->first.domain == domain) {
      domain_bytes = add(domain_bytes, it->second.charge);
      if (!it->second.pinned) victims.push_back(it);
    }
  std::sort(victims.begin(), victims.end(),
            [](auto a, auto b) { return a->second.used < b->second.used; });
  size_t next_victim = 0;
  for (size_t i = 0; i < requests.size(); ++i) {
    const auto& r = requests[i];
    auto key = key_of(r);
    auto found = rows_.find(key);
    if (found != rows_.end()) {
      ++batch.hits;
    } else {
      const auto charge = row_bytes(r.length, precision, domain.size());
      if (charge > domain_capacity_) continue;
      // A representative visits many classes before returning to this one.
      // Evict only within this class's quota: cycling through other classes
      // must not evict every useful row before its next representative call.
      while (charge > capacity_ - bytes_ || charge > domain_capacity_ - domain_bytes) {
        if (next_victim == victims.size()) break;
        const auto victim = victims[next_victim++];
        bytes_ -= victim->second.charge;
        domain_bytes -= victim->second.charge;
        rows_.erase(victim);
        ++batch.evictions;
      }
      if (charge > capacity_ - bytes_ || charge > domain_capacity_ - domain_bytes)
        continue;  // deterministic recomputation
      found = rows_.try_emplace(std::move(key)).first;
      found->second.charge = charge;
      bytes_ += charge;
      domain_bytes += charge;
    }
    found->second.pinned = true;
    found->second.used = ++clock_;
    batch.slots[i] = &found->second.values;
  }
  return batch;
}

void DirectEtaRows::settle() {
  for (auto it = rows_.begin(); it != rows_.end();) {
    auto& row = it->second;
    if (row.validated) {
      row.pinned = false;
      ++it;
      continue;
    }
    Bytes actual = add(it->first.domain.capacity(), sizeof(Key) + sizeof(Row) + 256);
    actual = add(actual, mul(row.values.v.capacity(), sizeof(acb_struct)));
    bool finite = row.values.size() != 0;
    for (const auto& value : row.values.v) {
      const auto allocated = acb_allocated_bytes(&value);
      if (allocated < 0) throw std::overflow_error("negative retained row allocation");
      actual = add(actual, add(allocated, 128));
      finite = finite && acb_is_finite(&value);
    }
    if (!finite || actual > row.charge) {
      bytes_ -= row.charge;
      it = rows_.erase(it);
    } else {
      row.pinned = false;
      row.validated = true;
      ++it;
    }
  }
}

void DirectEtaRows::clear() {
  rows_.clear();
  bytes_ = clock_ = 0;
}
}  // namespace sic::exact
