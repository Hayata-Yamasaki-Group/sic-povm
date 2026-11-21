// SPDX-License-Identifier: AGPL-3.0-or-later
#include <flint/arb_hypgeom.h>
#include <flint/arb_mat.h>

#include <chrono>

#include "exact/analytic/table/faddeev.hpp"
#include "exact/analytic/table/quadrature.hpp"
#include "exact/analytic/table/table_view.hpp"
#include "exact/certification/bounds/quadrature_bounds.hpp"
#include "exact/runtime/parallel.hpp"
#include "sic/support/flint_raii.hpp"
#include "support/arithmetic/balanced_product.hpp"
#include "support/resources.hpp"
namespace sic::exact {
sic::support::resources::Bytes table_kernel_bytes(long d, long node_slab, std::mutex& gl_mutex,
                                                  const TableRuleCache& gl_cache, size_t nodes,
                                                  long rows, size_t differences, slong wp) {
  using sic::support::resources::add;
  using sic::support::resources::Bytes;
  using sic::support::resources::mul;
  using sic::support::resources::real_bytes;
  const sic::support::resources::Count per_node = 2 * rows + d + differences + 5;
  // Include the live input/rule arrays, retained rules and quadrature output storage.
  const sic::support::resources::Count active =
      node_slab ? std::min<sic::support::resources::Count>(nodes, node_slab) : nodes;
  Bytes result = mul(mul(active, per_node), mul(2, real_bytes(wp)));
  result = add(result, mul(nodes, mul(8, real_bytes(wp))));
  {
    std::lock_guard<std::mutex> lock(gl_mutex);
    for (const auto& [key, rule] : gl_cache)
      result = add(result, mul(rule.size(), mul(2, real_bytes(key.precision_bits))));
  }
  result = add(result, mul(mul(2 * rows, d), mul(2, real_bytes(wp))));

  return result;
}

sic::support::resources::Bytes TableView::kernel_bytes(size_t nodes, long rows, size_t differences,
                                                       slong wp) const {
  return table_kernel_bytes(d, node_slab, gl_mutex, gl_cache, nodes, rows, differences, wp);
}
void TableView::eighth_root(acb_t r, long k, slong p) {
  sic::support::flint::Arb th;
  arb_const_pi(th, p);
  arb_mul_si(th, th, ((k % 8) + 8) % 8, p);
  arb_mul_2exp_si(th, th, -2);
  arb_sin_cos(acb_imagref(r), acb_realref(r), th, p);
}
void TableView::batch_state(sic::support::Index sid) {
  const sic::support::Index c = s_c[sid], l = s_l[sid];
  const auto state_started = std::chrono::steady_clock::now();
  if (sic::support::kernel_setting("SIC_PROGRESS"))
    sic::support::progressf("quadrature start: class=%ld state=%ld precision=%ld\n", (long)c,
                            (long)l, (long)prec);
  const slong wp = prec + 40;
  const double umax = 0.5 + 1e-9;
  SData& out = sd[sid];
  out.L.resize(d * d);
  out.S.resize(d * d);
  for (auto& z : out.L) acb_init(&z);
  for (auto& z : out.S) acb_init(&z);

  certified::Ball scalars[7];
  arb_ptr tau = scalars[0].v, b = scalars[1].v, binv = scalars[2].v, cen = scalars[3].v;
  arb_ptr eta = scalars[4].v, tmp = scalars[5].v, tmp2 = scalars[6].v;
  to_arb(tau, ar.kc, ar.cls[c].tau[l], 1, wp);
  arb_sqrt(b, tau, wp);
  arb_inv(binv, b, wp);
  arb_sub_ui(cen, tau, 1, wp);
  arb_mul_2exp_si(cen, cen, -1);
  const double bd = arf_get_d(arb_midref(b), ARF_RND_NEAR);
  const double etad = M_PI * std::min(bd, 1.0 / bd) / 2;
  arb_set_d(eta, etad);

  // rows: x = 1..d/2 (chain) and x = d (direct)
  const long half = d / 2;
  std::vector<long> rows;
  for (long x = 1; x <= half; ++x) rows.push_back(x);
  rows.push_back(d);
  const long R = (long)rows.size();
  std::vector<long> Ymin(d + 1, 0);
  sic::support::RealBallVector u0_owner(d + 1), tx_owner(d + 1);
  auto& u0 = u0_owner.v;
  auto& tx = tx_owner.v;  // t_x = (x tau + Ymin)/d
  for (long x : rows) {
    // u0 >= -1/2  <=>  (x tau + Y)/d >= 1 + cen - 1/2 = tau/2 :  Ymin = ceil(d tau/2 - x tau)
    arb_mul_si(tmp, tau, d, wp);
    arb_mul_2exp_si(tmp, tmp, -1);
    arb_mul_si(tmp2, tau, x, wp);
    arb_sub(tmp, tmp, tmp2, wp);
    sic::support::flint::Fmpz fz;
    arf_get_fmpz(fz, arb_midref(tmp), ARF_RND_CEIL);
    Ymin[x] = fmpz_get_si(fz);
    arb_mul_si(&tx[x], tau, x, wp);
    arb_add_si(&tx[x], &tx[x], Ymin[x], wp);
    arb_div_si(&tx[x], &tx[x], d, wp);
    arb_sub_ui(&u0[x], &tx[x], 1, wp);
    arb_sub(&u0[x], &u0[x], cen, wp);
    // check -umax <= u0 and u0 + (d-1)/d <= umax
    arb_set_d(tmp, -umax);
    bool ok = arb_ge(&u0[x], tmp);
    arb_set_si(tmp2, d - 1);
    arb_div_si(tmp2, tmp2, d, wp);
    arb_add(tmp2, tmp2, &u0[x], wp);
    arb_set_d(tmp, umax);
    ok = ok && arb_le(tmp2, tmp);
    if (!ok) throw std::runtime_error("batch_state: u out of range");
  }
  // distinct row increments Ymin(x+1) - Ymin(x), x = 1..half-1
  std::vector<long> dlist;
  for (long x = 1; x < half; ++x) {
    long dY = Ymin[x + 1] - Ymin[x];
    if (std::find(dlist.begin(), dlist.end(), dY) == dlist.end()) dlist.push_back(dY);
  }

  auto clk = []() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
  };
  double tc0 = clk();
  table_cleanup::Nodes node_owner;
  auto& nodes = node_owner.values;
  table_cleanup::Magnitude error_owner;
  mag_ptr err = error_owner.value;
  auto admit_nodes = [&](size_t count) {
    const auto forecast = kernel_bytes(count, R, dlist.size(), wp);
    if (admit_workspace) admit_workspace(forecast);
    auto previous = kernel_peak.load();
    while (forecast > previous && !kernel_peak.compare_exchange_weak(previous, forecast)) {
    }
    long old_nodes = maximum_nodes.load();
    while ((long)count > old_nodes &&
           !maximum_nodes.compare_exchange_weak(old_nodes, (long)count)) {
    }
  };
  double log2err = gl_nodes(nodes, b, eta, wp, umax, err, admit_nodes);
  double tc1 = clk();
  const long total_nodes = (long)nodes.size();
  if (sic::support::kernel_setting("SIC_PROGRESS"))
    sic::support::progressf(
        "quadrature nodes ready: class=%ld state=%ld nodes=%ld slab=%ld kernel_forecast=%llu "
        "bytes\n",
        (long)c, (long)l, total_nodes, node_slab,
        (unsigned long long)kernel_bytes(total_nodes, R, dlist.size(), wp));

