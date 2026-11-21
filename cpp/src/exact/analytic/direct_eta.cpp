// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/analytic/direct_eta.hpp"

#include "exact/analytic/direct_eta_rows.hpp"
#include "sic/error.hpp"

namespace sic::exact::direct_eta_detail {
long mod(EtaInt128 x, long m) {
  EtaInt128 r = x % m;
  if (r < 0) r += m;
  return (long)r;
}

long inverse_mod(long a, long m) {
  // m is a power of two >= 2 and a is odd.
  long x = 1;
  // Each iteration doubles the correct bits from one; 7 cover the admitted modulus <= 2^21.
  constexpr int kNewtonIterations = 7;
  for (int i = 0; i < kNewtonIterations; ++i) x = mod((EtaInt128)x * (2 - (EtaInt128)a * x), m);
  if (mod((EtaInt128)a * x, m) != 1 % m) throw std::logic_error("direct eta inverse");
  return x;
}

}  // namespace sic::exact::direct_eta_detail
namespace sic::exact {
std::string plan_direct_eta(long d, long n, long ng, long m, long A, long B, long C, long D,
                            int chi, bool conjugate, DirectEtaPlan& plan) {
  using direct_eta_detail::mod;
  if (d < 4 || (d & (d - 1)) || d > (1L << 20)) return "unsupported dimension";
  if (n < 0 || n >= 2 * d || ng < 0 || ng >= 2 * d || !(n & 1) || !(ng & 1))
    return "zeta action not an odd residue mod 2d";
  if (m < 0 || m >= d) return "representative index outside [0,d)";
  for (long x : {A, B, C, D})
    if (x < 0 || x >= d) return "matrix entry outside [0,d)";
  if (chi != 1 && chi != -1) return "invalid chi";
  if (mod((EtaInt128)A - ((EtaInt128)(d - 1) * B + D), d) || mod((EtaInt128)C + B, d))
    return "matrix is not a multiplication by B eps + D";
  const long nu = mod((EtaInt128)A * D - (EtaInt128)B * C, d);
  if (!(nu & 1)) return "even determinant";
  if (mod((EtaInt128)ng - (EtaInt128)n * nu, d))
    return "norm compatibility ng = n det H mod d fails";
  const EtaInt128 X =
      (EtaInt128)ng - (EtaInt128)n * (A * (EtaInt128)A + C * (EtaInt128)C) + (EtaInt128)n * A * C;
  const EtaInt128 Y =
      (EtaInt128)ng - (EtaInt128)n * (B * (EtaInt128)B + D * (EtaInt128)D) + (EtaInt128)n * B * D;
  const EtaInt128 Z =
      (EtaInt128)ng + (EtaInt128)n * (A * (EtaInt128)B + C * (EtaInt128)D) - (EtaInt128)n * A * D;
  if (mod(X, d) || mod(Y, d) || mod(Z, d)) return "parity congruences fail";
  const long li = mod(X / d + 1 + A + C, 2);
  const long lm = mod(Y / d - 1 - B - D, 2);
  plan = DirectEtaPlan();
  plan.conjugate = conjugate;
  plan.chi = chi;
  if (C == 0) {
    if (B != 0) return "C = 0 without B = 0";
    plan.scalar = true;
    plan.astar = mod((EtaInt128)D * m + li * (d / 2), d);
    plan.bstar = mod(li * (d / 2), d);
    plan.scalar_sign = (lm * m) % 2 ? -1 : 1;
    return {};
  }
  const long k = std::gcd(C, d), L = d / k, c = C / k;
  if (!(c & 1) || (L & 1)) return "unexpected C stratum";
  const long cbar = direct_eta_detail::inverse_mod(c, 2 * L);
  const EtaInt128 Dm = (EtaInt128)D * m;
  const long a0 = mod(Dm, k);
  const EtaInt128 h0 = (Dm - a0) / k;
  const long alpha = mod((EtaInt128)n * A * cbar, 2 * L);
  plan.k = k;
  plan.L = L;
  plan.a0 = a0;
  plan.gamma = mod((EtaInt128)alpha + (EtaInt128)L * li, 2 * L);
  plan.f = mod((EtaInt128)alpha * h0 - (EtaInt128)n * B * m, L);
  // Exact integer evaluation of (12); every term is below 2^110 for d <= 2^20.
  plan.P = mod((EtaInt128)k * alpha * h0 * h0 - (EtaInt128)n * B * D * m * m +
                   (EtaInt128)2 * n * B * m * a0 + (EtaInt128)d * li * h0 + (EtaInt128)d * lm * m,
               2 * d);
  return {};
}

DirectEtaRoot::DirectEtaRoot(long d, slong precision, sic::support::ComplexBallVector v,
                             sic::support::ComplexBallVector u, const std::vector<acb_struct>& w2d)
    : d_(d), p_(precision), v_(std::move(v)), u_(std::move(u)), w2d_(2 * d), k0_(1) {
  if (d < 4 || (d & (d - 1)) || v_.size() != (size_t)d || u_.size() != (size_t)d ||
      w2d.size() != (size_t)(2 * d))
    throw std::invalid_argument("invalid direct eta root");
  for (long r = 0; r < 2 * d; ++r) acb_set_round(w2d_[r], &w2d[r], p_);
  sic::support::ComplexBallVector s(1);
  acb_dot(s[0], nullptr, 0, u_.v.data(), 1, v_.v.data(), 1, d, p_);
  if (!acb_is_finite(s[0]) || acb_contains_zero(s[0]))
    throw sic::Error(sic::ErrorKind::precision,
                     "direct eta: bilinear normalization S contains zero");
  arb_sqrt_ui(acb_realref(k0_[0]), d + 1, p_);
  arb_zero(acb_imagref(k0_[0]));
  acb_div(k0_[0], k0_[0], s[0], p_);
  if (!acb_is_finite(k0_[0]))
    throw sic::Error(sic::ErrorKind::precision, "direct eta: nonfinite q0/S");
}

DirectEtaRoot::~DirectEtaRoot() {
  for (auto& kv : plans_) acb_dft_precomp_clear(kv.second->value);
}

DirectEtaRoot::Census DirectEtaRoot::evaluate(const std::vector<DirectEtaPlan>& plans,
                                              sic::support::ComplexBallVector& out,
                                              slong out_precision, int threads,
                                              DirectEtaRows* cache, const std::string& domain) {
  try {
    if (out.size() != plans.size()) throw std::invalid_argument("direct eta output size");
    Census census;
    std::vector<size_t> scalar, rowed;
    for (size_t q = 0; q < plans.size(); ++q) (plans[q].scalar ? scalar : rowed).push_back(q);
    census.scalar = scalar.size();
    census.row_entries = rowed.size();
    // Gauss sums and DFT plans for every stratum in use (serial, cheap).
    for (size_t q : rowed) prepare_stratum(plans[q].k);
    auto key_of = [&](const DirectEtaPlan& p) {
      return std::tuple<long, long, long>(p.k, p.a0, p.gamma % p.L);
    };
    std::sort(rowed.begin(), rowed.end(), [&](size_t a, size_t b) {
      const auto ka = key_of(plans[a]), kb = key_of(plans[b]);
      return ka != kb ? ka < kb : a < b;
    });
    std::vector<size_t> starts;
    for (size_t q = 0; q < rowed.size(); ++q)
      if (!q || key_of(plans[rowed[q]]) != key_of(plans[rowed[q - 1]])) starts.push_back(q);
    starts.push_back(rowed.size());
    census.rows = starts.size() - 1;
    for (size_t r = 0; r + 1 < starts.size(); ++r) census.row_cells += plans[rowed[starts[r]]].L;
    DirectEtaRows::Batch batch;
    if (cache) {
      if (domain.empty()) throw std::invalid_argument("missing authenticated direct row domain");
      std::vector<DirectEtaRows::Request> requests;
      requests.reserve(census.rows);
      for (size_t r = 0; r < census.rows; ++r) {
        const auto& head = plans[rowed[starts[r]]];
        requests.push_back({head.k, head.a0, head.gamma % head.L, head.L});
      }
      batch = cache->prepare(domain, d_, p_, requests);
      census.hits = batch.hits;
      census.evictions = batch.evictions;
    }
    parallel_for((long)scalar.size(), threads, [&](long index) {
      const auto& p = plans[scalar[index]];
      sic::support::ComplexBallVector t(1);
      acb_mul(t[0], v_[p.astar], u_[p.bstar], p_);
      acb_mul(t[0], t[0], k0_[0], p_);
      acb_mul_ui(t[0], t[0], (ulong)d_, p_);
      if (p.scalar_sign * p.chi < 0) acb_neg(t[0], t[0]);
      finish(t[0], p, out[scalar[index]], out_precision);
    });
    parallel_for((long)census.rows, threads, [&](long row) {
      const size_t first = starts[row], last = starts[row + 1];
      sic::support::check_stop();
#ifdef SIC_ENABLE_TEST_HOOKS
      if (before_row) before_row(failure_context, row);
      if (test_fault_mid_rows && (size_t)row == census.rows / 2)
        throw std::runtime_error("injected direct eta row fault");
#endif
      const auto& head = plans[rowed[first]];
      const long k = head.k, L = head.L, a0 = head.a0, g0 = head.gamma % L;
      sic::support::ComplexBallVector temporary, work(1);
      auto* slot = cache ? batch.slots[row] : nullptr;
      auto& t = slot ? *slot : temporary;
      if (!t.size()) {
        sic::support::ComplexBallVector w(L);
        t = sic::support::ComplexBallVector(L);
        // w_x = v_(a0+kx) z^(k g0 x^2); Vhat(f) = sum_x w_x exp(-2 pi i f x/L).
        for (long x = 0; x < L; ++x) {
          const long e = direct_eta_detail::mod(
              (EtaInt128)k * g0 % (2 * d_) * ((EtaInt128)x * x % (2 * d_)), 2 * d_);
          acb_mul(w[x], v_[a0 + k * x], w2d_[e], p_);
        }
        acb_dft_precomp(t.v.data(), w.v.data(), plans_.at(L)->value, p_);
      }
      const sic::support::ComplexBallVector& gauss = gauss_.at(k);
      for (size_t q = first; q < last; ++q) {
        const auto& p = plans[rowed[q]];
        // Vhat_(g0+L)(f) = Vhat_(g0)(f - L/2): z^(k L x^2) = (-1)^x.
        const long f = p.gamma >= L ? direct_eta_detail::mod((EtaInt128)p.f - L / 2, L) : p.f;
        acb_mul(work[0], t[f], gauss[p.gamma], p_);
        acb_mul(work[0], work[0], w2d_[p.P], p_);
        acb_mul(work[0], work[0], k0_[0], p_);
        acb_mul_ui(work[0], work[0], (ulong)k, p_);
        if (p.chi < 0) acb_neg(work[0], work[0]);
        finish(work[0], p, out[rowed[q]], out_precision);
      }
    });
    if (cache) cache->settle();
    return census;
  } catch (...) {
    // parallel_for joins every worker before propagating failure. Never retain
    // a partially written row after stop, arithmetic refusal or allocation error.
    if (cache) cache->clear();
    throw;
  }
}

unsigned long long DirectEtaRoot::workspace_bytes(long d, slong precision, int threads,
                                                  size_t plans) {
  const unsigned long long ball = 2ULL * (64 + ((unsigned long long)precision + 63) / 64 * 8 + 64);
  return (unsigned long long)threads * 2ULL * (unsigned long long)d * ball  // live rows
         + 16ULL * (unsigned long long)d *
               ball  // roots (two contexts), packed root, v,u, Gauss sums and buckets
         + 2ULL * 2ULL * (unsigned long long)d * ball  // DFT plans (twiddles)
         + (unsigned long long)plans * (sizeof(DirectEtaPlan) + 2 * sizeof(size_t)) + (64ULL << 20);
}

void DirectEtaRoot::finish(acb_ptr value, const DirectEtaPlan& p, acb_ptr out,
                           slong out_precision) const {
  if (p.conjugate) acb_conj(value, value);
  acb_set_round(out, value, out_precision);
}

void DirectEtaRoot::prepare_stratum(long k) {
  const long L = d_ / k;
  if (!plans_.count(L)) {
    auto plan = std::make_unique<Plan>();
    acb_dft_precomp_init(plan->value, L, p_);
    plans_.emplace(L, std::move(plan));
  }
  if (gauss_.count(k)) return;
  // Ugauss_(k,gamma) = sum_y u_(ky) exp(-2 pi i gamma y^2/(2L)) for gamma in [0,2L):
  // the negative-exponent length-2L DFT of the exact square buckets.
  sic::support::ComplexBallVector bucket(2 * L), gauss(2 * L);
  for (long y = 0; y < L; ++y) {
    const long r = direct_eta_detail::mod((EtaInt128)y * y, 2 * L);
    acb_add(bucket[r], bucket[r], u_[k * y], p_);
  }
  acb_dft(gauss.v.data(), bucket.v.data(), 2 * L, p_);
  gauss_.emplace(k, std::move(gauss));
}

}  // namespace sic::exact
