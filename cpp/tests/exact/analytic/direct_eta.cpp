// SPDX-License-Identifier: AGPL-3.0-or-later
// Finite-identity oracle for direct_eta.hpp.
//
// For arbitrary (random) bilinear root vectors v,u the literal representative
// sum (3) of the design note, with raw-lift values (2), is evaluated by brute
// force from the full coefficient grid C(f,j) and compared with the planned
// chirp/Gauss contraction for every multiplication matrix of odd determinant,
// both norm-compatible lifts of ng modulo 2d, all m (subset at d=32), both chi
// signs and both conjugation flags. Refusal controls are checked too.
#include "exact/analytic/direct_eta.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>

#include "exact/analytic/direct_eta_rows.hpp"
#include "exact/certification/reconstruction/ghost_reconstruction.hpp"

using namespace sic::exact;

namespace {

int failures = 0;
bool fault_mode = false;
void check(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    if (failures < 20) std::printf("FAIL: %s\n", what);
  }
}

struct Fixture {
  long d;
  slong p;
  sic::support::ComplexBallVector v, u, z, grid;  // z[r] = exp(pi i r/d), grid[f*d+j] = C(f,j)
  sic::support::ComplexBallVector q0, S;
  Fixture(long dimension, slong precision, unsigned seed)
      : d(dimension), p(precision), v(d), u(d), z(2 * d), grid(d * d), q0(1), S(1) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> digit(-1000, 1000);
    for (long a = 0; a < d; ++a) {
      acb_set_si_si(v[a], digit(rng), digit(rng));
      acb_div_ui(v[a], v[a], 997, p);
      acb_set_si_si(u[a], digit(rng), digit(rng));
      acb_div_ui(u[a], u[a], 991, p);
    }
    arb_t x;
    arb_init(x);
    for (long r = 0; r < 2 * d; ++r) {
      arb_set_si(x, r);
      arb_div_si(x, x, d, p);
      arb_sin_cos_pi(acb_imagref(z[r]), acb_realref(z[r]), x, p);
    }
    arb_clear(x);
    for (long f = 0; f < d; ++f)
      for (long j = 0; j < d; ++j) {
        acb_ptr out = grid[f * d + j];
        acb_zero(out);
        sic::support::ComplexBallVector t(1);
        for (long a = 0; a < d; ++a) {
          acb_mul(t[0], v[a], u[(a + j) % d], p);
          acb_mul(t[0], t[0], z[(2 * d - (2 * f * a) % (2 * d)) % (2 * d)], p);
          acb_add(out, out, t[0], p);
        }
      }
    arb_sqrt_ui(acb_realref(q0[0]), d + 1, p);
    acb_dot(S[0], nullptr, 0, u.v.data(), 1, v.v.data(), 1, d, p);
  }
  // Literal (2): value at the raw lift (i,j) != (0,0).
  void table(acb_ptr out, long n, long i, long j) const {
    const long f = (n * i) % d;
    const long e = (2 * d - (n * ((i * i + j * j) % (2 * d))) % (2 * d)) % (2 * d);
    acb_mul(out, grid[f * d + j], z[e], p);
    acb_mul(out, out, q0[0], p);
    acb_div(out, out, S[0], p);
    if ((i + j) % 2 == 0) acb_neg(out, out);  // -q0 (-1)^(i+j)
  }
  // Literal (3) through H.
  void literal(acb_ptr out, long n, long ng, long m, long A, long B, long C, long D, int chi,
               bool conj) const {
    sic::support::ComplexBallVector t(1), term(1);
    acb_zero(out);
    for (long i = 0; i < d; ++i) {
      if (i == 0 && m == 0) continue;
      const long ip = ((A * i - B * m) % d + d) % d, jp = ((C * i - D * m) % d + d) % d;
      table(t[0], n, ip, jp);
      acb_mul(term[0], t[0], z[(ng * (((i + m) * (i + m)) % (2 * d))) % (2 * d)], p);
      if ((i - m) % 2) acb_neg(term[0], term[0]);
      acb_add(out, out, term[0], p);
    }
    acb_neg(out, out);
    if (m == 0) arb_add(acb_realref(out), acb_realref(out), acb_realref(q0[0]), p);
    if (chi < 0) acb_neg(out, out);
    if (conj) acb_conj(out, out);
  }
};

