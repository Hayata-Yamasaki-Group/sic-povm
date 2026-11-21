// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/selection/alignment_search.hpp"

namespace sic::construct::alignment::detail {
uint64_t add(uint64_t a, uint64_t b) {
  if (b > UINT64_MAX - a) throw sic::Error(sic::ErrorKind::resource, "alignment byte addition");
  return a + b;
}

uint64_t mul(uint64_t a, uint64_t b) {
  if (a && b > UINT64_MAX / a)
    throw sic::Error(sic::ErrorKind::resource, "alignment byte multiplication");
  return a * b;
}

int workers(long tasks, int requested, uint64_t per_worker, uint64_t cap) {
  return static_cast<int>(
      std::min<uint64_t>(std::min<long>(tasks, requested),
                         std::max<uint64_t>(1, cap / std::max<uint64_t>(1, per_worker))));
}

}  // namespace sic::construct::alignment::detail
namespace sic::construct::alignment {
AlignMemory align_memory(const AlignInput& in) {
  using detail::add;
  using detail::mul;
  if (in.t < 1 || in.Ncos.size() != size_t(in.t + 1) || in.ps.size() != size_t(in.t) ||
      in.searchable.size() != size_t(in.t) || in.threads < 1 || !in.train || !in.eval || !in.cand ||
      in.sweeps < 0 || !in.tile || !in.batch_bytes || !in.worker_bytes || in.reps.empty())
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "invalid alignment shape or resource policy");
  AlignMemory m;
  uint64_t rows = 0, pairs = 0, largest = 0;
  for (int i = 0; i < in.t; ++i) {
    if (in.Ncos[i] < 1 || in.ps[i] < 1 || in.ps[i] > 256 ||
        mul(in.Ncos[i], in.ps[i]) != uint64_t(in.Ncos[i + 1]))
      throw sic::Error(sic::ErrorKind::invalid_argument, "invalid alignment tower degrees");
    if (i + 1 < in.t) m.inputs = add(m.inputs, mul(4, in.Ncos[i + 1]));
    if (in.searchable[i]) {
      if (in.ps[i] != 2 ||
          uint64_t(in.Ncos[i]) >
              static_cast<uint64_t>(std::numeric_limits<sic::support::Index>::max()))
        throw sic::Error(sic::ErrorKind::invalid_argument, "invalid alignment searchable degree");
      pairs = add(pairs, in.Ncos[i]);
      largest = std::max<uint64_t>(largest, in.Ncos[i]);
      const uint64_t row = mul(4, in.Ncos[i]);
      const uint64_t samples = mul(in.reps.size(), in.train);  // safe upper bound on qs
      const uint64_t batch_rows = std::min(samples, std::max<uint64_t>(1, in.batch_bytes / row));
      m.batch = std::max(m.batch, mul(row, batch_rows));
      const auto nw = detail::workers(std::min<uint64_t>(in.cand, in.Ncos[i]), in.threads,
                                      mul(sizeof(sic::support::Index) + sizeof(float), in.Ncos[i]),
                                      in.worker_bytes);
      m.workers = std::max(m.workers,
                           mul(nw, mul(sizeof(sic::support::Index) + sizeof(float), in.Ncos[i])));
    }
  }
  for (const auto& r : in.reps) {
    if (r.level < 0 || r.level > in.t)
      throw sic::Error(sic::ErrorKind::invalid_argument, "invalid alignment representative");
    rows = add(rows, in.Ncos[r.level]);
  }
  m.inputs = add(m.inputs, mul(4, rows));
  const auto full = mul(mul(4, rows), in.train);
  m.cache = full <= in.cache_bytes ? full : 0;
  m.journal = mul(mul(4, pairs), in.sweeps);  // also retained with F for exact cache eviction
  m.generators = mul(12, pairs);              // lp, cur and one pending new/delta row
  m.candidate = mul(1 + sizeof(long) + 2 * sizeof(sic::support::Index),
                    largest);  // used, pos, inverse and permutation during candidate generation
  m.workers = add(m.workers, mul(mul(4, in.tile), in.threads));
  const auto samples = mul(in.reps.size(), add(in.train, in.eval));
  m.metadata =
      add(uint64_t{64} << 20,
          add(mul(uint64_t{8} << 20, in.threads),
              add(mul(24, std::min<uint64_t>(in.cand, largest)),
                  add(mul(samples, add(in.t, 256)), mul(mul(in.t, add(in.sweeps, 1)), 4096)))));
  m.workspace = m.metadata;
  for (auto v : {m.cache, m.journal, m.generators, m.batch, m.candidate, m.workers})
    m.workspace = add(m.workspace, v);
  return m;
}

