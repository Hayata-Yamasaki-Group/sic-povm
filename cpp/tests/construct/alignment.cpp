// SPDX-License-Identifier: AGPL-3.0-or-later
// Determinism across cache/replay, scoring tiles, thread counts and search admission shapes.
#include <iostream>
#include <regex>

#include "construct/selection/alignment_search.hpp"

namespace {
void check(bool ok, const char* what) {
  if (!ok) throw std::runtime_error(what);
}
std::string decisions(std::string log) {
  return std::regex_replace(log, std::regex(R"( \[[0-9.]+s\])"), "");
}
void same(const sic::construct::alignment::AlignResult& a,
          const sic::construct::alignment::AlignResult& b) {
  check(a.choice == b.choice && a.level_gain == b.level_gain, "choices/gains differ");
  check(a.eval_before == b.eval_before && a.eval_after == b.eval_after &&
            a.eval_max_before == b.eval_max_before && a.eval_max_after == b.eval_max_after,
        "evaluation differs");
  check(decisions(a.log) == decisions(b.log), "candidate decisions differ");
}
void compatibility() {
  size_t changed = 0;
  for (unsigned seed = 0; seed < 12; ++seed) {
    sic::construct::alignment::AlignInput in;
    in.seed += seed;
    in.t = 9;
    in.ps.assign(in.t, 2);
    in.ps[0] = 3;
    in.Ncos = {1};
    for (auto p : in.ps) in.Ncos.push_back(in.Ncos.back() * p);
    in.searchable.assign(in.t, 1);
    in.searchable[0] = in.searchable[3] = in.searchable.back() = 0;
    in.lg.resize(in.t);
    std::mt19937_64 rng(seed);
    auto value = [&] { return float(int(rng() % 200001) - 100000) / 137.0f; };
    for (int i = 0; i < in.t - 1; ++i) {
      in.lg[i].resize(in.Ncos[i + 1]);
      for (long c = 0; c < in.Ncos[i + 1]; ++c)
        in.lg[i][c] = (in.searchable[i] && (c & 1)) ? in.lg[i][c - 1] : value();
    }
    for (int L : {2, 4, 6, 7, 8, 8}) {
      sic::construct::alignment::AlignInput::Rep r;
      r.level = L;
      r.lx.resize(in.Ncos[L]);
      for (auto& x : r.lx) x = value();
      in.reps.push_back(std::move(r));
    }
    in.perm = [n = in.Ncos](int s, long v, std::vector<sic::support::Index>& p) {
      p.resize(n[s]);
      for (long u = 0; u < n[s]; ++u) p[u] = (u + v) % n[s];
    };
    in.train = 17;
    in.eval = 11;
    in.cand = 19;
    in.sweeps = 4;
    in.threads = 1;
    const auto expected = sic::construct::alignment::align_search(in);
    for (auto c : expected.choice) changed += c != 0;
    unsigned boundaries = 0;
    in.threads = 4;
    in.rebalance = [&](int& threads, const std::function<void()>& evict) {
      threads = 1;
      if (++boundaries == 3) evict();
    };
    same(sic::construct::alignment::align_search(in), expected);
    check(boundaries >= 3, "cache eviction boundary was not exercised");
    in.rebalance = {};
    for (int threads : {1, 3, 8}) {
      in.threads = threads;
      same(sic::construct::alignment::align_search(in), expected);
      in.cache_bytes = 0;
      in.tile = 13;  // crosses both reconstruction blocks and reduction boundaries
      in.batch_bytes = 4 * in.Ncos[7] * 3;  // three samples at the largest searchable level
      in.worker_bytes = 1;  // one candidate worker, independent of evaluation parallelism
      same(sic::construct::alignment::align_search(in), expected);
      in.cache_bytes = size_t{1} << 30;
      in.tile = 65536;
      in.batch_bytes = size_t{256} << 20;
      in.worker_bytes = size_t{256} << 20;
    }
  }
  check(changed > 12, "oracle failed to exercise accepted updates");
}
void forecast() {
  sic::construct::alignment::AlignInput in;
  in.t = 28;
  in.threads = 32;
  in.ps.assign(in.t, 2);
  in.searchable.assign(in.t, 1);
  in.searchable.back() = 0;
  in.Ncos = {1};
  for (auto p : in.ps) in.Ncos.push_back(in.Ncos.back() * p);
  // d2048 adapted representatives: levels 17..27 and the second smallest-field representative.
  for (int L = 17; L <= 27; ++L)
    for (int ch = 0; ch < 2; ++ch) in.reps.push_back({L, {}, {}});
  for (int ch = 0; ch < 2; ++ch) in.reps.push_back({17, {}, {}});
  const auto m = sic::construct::alignment::align_memory(in);
  check(!m.cache && m.inputs + m.workspace < (uint64_t{16} << 30), "d2048 search exceeds 16 GiB");
  std::cout << "d2048 all-quadratic upper forecast: inputs=" << m.inputs
            << " workspace=" << m.workspace << " total=" << m.inputs + m.workspace << '\n';
  in.train = SIZE_MAX;
  bool threw = false;
  try {
    (void)sic::construct::alignment::align_memory(in);
  } catch (const sic::Error& error) {
    threw = error.kind() == sic::ErrorKind::resource;
  }
  check(threw, "alignment forecast accepted an overflow");
  in.train = 0;
  threw = false;
  try {
    (void)sic::construct::alignment::align_memory(in);
  } catch (const sic::Error& error) {
    threw = error.kind() == sic::ErrorKind::invalid_argument;
  }
  check(threw, "alignment forecast accepted zero training samples");
}
}  // namespace
int main() {
  try {
    compatibility();
    forecast();
    bool caught = false;
    try {
      sic::construct::alignment::detail::par(128, 3, [](long i) {
        if (i == 17) throw std::runtime_error("worker sentinel");
      });
    } catch (const std::runtime_error& e) {
      caught = std::string(e.what()) == "worker sentinel";
    }
    check(caught, "worker exception did not propagate");
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