void run_dimension(long d, const std::vector<long>& ns, const std::vector<long>& ms,
                   unsigned seed) {
  const slong p = 256;
  Fixture F(d, p, seed);
  DirectEtaRoot root(d, p, F.v, F.u, F.z.v);
  std::vector<DirectEtaPlan> plans;
  std::vector<std::array<long, 9>> cases;  // n ng m A B C D chi conj
  long refusals_checked = 0;
  for (long n : ns)
    for (long B = 0; B < d; ++B)
      for (long D = 0; D < d; ++D) {
        const long A = ((d - 1) * B + D) % d, C = (d - B) % d;
        const long nu = (((A * D - B * C) % d) + d) % d;
        if (!(nu & 1)) {
          DirectEtaPlan plan;
          check(!plan_direct_eta(d, n, n, 1, A, B, C, D, 1, false, plan).empty(),
                "even determinant must refuse");
          continue;
        }
        const long r = (n * nu) % d;
        for (long ng : {r, r + d}) {
          {
            DirectEtaPlan plan;
            const long bad = (ng + 2) % (2 * d);
            check(!plan_direct_eta(d, n, bad, 1, A, B, C, D, 1, false, plan).empty(),
                  "inadmissible ng must refuse");
            ++refusals_checked;
          }
          for (long m : ms) {
            const int chi = ((B + D + m) & 1) ? -1 : 1;
            const bool conj = ((B * 7 + D * 3 + m + ng) % 3) == 0;
            DirectEtaPlan plan;
            const auto why = plan_direct_eta(d, n, ng, m, A, B, C, D, chi, conj, plan);
            check(why.empty(), "admissible plan refused");
            if (!why.empty()) {
              std::printf("  d=%ld n=%ld ng=%ld m=%ld B=%ld D=%ld: %s\n", d, n, ng, m, B, D,
                          why.c_str());
              continue;
            }
            plan.position = (long)plans.size();
            plans.push_back(plan);
            cases.push_back({n, ng, m, A, B, C, D, chi, conj});
          }
        }
      }
  sic::support::ComplexBallVector direct(plans.size());
  const auto census = root.evaluate(plans, direct, p, 2);
  // Exercise the same interval oracle with cross-call storage, fresh root
  // objects, distinct scheduling and output precision. Equality here includes
  // every midpoint and radius, rather than only overlap of enclosures.
  DirectEtaRows cache(8 * sic::support::resources::kMib);
  sic::support::ComplexBallVector cached(plans.size());
  auto cold = root.evaluate(plans, cached, p, 1, &cache, "authenticated-root-a:0:256");
  check(cold.hits == 0, "cold row cache has a hit");
  for (int threads : {1, 2, 4}) {
    DirectEtaRoot fresh(d, p, F.v, F.u, F.z.v);
    const auto warm =
        fresh.evaluate(plans, cached, p, threads, &cache, "authenticated-root-a:0:256");
    check(warm.hits == census.rows, "cross-call rows were not retained");
    for (size_t q = 0; q < plans.size(); ++q)
      check(acb_equal(cached[q], direct[q]), "cache changes midpoint or radius across threads");
  }
  std::vector<DirectEtaPlan> subset;
  for (size_t q = 0; q < plans.size(); q += 7) subset.push_back(plans[q]);
  sic::support::ComplexBallVector expected_subset(subset.size()), actual_subset(subset.size());
  root.evaluate(subset, expected_subset, 192, 1);
  root.evaluate(subset, actual_subset, 192, 4, &cache, "authenticated-root-a:0:256");
  for (size_t q = 0; q < subset.size(); ++q)
    check(acb_equal(expected_subset[q], actual_subset[q]), "cache changes subset/output precision");
  DirectEtaRoot lower(d, 192, F.v, F.u, F.z.v);
  lower.evaluate(subset, expected_subset, 192, 1);
  auto lower_cold =
      lower.evaluate(subset, actual_subset, 192, 2, &cache, "authenticated-root-a:0:256");
  check(lower_cold.hits == 0, "rows leaked across root precisions");
  for (size_t q = 0; q < subset.size(); ++q)
    check(acb_equal(expected_subset[q], actual_subset[q]), "precision-keyed cache changes balls");
  Fixture changed(d, p, seed + 1000);
  DirectEtaRoot other_root(d, p, changed.v, changed.u, changed.z.v);
  other_root.evaluate(subset, expected_subset, p, 1);
  const auto changed_cold =
      other_root.evaluate(subset, actual_subset, p, 2, &cache, "authenticated-root-b:0:256");
  check(changed_cold.hits == 0, "rows leaked across authenticated root generations");
  for (size_t q = 0; q < subset.size(); ++q)
    check(acb_equal(expected_subset[q], actual_subset[q]), "generation-keyed cache changes balls");
  // A one-row window must evict old rows while reproducing the literal path.
  DirectEtaRows bounded(DirectEtaRows::row_bytes(d, p, 256));
  size_t evictions = 0;
  std::map<std::tuple<long, long, long>, DirectEtaPlan> unique;
  for (const auto& plan : plans)
    if (!plan.scalar) unique.try_emplace({plan.k, plan.a0, plan.gamma % plan.L}, plan);
  for (const auto& [key, plan] : unique) {
    std::vector<DirectEtaPlan> one{plan};
    sic::support::ComplexBallVector expected(1), actual(1);
    root.evaluate(one, expected, p, 1);
    evictions += root.evaluate(one, actual, p, 2, &bounded, "bounded-root").evictions;
    check(acb_equal(expected[0], actual[0]), "eviction changes direct output");
    check(bounded.bytes() <= bounded.capacity(), "row cache exceeds its admitted bound");
  }
  check(evictions > 0, "bounded row cache did not exercise eviction");
  DirectEtaRows partitioned(2 * DirectEtaRows::row_bytes(d, p, 256), 2);
  root.evaluate(plans, cached, p, 2, &partitioned, "class-a");
  root.evaluate(plans, cached, p, 2, &partitioned, "class-b");
  const auto returned = root.evaluate(plans, cached, p, 2, &partitioned, "class-a");
  check(returned.hits > 0, "other classes evicted the entire reusable class window");
  check(partitioned.bytes() <= partitioned.capacity(), "class windows exceed the global bound");
  for (size_t q = 0; q < plans.size(); ++q)
    check(acb_equal(cached[q], direct[q]), "partitioned cache changes balls");
  DirectEtaRows denied(0);
  root.evaluate(subset, expected_subset, p, 1);
  root.evaluate(subset, actual_subset, p, 2, &denied, "denied-root");
  check(denied.bytes() == 0, "zero window retained a row");
  for (size_t q = 0; q < subset.size(); ++q)
    check(acb_equal(expected_subset[q], actual_subset[q]), "denied retention changes output");
#ifdef SIC_ENABLE_TEST_HOOKS
  if (fault_mode) {
    root.test_fault_mid_rows = true;
    bool faulted = false;
    try {
      root.evaluate(plans, cached, p, 4, &cache, "authenticated-root-a:0:256");
    } catch (const std::runtime_error&) {
      faulted = true;
    }
    root.test_fault_mid_rows = false;
    check(faulted && cache.size() == 0 && cache.bytes() == 0,
          "row fault retained incomplete state");
    root.evaluate(plans, cached, p, 2, &cache, "authenticated-root-a:0:256");
    for (size_t q = 0; q < plans.size(); ++q)
      check(acb_equal(cached[q], direct[q]), "post-failure recomputation changes balls");
  }
#endif
  long scalar = 0, strata[32] = {0}, lifted = 0;
  sic::support::ComplexBallVector literal(1), other(1);
  for (size_t q = 0; q < plans.size(); ++q) {
    const auto& c = cases[q];
    F.literal(literal[0], c[0], c[1], c[2], c[3], c[4], c[5], c[6], (int)c[7], c[8]);
    const bool ok = acb_overlaps(literal[0], direct[q]) && acb_rel_accuracy_bits(direct[q]) > 150;
    if (!ok && failures < 20)
      std::printf("  mismatch d=%ld n=%ld ng=%ld m=%ld A=%ld B=%ld C=%ld D=%ld scalar=%d\n", d,
                  c[0], c[1], c[2], c[3], c[4], c[5], c[6], plans[q].scalar);
    check(ok, "direct contraction differs from the literal sum");
    if (plans[q].scalar)
      ++scalar;
    else
      ++strata[__builtin_ctzl(plans[q].k)];
    if (plans[q].gamma >= plans[q].L && !plans[q].scalar) ++lifted;
  }
  // The ng lift matters: the other lift gives a distinguishable value somewhere.
  long distinguished = 0;
  for (size_t q = 0; q < plans.size() && !distinguished; ++q) {
    const auto& c = cases[q];
    F.literal(other[0], c[0], (c[1] + d) % (2 * d), c[2], c[3], c[4], c[5], c[6], (int)c[7], c[8]);
    if (!acb_overlaps(other[0], direct[q])) ++distinguished;
  }
  check(distinguished > 0, "ng lift parity is never distinguished");
  std::printf(
      "d=%ld cases=%zu scalar=%ld rows=%zu row_cells=%llu gamma>=L=%ld refusals=%ld strata:", d,
      plans.size(), scalar, census.rows, census.row_cells, lifted, refusals_checked);
  for (int v = 0; v < 32; ++v)
    if (strata[v]) std::printf(" k=%ld:%ld", 1L << v, strata[v]);
  std::printf("\n");
}

}  // namespace