AlignResult align_search(const AlignInput& in) {
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  int active_threads = in.threads;
  const auto memory = align_memory(in);  // validate and forecast before any search allocations
  if (in.lg.size() != size_t(in.t))
    throw sic::Error(sic::ErrorKind::invalid_argument, "invalid alignment log rows");
  for (int i = 0; i < in.t; ++i)
    if (!in.lg[i].empty() && in.lg[i].size() != size_t(in.Ncos[i + 1]))
      throw sic::Error(sic::ErrorKind::invalid_argument, "invalid alignment generator log row");
  for (const auto& r : in.reps)
    if (r.lx.size() != size_t(in.Ncos[r.level]))
      throw sic::Error(sic::ErrorKind::invalid_argument,
                       "invalid alignment representative log row");
  auto trace = [&](const char* stage) {
    if (in.trace) in.trace(stage);
  };
  trace("begin");
  AlignResult out;
  const int t = in.t;
  out.choice.assign(t, 0);
  out.level_gain.assign(t, 0);
  char line[512];
  // pair magnitudes of the original generators: lp[s][u] = l_{g_s} at pair u (quadratic levels)
  std::vector<std::vector<float>> lp(t), cur(t);
  for (int s = 0; s < t; ++s)
    if (in.searchable[s]) {
      if (in.ps[s] != 2 || in.lg[s].size() != static_cast<size_t>(in.Ncos[s + 1]))
        throw std::logic_error("alignment: searchable level is not a modelled quadratic level");
      lp[s].resize(in.Ncos[s]);
      for (long u = 0; u < in.Ncos[s]; ++u) lp[s][u] = in.lg[s][2 * u];
      cur[s] = lp[s];
    }
  // leaf samples
  struct Smp {
    int r;
    std::vector<uint8_t> dg;
    double w;
  };
  std::vector<Smp> tr, ev;
  {
    std::mt19937_64 rng(in.seed);
    for (size_t r = 0; r < in.reps.size(); ++r) {
      const int L = in.reps[r].level;
      const double count = static_cast<double>(in.Ncos[L]);
      for (size_t q = 0; q < in.eval + in.train; ++q) {
        Smp sp{static_cast<int>(r), std::vector<uint8_t>(t, 0), 0};
        for (int i = 0; i < L; ++i)
          sp.dg[i] = static_cast<uint8_t>(rng() % static_cast<uint64_t>(in.ps[i]));
        sp.w = count / static_cast<double>(q < in.eval ? in.eval : in.train);
        (q < in.eval ? ev : tr).push_back(std::move(sp));
      }
    }
  }
  // generator magnitude at label c of level L (c < Ncos[L]) for level i < L
  auto gen_at = [&](const std::vector<std::vector<float>>& gens, int i, int L, long c) -> float {
    if (in.searchable[i]) return gens[i][c / (in.Ncos[L] / in.Ncos[i])];
    return in.lg[i][c / (in.Ncos[L] / in.Ncos[i + 1])];
  };
  struct Move {
    int level;
    std::vector<float> delta;
  };
  std::vector<Move> moves;
  moves.reserve(detail::mul(in.sweeps, t));
  auto fill_slice = [&](const Smp& sp, const std::vector<std::vector<float>>& gens, long first,
                        long count, std::vector<float>& f) {
    const auto& R = in.reps[sp.r];
    f.assign(R.lx.begin() + first, R.lx.begin() + first + count);
    for (int i = 0; i < R.level; ++i) {
      if (!sp.dg[i]) continue;
      if (!in.searchable[i] && in.lg[i].empty()) continue;
      const float mu = sp.dg[i];
      for (long c = 0; c < count; ++c) f[c] += mu * gen_at(gens, i, R.level, first + c);
    }
  };
  auto replay = [&](const Smp& sp, long first, long count, std::vector<float>& f) {
    fill_slice(sp, lp, first, count, f);
    const long n = in.Ncos[in.reps[sp.r].level];
    for (const auto& move : moves) {
      if (in.reps[sp.r].level <= move.level || !sp.dg[move.level]) continue;
      const long sc = n / in.Ncos[move.level];
      const float mu = sp.dg[move.level];
      for (long c = 0; c < count; ++c) f[c] += mu * move.delta[(first + c) / sc];
    }
  };
  auto evaluate = [&](const std::vector<std::vector<float>>& gens, double& mean, double& mx) {
    std::vector<double> a(ev.size());
    detail::par(static_cast<long>(ev.size()), active_threads, [&](long q) {
      std::vector<float> f;
      const long n = in.Ncos[in.reps[ev[q].r].level];
      float m = -std::numeric_limits<float>::infinity();
      for (long first = 0; first < n;) {
        const long count = std::min<uint64_t>(in.tile, n - first);
        fill_slice(ev[q], gens, first, count, f);
        m = std::max(m, *std::max_element(f.begin(), f.end()));
        first += count;
      }
      a[q] = m;
    });
    double tot = 0, wsum = 0;
    mx = -1e300;
    for (size_t q = 0; q < ev.size(); ++q) {
      tot += ev[q].w * std::max(0.0, a[q]);
      wsum += ev[q].w;
      mx = std::max(mx, a[q]);
    }
    mean = wsum > 0 ? tot / wsum : 0;
  };
  evaluate(cur, out.eval_before, out.eval_max_before);
  std::vector<std::vector<float>> F;
  if (memory.cache) {
    F.resize(tr.size());
    detail::par(static_cast<long>(tr.size()), active_threads,
                [&](long q) { fill_slice(tr[q], cur, 0, in.Ncos[in.reps[tr[q].r].level], F[q]); });
  }
  trace(memory.cache ? "F-cache" : "F-replay");
  double wsum_tr = 0;
  for (const auto& sp : tr) wsum_tr += sp.w;
  for (int sweep = 0; sweep < in.sweeps; ++sweep) {
    bool moved = false;
    for (int s = 0; s < t; ++s) {
      if (!in.searchable[s]) continue;
      if (in.rebalance)
        in.rebalance(active_threads, [&] {
          F.clear();
          F.shrink_to_fit();
          sic::support::resources::release_free_heap();
          trace("F-evicted-to-replay");
        });
      const auto ts = Clock::now();
      const long Ds = in.Ncos[s];
      // candidates (pair labels v of tau_v)
      std::vector<long> cands;
      {
        std::vector<char> used(Ds, 0);
        auto add = [&](long v) {
          if (v >= 0 && v < Ds && !used[v]) {
            used[v] = 1;
            cands.push_back(v);
          }
        };
        add(0);
        if (static_cast<size_t>(Ds) <= in.cand) {
          for (long v = 1; v < Ds; ++v) add(v);
        } else {
          // translations u_0 -> u_q from the largest position u_0 to the q-th largest (q < 512):
          // tau with pi_tau(u_0) = u_q; this moves the spike of w_s from u_q to u_0 (and its
          // neighbours accordingly)
          std::vector<long> pos(Ds);
          for (long u = 0; u < Ds; ++u) pos[u] = u;
          const long top = std::min<long>(Ds, std::min<long>(512, static_cast<long>(in.cand / 2)));
          std::partial_sort(pos.begin(), pos.begin() + top, pos.end(), [&](long a, long b) {
            return lp[s][a] > lp[s][b] || (lp[s][a] == lp[s][b] && a < b);
          });
          std::vector<sic::support::Index> p0;
          // pi_v(u_0) = u_q for the v with v = coset(u_q * u_0^{-1}); found by scanning
          // pi_{u_0}^{-1}: pi_v(u_0) = coset(rep(u_0) rep(v)), so among all v the one hitting u_q
          // is unique.
          in.perm(s, pos[0], p0);  // p0[v] = coset(rep(v) rep(u_0)) = pi_v(u_0) (abelian)
          std::vector<sic::support::Index> inv(Ds, sic::support::kNoIndex);
          for (long v = 0; v < Ds; ++v) inv[p0[v]] = sic::support::checked_index(v);
          for (long q = 0; q < top; ++q) add(inv[pos[q]]);
          std::mt19937_64 rng(99 + static_cast<uint64_t>(s));
          while (cands.size() < in.cand) add(static_cast<long>(rng() % static_cast<uint64_t>(Ds)));
        }
      }
      // samples with e_s = 1 and level > s; reduced tables B_a(u) = max_{c in block u} F_a(c) -
      // cur_s(u)
      std::vector<size_t> qs;
      for (size_t q = 0; q < tr.size(); ++q)
        if (in.reps[tr[q].r].level > s && tr[q].dg[s]) qs.push_back(q);
      if (qs.empty()) continue;
      // Batch samples, never labels: each candidate's double additions retain their sequential
      // order.
      const size_t batch_rows =
          std::min<uint64_t>(qs.size(), std::max<uint64_t>(1, in.batch_bytes / detail::mul(4, Ds)));
      std::vector<float> B(detail::mul(batch_rows, Ds));
      std::vector<double> acur(batch_rows);
      std::vector<double> score(cands.size(), 0);
      double base = 0;
      for (size_t first = 0; first < qs.size(); first += batch_rows) {
        const size_t count = std::min(batch_rows, qs.size() - first);
        detail::par(static_cast<long>(count), active_threads, [&](long a) {
          const auto q = qs[first + a];
          const long n = in.Ncos[in.reps[tr[q].r].level], sc = n / Ds;
          float* b = &B[a * Ds];
          std::fill(b, b + Ds, -1e30f);
          if (!F.empty()) {
            const float* f = F[q].data();
            for (long u = 0; u < Ds; ++u)
              for (long c = u * sc; c < (u + 1) * sc; ++c) b[u] = std::max(b[u], f[c]);
          } else {
            std::vector<float> f;
            for (long c = 0; c < n;) {
              const long len = std::min<uint64_t>(in.tile, n - c);
              replay(tr[q], c, len, f);
              for (long j = 0; j < len; ++j) b[(c + j) / sc] = std::max(b[(c + j) / sc], f[j]);
              c += len;
            }
          }
          double am = -1e300;
          for (long u = 0; u < Ds; ++u) {
            am = std::max(am, static_cast<double>(b[u]));
            b[u] -= cur[s][u];
          }
          acur[a] = am;
        });
        for (size_t a = 0; a < count; ++a) base += tr[qs[first + a]].w * std::max(0.0, acur[a]);
        const int workers = detail::workers(
            cands.size(), active_threads,
            detail::mul(sizeof(sic::support::Index) + sizeof(float), Ds), in.worker_bytes);
        detail::par(static_cast<long>(cands.size()), workers, [&](long ci) {
          std::vector<sic::support::Index> p;
          in.perm(s, cands[ci], p);
          std::vector<float> cl(Ds);
          for (long u = 0; u < Ds; ++u) cl[u] = lp[s][p[u]];
          double tot = score[ci];
          for (size_t a = 0; a < count; ++a) {
            const float* b = &B[a * Ds];
            float m = -1e30f;
            for (long u = 0; u < Ds; ++u) m = std::max(m, b[u] + cl[u]);
            tot += tr[qs[first + a]].w * std::max(0.0, static_cast<double>(m));
          }
          score[ci] = tot;
        });
        trace("B-batch");
      }
      size_t bc = 0;
      for (size_t c = 1; c < cands.size(); ++c)
        if (score[c] < score[bc]) bc = c;
      // the current choice is among the candidates only implicitly; compare with base
      const double gain = (score[bc] - base) / wsum_tr;
      const double secs = std::chrono::duration<double>(Clock::now() - ts).count();
      std::snprintf(line, sizeof line,
                    "alignment sweep=%d level=%d D_s=%ld cands=%zu leaves(e_s=1)=%zu best=v%ld "
                    "delta_bits_per_leaf=%.3f [%.1fs]\n",
                    sweep, s, Ds, cands.size(), qs.size(), cands[bc], gain, secs);
      out.log += line;
      if (gain < -1e-4 && cands[bc] != out.choice[s]) {
        moved = true;
        std::vector<sic::support::Index> p;
        in.perm(s, cands[bc], p);
        std::vector<float> nw(Ds);
        for (long u = 0; u < Ds; ++u) nw[u] = lp[s][p[u]];
        if (!F.empty())
          detail::par(static_cast<long>(tr.size()), active_threads, [&](long q) {
            const int L = in.reps[tr[q].r].level;
            if (L <= s || !tr[q].dg[s]) return;
            const long n = in.Ncos[L], sc = n / Ds;
            float* f = F[q].data();
            const float mu = tr[q].dg[s];
            for (long c = 0; c < n; ++c) f[c] += mu * (nw[c / sc] - cur[s][c / sc]);
          });
        {
          Move move{s, std::vector<float>(Ds)};
          for (long u = 0; u < Ds; ++u) move.delta[u] = nw[u] - cur[s][u];
          moves.push_back(std::move(move));
        }
        cur[s] = std::move(nw);
        out.choice[s] = cands[bc];
        out.level_gain[s] += gain;
      }
    }
    if (!moved) break;
  }
  trace("evaluation");
  evaluate(cur, out.eval_after, out.eval_max_after);
  trace("end");
  out.seconds = std::chrono::duration<double>(Clock::now() - start).count();
  return out;
}

}  // namespace sic::construct::alignment