  // On the line w = x + i eta, e^{2 w s / b} = e^{2 x s / b} e^{2 i eta s / b}: the phase does
  // not depend on the node. So C[x][j] = phase(x, j) * sum_k At[x][k] Bt[j][k] with complex
  // At[x][k] = c_k e^{2 x_k t_x / b} and real Bt[j][k] = e^{2 x_k j / (d b)}: two real dot
  // products per point.
  sic::support::flint::Arb sQ, sP, r2b;
  arb_one(sQ);
  arb_div_si(sQ, sQ, d, wp);             // Q = e^{2 w (1/d) / b}
  arb_mul(sP, tau, sQ, wp);              // P = e^{2 w (tau/d) / b}
  arb_mul_2exp_si(r2b, binv, 1);         // 2/b
  sic::support::flint::Acb ph2b, ph2bt;  // e^{2 i eta / b}, e^{2 i eta tau / b}
  arb_mul(tmp, eta, r2b, wp);
  arb_sin_cos(acb_imagref(ph2b), acb_realref(ph2b), tmp, wp);
  arb_mul(tmp, tmp, tau, wp);
  arb_sin_cos(acb_imagref(ph2bt), acb_realref(ph2bt), tmp, wp);

  sic::support::ComplexBallVector integrals(R * d);
  auto sum_nodes = [&](long node_start, long N) {
    // Per-node data are local to this slab. The phase and global integration
    // error are applied once, after the complete unphased sum is accumulated.
    sic::support::RealBallVector ARe_owner(R * N), AIm_owner(R * N), Bt_owner(d * N);
    auto& ARe = ARe_owner.v;
    auto& AIm = AIm_owner.v;
    auto& Bt = Bt_owner.v;  // At[r][k], Bt[j][k]
    std::vector<sic::support::RealBallVector> PQ;
    PQ.reserve(dlist.size());
    for (size_t q = 0; q < dlist.size(); ++q) PQ.emplace_back(N);
    {
      parallel_for(N, nthreads, [&](long k) {
        sic::support::flint::Acb w, E1s, E2s, ck, t1;
        sic::support::flint::Arb xr, qr, pr, e1, e2, ra;
        {
          const arb_struct* xk = &nodes[node_start + k].abscissa;
          arb_set(acb_realref(w), xk);
          arb_set(acb_imagref(w), eta);
          arb_mul(xr, xk, r2b, wp);  // 2 x_k / b
          arb_mul(ra, xr, sQ, wp);
          arb_exp(qr, ra, wp);  // e^{2 x/(d b)}
          arb_mul(ra, xr, sP, wp);
          arb_exp(pr, ra, wp);  // e^{2 x tau/(d b)} = e^{2 x b/d}
          arb_set(e2, qr);
          arb_set(e1, pr);
          for (long m = 1; m < d; m *= 2) {
            arb_sqr(e2, e2, wp);
            arb_sqr(e1, e1, wp);
          }  // ^d: e^{2x/b}, e^{2 x b}
          acb_mul_arb(E2s, ph2b, e2, wp);
          acb_sub_ui(E2s, E2s, 1, wp);  // E2^2 - 1
          acb_mul_arb(E1s, ph2bt, e1, wp);
          acb_sub_ui(E1s, E1s, 1, wp);  // E1^2 - 1
          acb_mul(t1, E1s, E2s, wp);
          acb_mul(t1, t1, w, wp);
          acb_inv(ck, t1, wp);
          acb_mul_arb(ck, ck, &nodes[node_start + k].weight, wp);
          for (size_t i = 0; i < dlist.size(); ++i) {  // e^{2 x (tau + dY)/(d b)} = pr qr^dY
            arb_pow_ui(ra, qr, (ulong)std::labs(dlist[i]), wp);
            if (dlist[i] < 0)
              arb_div(PQ[i][k], pr, ra, wp);
            else
              arb_mul(PQ[i][k], pr, ra, wp);
          }
          arb_mul(ra, xr, &tx[1], wp);
          arb_exp(ra, ra, wp);
          arb_mul(&ARe[k], acb_realref(ck), ra, wp);
          arb_mul(&AIm[k], acb_imagref(ck), ra, wp);
          arb_mul(ra, xr, &tx[d], wp);
          arb_exp(ra, ra, wp);
          arb_mul(&ARe[(R - 1) * N + k], acb_realref(ck), ra, wp);
          arb_mul(&AIm[(R - 1) * N + k], acb_imagref(ck), ra, wp);
          {
            arb_one(&Bt[k]);
            for (long j = 1; j < d; ++j) arb_mul(&Bt[j * N + k], &Bt[(j - 1) * N + k], qr, wp);
          }
        }
        for (long x = 2; x <= half; ++x) {
          long dY = Ymin[x] - Ymin[x - 1];
          size_t i = std::find(dlist.begin(), dlist.end(), dY) - dlist.begin();
          {
            arb_mul(&ARe[(x - 1) * N + k], &ARe[(x - 2) * N + k], PQ[i][k], wp);
            arb_mul(&AIm[(x - 1) * N + k], &AIm[(x - 2) * N + k], PQ[i][k], wp);
          }
        }
      });
    }
    parallel_for(R, nthreads, [&](long r) {
      sic::support::ComplexBallVector value(1);
      for (long j = 0; j < d; ++j) {
        arb_dot(acb_realref(value[0]), NULL, 0, &ARe[r * N], 1, &Bt[j * N], 1, N, wp);
        arb_dot(acb_imagref(value[0]), NULL, 0, &AIm[r * N], 1, &Bt[j * N], 1, N, wp);

        if (node_start == 0)
          acb_set(integrals[r * d + j], value[0]);
        else
          acb_add(integrals[r * d + j], integrals[r * d + j], value[0], wp);
      }
    });
  };
  const long slab = node_slab ? std::min(node_slab, total_nodes) : total_nodes;
  for (long first = 0; first < total_nodes;) {
    const long count = std::min(slab, total_nodes - first);
    sum_nodes(first, count);
    first += count;
  }
  node_owner.clear();
  double tc2 = clk();