// Binds the oracle's raw-lift formula (2) to the production reconstruction:
// reconstruct_ghost_state with cst = -q0 (chi = +1, so packing is the identity)
// must emit Fixture::table at every original (i,j) != (0,0).
void bind_reconstruction(long d, long n, unsigned seed) {
  Fixture F(d, 256, seed);
  acb_one(F.v[0]);
  acb_one(F.u[0]);
  Fixture G(d, 256, seed);  // recompute grid and S for the normalized vectors
  for (long a = 0; a < d; ++a) {
    acb_set(G.v[a], F.v[a]);
    acb_set(G.u[a], F.u[a]);
  }
  for (long f = 0; f < d; ++f)
    for (long j = 0; j < d; ++j) {
      acb_ptr out = G.grid[f * d + j];
      acb_zero(out);
      sic::support::ComplexBallVector t(1);
      for (long a = 0; a < d; ++a) {
        acb_mul(t[0], G.v[a], G.u[(a + j) % d], G.p);
        acb_mul(t[0], t[0], G.z[(2 * d - (2 * f * a) % (2 * d)) % (2 * d)], G.p);
        acb_add(out, out, t[0], G.p);
      }
    }
  acb_dot(G.S[0], nullptr, 0, G.u.v.data(), 1, G.v.v.data(), 1, d, G.p);
  struct Context {
    slong p;
    std::vector<acb_struct> w2d;
    acb_t cst;
  } context;
  context.p = G.p;
  context.w2d.resize(2 * d);
  for (long r = 0; r < 2 * d; ++r) {
    acb_init(&context.w2d[r]);
    acb_set(&context.w2d[r], G.z[r]);
  }
  acb_init(context.cst);
  acb_neg(context.cst, G.q0[0]);
  std::vector<acb_struct> state(2 * d - 2);
  for (long a = 1; a < d; ++a) {
    acb_init(&state[a - 1]);
    acb_set(&state[a - 1], G.v[a]);
    acb_init(&state[d - 2 + a]);
    acb_set(&state[d - 2 + a], G.u[a]);
  }
  GhostRootMetadata metadata;
  metadata.d = d;
  metadata.n = n;
  metadata.k = 0;
  metadata.chi = 1;
  metadata.precision = G.p;
  metadata.constants_precision = G.p;
  long emitted = 0;
  sic::support::ComplexBallVector expected(1);
  reconstruct_ghost_state(metadata, state, context, [&](long original, acb_srcptr value) {
    G.table(expected[0], n, original / d, original % d);
    check(acb_overlaps(expected[0], value),
          "oracle formula (2) differs from reconstruct_ghost_state");
    ++emitted;
  });
  check(emitted == d * d - 1, "reconstruction emitted count");
  for (auto& x : state) acb_clear(&x);
  for (auto& x : context.w2d) acb_clear(&x);
  acb_clear(context.cst);
  std::printf("bound oracle (2) to reconstruct_ghost_state: d=%ld n=%ld values=%ld\n", d, n,
              emitted);
}

