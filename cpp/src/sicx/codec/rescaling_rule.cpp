// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/rescaling_rule.hpp"

#include "sicx/codec/block.hpp"
#include "support/admitted_worker_team.hpp"
#include "support/sha256.hpp"

namespace sic::sicx {
#ifdef SIC_ENABLE_TEST_HOOKS
thread_local uint64_t rescaling_thread_starts = 0;
thread_local uint64_t rescaling_parallel_grain = kRescalingParallelGrain;
#endif
}  // namespace sic::sicx

namespace sic::sicx {
bool GeneratorRescaling::identity() const {
  for (auto j : shifts)
    if (j) return false;
  return true;
}
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t GeneratorRescaling::total() const {
  uint64_t s = 0;
  for (auto j : shifts) s += j;
  return s;
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string GeneratorRescaling::text() const {
  std::string s;
  for (size_t i = 0; i < shifts.size(); ++i) s += (i ? "," : "") + std::to_string(shifts[i]);
  return s;
}
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t own_weight(const ElementId& id, const std::vector<unsigned>& degrees) {
  if (id.kind == "rep") return 0;
  if (id.level >= degrees.size())
    fail(ErrorKind::invalid_format, "rescaling: element level outside the tower");
  const uint64_t p = degrees[id.level], c = p * (p - 1) / 2;
  if (id.kind == "wsq" || id.kind == "act") return 2;
  if (id.k < 0 || static_cast<uint64_t>(id.k) >= p) {
    if (id.kind == "actdelta") return c;
    fail(ErrorKind::invalid_format, "rescaling: element " + id.kind + " without a valid k");
  }
  const uint64_t k = static_cast<uint64_t>(id.k);
  if (id.kind == "fcoef") return p - k;
  if (id.kind == "actodd") return c + 1 - k;
  fail(ErrorKind::invalid_format, "rescaling: unknown element kind " + id.kind);
}
}  // namespace sic::sicx

namespace sic::sicx {
sic::support::Json rescaling_json(const GeneratorRescaling& r, const std::string& rule) {
  sic::support::Json j = sic::support::Json::object_value();
  j["rule"] = sic::support::Json::string_value(rule);
  j["shifts"] = sic::support::Json::integer_array(r.shifts);
  return j;
}
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t pair_val2(const fmpz* x, const fmpz* y) {
  uint64_t v = std::numeric_limits<uint64_t>::max();
  if (!fmpz_is_zero(x)) v = std::min<uint64_t>(v, fmpz_val2(x));
  if (!fmpz_is_zero(y)) v = std::min<uint64_t>(v, fmpz_val2(y));
  return v;
}
}  // namespace sic::sicx

namespace sic::sicx {
RescalingCollector::RescalingCollector(const Meta& m, ValuationPolicy policy)
    : policy_(policy), degrees_(m.degrees), width_(m.width), gain_(m.degrees.size(), 0) {}
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t RescalingCollector::resident_valuation_bytes() const {
  uint64_t count = all_.capacity();
  for (const auto& entry : own_) {
    if (entry.second.capacity() > UINT64_MAX - count)
      throw std::overflow_error("SICX: valuation capacity overflow");
    count += entry.second.capacity();
  }
  if (count > UINT64_MAX / sizeof(uint32_t))
    throw std::overflow_error("SICX: valuation capacity overflow");
  return count * sizeof(uint32_t);
}
}  // namespace sic::sicx

namespace sic::sicx {
void RescalingCollector::add(const ElementId& id, const LeafSource& source) {
  const unsigned n = id.level;
  if (n > degrees_.size()) fail(ErrorKind::invalid_format, "rescaling: bad element level");
  const uint64_t w = own_weight(id, degrees_);
  std::vector<uint32_t>* table;
  if (w) {
    auto& slot = own_[{n, w}];
    if (slot.empty()) {
      admit_table(width_[n]);
      slot.assign(width_[n], kNone);
    }
    table = &slot;
  } else {
    if (all_.empty()) {
      admit_table(width_.back());
      all_.assign(width_.back(), kNone);
    }
    table = &all_;
  }
  std::vector<uint64_t> gain(degrees_.size(), 0);
  source([&](uint64_t pos, const fmpz* x, const fmpz* y) {
    if (fmpz_is_zero(x) && fmpz_is_zero(y)) return;
    if (pos >= width_[n]) fail(ErrorKind::invalid_format, "rescaling: leaf outside its field");
    const uint64_t v = pair_val2(x, y);
    if (v >= kNone) fail(ErrorKind::invalid_format, "rescaling: 2-adic valuation out of range");
    uint32_t& slot = (*table)[pos];
    slot = std::min<uint32_t>(slot, static_cast<uint32_t>(v));
    uint64_t rest = pos;
    for (unsigned i = 0; i < n; ++i) {
      gain[i] += rest % degrees_[i];
      rest /= degrees_[i];
    }
    if (w) gain[n] += w;
  });
  for (size_t i = 0; i < gain.size(); ++i) gain_[i] += gain[i];
}
}  // namespace sic::sicx

namespace sic::sicx {
void RescalingCollector::valuations(
    const std::function<void(int64_t, uint64_t, std::span<const uint32_t>)>& visit) const {
  if (!all_.empty()) visit(-1, 0, all_);
  for (const auto& [key, values] : own_) visit(key.level, key.weight, values);
}
}  // namespace sic::sicx

namespace sic::sicx {
const std::vector<uint64_t>& RescalingCollector::gains() const { return gain_; }
}  // namespace sic::sicx

namespace sic::sicx {
GeneratorRescaling RescalingCollector::compute(unsigned threads,
                                               std::span<const uint64_t> ceilings) {
  if (!ceilings.empty() && ceilings.size() != degrees_.size())
    throw sic::Error(sic::ErrorKind::invalid_argument, "rescaling constraint shape");
  const size_t t = degrees_.size();
  GeneratorRescaling r;
  r.shifts.assign(t, 0);
  r.gain = gain_;
  threads = std::max(1u, threads);
  struct Table {
    int64_t n;
    uint64_t w;
    std::vector<uint32_t>* v;
  };  // n = -1: no own weight
  std::vector<Table> tables;
  if (!all_.empty()) tables.push_back({-1, 0, &all_});
  for (auto& [key, v] : own_) tables.push_back({static_cast<int64_t>(key.level), key.weight, &v});
  // weight a_s of index q of a table
  auto weight = [&](const Table& tb, unsigned s, uint64_t q) -> uint64_t {
    if (tb.n >= 0 && static_cast<int64_t>(s) >= tb.n)
      return static_cast<int64_t>(s) == tb.n ? tb.w : 0;
    return (q / width_[s]) % degrees_[s];
  };
  auto run = [&](uint64_t size, auto&& body) {
    uint64_t grain = kRescalingParallelGrain;
#ifdef SIC_ENABLE_TEST_HOOKS
    grain = rescaling_parallel_grain;
#endif
    const auto workers = static_cast<unsigned>(
        std::min<uint64_t>(threads, std::max<uint64_t>(1, size / std::max<uint64_t>(1, grain))));
    if (workers == 1 || sic::support::WorkerTeam::inside()) {
      body(0, 0, size);
      return;
    }
    auto& team = sic::support::local_worker_team();
#ifdef SIC_ENABLE_TEST_HOOKS
    const auto before = team.size();
#endif
    team.run(workers, [&](unsigned k) { body(k, size * k / workers, size * (k + 1) / workers); });
#ifdef SIC_ENABLE_TEST_HOOKS
    rescaling_thread_starts += team.size() - before;
#endif
  };
  std::vector<unsigned> order;
  for (size_t s = 0; s < t; ++s)
    if (gain_[s]) order.push_back(static_cast<unsigned>(s));
  std::stable_sort(order.begin(), order.end(),
                   [&](unsigned a, unsigned b) { return gain_[a] > gain_[b]; });
  for (unsigned s : order) {
    // J_s = min over leaves with a_s > 0 of floor(slack / a_s)
    uint64_t j = std::numeric_limits<uint64_t>::max();
    for (const auto& tb : tables) {
      std::vector<uint64_t> part(threads, std::numeric_limits<uint64_t>::max());
      run(tb.v->size(), [&](unsigned k, uint64_t begin, uint64_t end) noexcept {
        uint64_t m = std::numeric_limits<uint64_t>::max();
        for (uint64_t q = begin; q < end; ++q) {
          const uint32_t slack = (*tb.v)[q];
          if (slack == kNone) continue;
          const uint64_t a = weight(tb, s, q);
          if (a) m = std::min<uint64_t>(m, slack / a);
        }
        part[k] = m;
      });
      for (auto m : part) j = std::min(j, m);
    }
    if (j == std::numeric_limits<uint64_t>::max())
      fail(ErrorKind::invalid_format, "rescaling: level with gain has no constraint");
    if (!ceilings.empty()) j = std::min(j, ceilings[s]);
    r.shifts[s] = j;
    if (!j) continue;
    std::atomic<bool> underflow{false};
    for (const auto& tb : tables)
      run(tb.v->size(), [&](unsigned, uint64_t begin, uint64_t end) noexcept {
        for (uint64_t q = begin; q < end; ++q) {
          uint32_t& slack = (*tb.v)[q];
          if (slack == kNone) continue;
          const uint64_t a = weight(tb, s, q) * j;
          if (a > slack) {
            underflow = true;
            continue;
          }  // impossible by the choice of j
          slack = static_cast<uint32_t>(slack - a);
        }
      });
    if (underflow) throw std::logic_error("SICX: rescaling: internal slack underflow");
  }
  all_ = {};
  own_.clear();
  return r;
}
}  // namespace sic::sicx

namespace sic::sicx {
void RescalingCollector::admit_table(uint64_t count) {
  const auto bytes = policy_.admit(count, allocated_bytes_);
  allocated_bytes_ += bytes;
}
}  // namespace sic::sicx