  // shift factors: e((z0 + m)/tau) = E0 om^m, om = e(1/tau); in row x, E0(j) = e((cen +
  // u0(x))/tau) psi^j, psi = e(1/(d tau))
  sic::support::flint::Acb om, omi, psi, e2c;

  arb_inv(tmp, tau, wp);
  acb_e(om, tmp, wp);
  acb_conj(omi, om);
  arb_div_si(tmp, tmp, d, wp);
  acb_e(psi, tmp, wp);
  arb_div(tmp, cen, tau, wp);
  arb_mul_2exp_si(tmp, tmp, 1);
  acb_e(e2c, tmp, wp);  // e(2 cen / tau)
  // constant of the reflection: pi i (tau + 1/tau)/12 ; and -pi i u^2/tau
  sic::support::flint::Acb refc;
  arb_inv(tmp, tau, wp);
  arb_add(tmp, tmp, tau, wp);
  arb_const_pi(tmp2, wp);
  arb_mul(tmp, tmp, tmp2, wp);
  arb_div_ui(tmp, tmp, 12, wp);
  arb_zero(acb_realref(refc));
  arb_set(acb_imagref(refc), tmp);

  sic::support::flint::Acb psh;
  arb_mul(tmp, eta, r2b, wp);
  arb_div_si(tmp, tmp, d, wp);
  arb_sin_cos(acb_imagref(psh), acb_realref(psh), tmp, wp);  // e^{2 i eta/(d b)}
  parallel_for(R, nthreads, [&](long r) {
    sic::support::flint::Acb E0, E0r, Er, fct, S, Ival, res, phx;

    sic::support::flint::Arb row_tmp, row_tmp2, row_u;

    const long x = rows[r];
    sic::support::BalancedComplexProduct product(std::labs(Ymin[x] / d) + 3);
    auto shift_mult = [&](acb_t Sres, const acb_t E0v, long k) {
      acb_one(Sres);
      product.reset();
      if (k > 0) {
        acb_set(Er, E0v);
        for (long m = 1; m <= k; ++m) {
          acb_mul(Er, Er, om, wp);
          acb_sub_ui(fct, Er, 1, wp);
          acb_neg(fct, fct);
          product.append(fct, wp);
        }
        product.get(Sres, wp);
        acb_inv(Sres, Sres, wp);
      } else if (k < 0) {
        acb_set(Er, E0v);
        for (long m = 0; m < -k; ++m) {
          if (m > 0) acb_mul(Er, Er, omi, wp);
          acb_sub_ui(fct, Er, 1, wp);
          acb_neg(fct, fct);
          product.append(fct, wp);
        }
        product.get(Sres, wp);
      }
    };
    arb_add(row_tmp, cen, &u0[x], wp);
    arb_div(row_tmp, row_tmp, tau, wp);
    acb_e(E0, row_tmp, wp);
    arb_mul(row_tmp, eta, r2b, wp);
    arb_mul(row_tmp, row_tmp, &tx[x], wp);
    arb_sin_cos(acb_imagref(phx), acb_realref(phx), row_tmp, wp);  // e^{2 i eta t_x / b}
    for (long j = 0; j < d; ++j) {
      if (j > 0) {
        acb_mul(E0, E0, psi, wp);
        acb_mul(phx, phx, psh, wp);
      }
      acb_set(Ival, integrals[r * d + j]);
      acb_mul(Ival, Ival, phx, wp);
      acb_add_error_mag(Ival, err);
      const long Y = Ymin[x] + j;
      const long y = ((Y % d) + d) % d;
      const long k = (y - Y) / d;
      const long id = (x - 1) * d + y;
      acb_set_round(&out.L[id], Ival, prec + 20);
      shift_mult(S, E0, k);
      acb_set_round(&out.S[id], S, prec + 20);
      if (x < half) {
        // reflected point (d - x, -y): u' = -u, z0' = cen - u, E0' = e(2 cen/tau) conj(E0)
        const long x2 = d - x, y2 = (d - y) % d, k2 = (y2 + Y) / d - 1;
        arb_set_si(row_u, j);
        arb_div_si(row_u, row_u, d, wp);
        arb_add(row_u, row_u, &u0[x], wp);
        acb_neg(res, Ival);
        arb_sqr(row_tmp, row_u, wp);
        arb_div(row_tmp, row_tmp, tau, wp);
        arb_const_pi(row_tmp2, wp);
        arb_mul(row_tmp, row_tmp, row_tmp2, wp);
        arb_sub(acb_imagref(res), acb_imagref(res), row_tmp, wp);
        acb_add(res, res, refc, wp);
        const long id2 = (x2 - 1) * d + y2;
        acb_set_round(&out.L[id2], res, prec + 20);
        acb_conj(E0r, E0);
        acb_mul(E0r, E0r, e2c, wp);
        shift_mult(S, E0r, k2);
        acb_set_round(&out.S[id2], S, prec + 20);
      }
    }
  });
  out.nodes = total_nodes;
  out.log2err = log2err;
  out.t_nodes = tc1 - tc0;
  out.t_setup = tc2 - tc1;
  out.t_main = clk() - tc2;

  if (sic::support::kernel_setting("SIC_PROGRESS"))
    sic::support::progressf(
        "quadrature done: class=%ld state=%ld elapsed=%.2fs RSS=%.1f MiB\n", (long)c, (long)l,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - state_started).count(),
        sic::support::resources::inspect().rss / (double)sic::support::resources::kMib);
}
}  // namespace sic::exact