int main(int argc, char** argv) {
  fault_mode = argc == 2 && std::string(argv[1]) == "fault";
#ifndef SIC_ENABLE_TEST_HOOKS
  if (fault_mode) return 2;
#endif
  bind_reconstruction(8, 3, 21);
  bind_reconstruction(16, 13, 22);
  bind_reconstruction(32, 27, 23);
  std::vector<long> all8(8), all16(16);
  for (long m = 0; m < 8; ++m) all8[m] = m;
  for (long m = 0; m < 16; ++m) all16[m] = m;
  run_dimension(4, {1, 3, 5, 7}, {0, 1, 2, 3}, 11);
  run_dimension(8, {1, 3, 5, 7, 9, 15}, all8, 12);
  run_dimension(16, {1, 7, 13, 31}, all16, 13);
  run_dimension(32, {5, 27}, {0, 1, 2, 3, 4, 8, 16, 31}, 14);
  // A vanishing bilinear normalization must refuse.
  {
    const long d = 8;
    sic::support::ComplexBallVector v(d), u(d), z(2 * d);
    acb_one(v[0]);
    acb_one(u[1]);
    for (long r = 0; r < 2 * d; ++r) acb_one(z[r]);
    bool refused = false;
    try {
      DirectEtaRoot root(d, 128, v, u, z.v);
    } catch (const std::runtime_error&) {
      refused = true;
    }
    check(refused, "S = 0 must refuse");
  }
  {
    DirectEtaPlan plan;
    check(!plan_direct_eta(8, 2, 1, 0, 1, 0, 0, 1, 1, false, plan).empty(), "even n must refuse");
    check(!plan_direct_eta(6, 1, 1, 0, 1, 0, 0, 1, 1, false, plan).empty(),
          "non power of two must refuse");
    check(!plan_direct_eta(8, 1, 1, 0, 2, 0, 0, 1, 1, false, plan).empty(),
          "non-multiplication matrix must refuse");
  }
  if (failures) {
    std::printf("sic-test-exact-direct-eta: %d failures\n", failures);
    return 1;
  }
  std::printf("sic-test-exact-direct-eta: all checks passed\n");
  return 0;
}
