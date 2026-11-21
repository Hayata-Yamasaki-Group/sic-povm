// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/analytic/table/quadrature.hpp"

#include <flint/arb_hypgeom.h>
#include <flint/arb_mat.h>

#include <chrono>

#include "exact/analytic/table/faddeev.hpp"
#include "exact/certification/bounds/quadrature_bounds.hpp"
#include "exact/runtime/parallel.hpp"
#include "sic/error.hpp"
#include "support/resources.hpp"
namespace sic::exact {

const std::vector<QuadratureNode>& TableView::gl_rule(long n, slong wp) {
  const QuadratureRuleKey key{n, wp};
  {
    std::lock_guard<std::mutex> lk(gl_mutex);
    auto it = gl_cache.find(key);
    if (it != gl_cache.end()) return it->second;
  }
  table_cleanup::Nodes owner(n);
  auto& v = owner.values;
  parallel_for(n, nthreads, [&](long k) {
    arb_hypgeom_legendre_p_ui_root(&v[k].abscissa, &v[k].weight, (ulong)n, (ulong)k, wp);
  });
  std::lock_guard<std::mutex> lk(gl_mutex);
  auto [it, inserted] = gl_cache.try_emplace(key);
  if (inserted) it->second.swap(v);
  return it->second;  // map nodes are stable; owner clears concurrent duplicates
}
double TableView::log_sinh(double x) {
  return x > 20 ? x - M_LN2 + std::log1p(-std::exp(-2 * x)) : std::log(std::sinh(x));
}
double TableView::gl_nodes(std::vector<QuadratureNode>& nodes, const arb_t b, const arb_t eta,
                           slong wp, double umax, mag_t error,
                           const std::function<void(size_t)>& admit_nodes) {
  if (!arb_is_exact(eta))
    throw std::invalid_argument("gl_nodes: contour height must be an exact dyadic");
  const double bd = arf_get_d(arb_midref(b), ARF_RND_NEAR);
  const double etad = arf_get_d(arb_midref(eta), ARF_RND_NEAR);
  certified::ContourBounds bounds(b, eta, umax, wp);
  const double kap = bd + (1 - 2 * umax) / bd;
  const double cfac = (1 - std::exp(-2 * bd)) * (1 - std::exp(-2 / bd));
  const double target = -(double)(prec + 24);
  const double R = (-(target - 2) * M_LN2 + std::log(4.0 / (kap * cfac))) / kap + 1;
  if (!std::isfinite(R) || R <= 0 || !std::isfinite(etad) || etad <= 0)
    throw sic::Error(sic::ErrorKind::precision, "gl_nodes: invalid proposed panel geometry");
  struct Panel {
    double lower, upper, ellipse_radius, log2_magnitude;
  };
  std::vector<Panel> panels;
  auto nodes_for = [&](double l2M, double rho, double L) {
    // Conservative Chebyshev bound: 4 L M rho^(-2n)/(1-rho^-1).
    const double base = 2 + std::log2(L) + l2M - std::log2(1 - 1 / rho);
    return std::max(4.0, std::ceil((base - (target - 8)) / (2 * std::log2(rho))));
  };
  {
    double bestn = 1e300, bestrho = 1.25, bestM = 0;
    for (double rho = 1.25; rho < 2.4; rho += 0.05) {
      const double beta = etad * (rho - 1 / rho) / 2;
      const double a = etad * (rho + 1 / rho) / 2;
      const double y0 = etad - beta, y1 = etad + beta;
      if (y0 <= 0 || bd * y1 >= M_PI || y1 / bd >= M_PI) continue;
      const double s1 = std::min(std::sin(bd * y0), std::sin(bd * y1));
      const double s2 = std::min(std::sin(y0 / bd), std::sin(y1 / bd));
      if (s1 <= 0 || s2 <= 0) continue;
      const double l2M = (2 * umax * a / bd - std::log(4 * s1 * s2 * y0)) / M_LN2;
      const double n = nodes_for(l2M, rho, 2 * etad);
      if (n < bestn) {
        bestn = n;
        bestrho = rho;
        bestM = l2M;
      }
    }
    panels.emplace_back(-etad, etad, bestrho, bestM);
  }
  auto append_dyadic = [&](double x0) {
    if (!std::isfinite(2 * x0))
      throw sic::Error(sic::ErrorKind::precision, "gl_nodes: panel overflow");
    double bestn = 1e300, bestrho = 4, bestM = 0;
    for (double rho = 1.5; rho < 5.8; rho += 0.1) {
      const double r0 = x0 * (1.5 - (rho + 1 / rho) / 4);
      if (r0 <= 0) continue;
      const double l2M = (2 * umax * r0 / bd - std::log(4.0) - log_sinh(bd * r0) -
                          log_sinh(r0 / bd) - std::log(r0)) /
                         M_LN2;
      const double n = nodes_for(l2M, rho, x0);
      if (n < bestn) {
        bestn = n;
        bestrho = rho;
        bestM = l2M;
      }
    }
    panels.emplace_back(x0, 2 * x0, bestrho, bestM);
    panels.emplace_back(-2 * x0, -x0, bestrho, bestM);
  };
  double endpoint = etad;
  while (endpoint < R) {
    append_dyadic(endpoint);
    endpoint *= 2;
  }
  table_cleanup::Magnitude magnitudes[5];
  mag_ptr tail = magnitudes[0].value, tail_budget = magnitudes[1].value;
  mag_ptr panel_bound = magnitudes[2].value, panel_budget = magnitudes[3].value;
  mag_ptr total_budget = magnitudes[4].value;
  mag_set_ui_2exp_si(total_budget, 1, -(prec + 24));
  mag_set_ui_2exp_si(tail_budget, 1, -(prec + 26));
  bounds.tail_error(tail, endpoint);
  while (mag_cmp(tail, tail_budget) > 0) {
    append_dyadic(endpoint);
    endpoint *= 2;
    bounds.tail_error(tail, endpoint);
  }
  slong share_bits = 2;
  for (size_t share = 1; share < panels.size(); share *= 2) ++share_bits;
  mag_set_ui_2exp_si(panel_budget, 1, -(prec + 24 + share_bits));
  mag_set(error, tail);
  for (auto& [lo, hi, rho, l2M] : panels) {
    certified::Ball M, mid, half, lo_a, hi_a, length;
    bool valid = lo < 0 && hi > 0 ? bounds.central(M.v, rho)
                                  : bounds.off_axis(M.v, std::min(std::abs(lo), std::abs(hi)), rho);
    if (!valid)
      throw sic::Error(sic::ErrorKind::precision, "gl_nodes: ellipse bound not certified");
    arb_set_d(lo_a.v, lo);
    arb_set_d(hi_a.v, hi);
    arb_add(mid.v, lo_a.v, hi_a.v, wp);
    arb_mul_2exp_si(mid.v, mid.v, -1);
    arb_sub(length.v, hi_a.v, lo_a.v, wp);
    arb_mul_2exp_si(half.v, length.v, -1);
    const double proposed = nodes_for(l2M, rho, hi - lo);
    if (!std::isfinite(proposed) || proposed > 10000000)
      throw sic::Error(sic::ErrorKind::resource, "gl_nodes: proposed node count is not practical");
    ulong n = (ulong)proposed;
    bounds.panel_error(panel_bound, M.v, length.v, rho, n);
    while (mag_cmp(panel_bound, panel_budget) > 0) {
      n += std::max<ulong>(1, n / 16);
      bounds.panel_error(panel_bound, M.v, length.v, rho, n);
    }
    if (admit_nodes) admit_nodes(sic::support::resources::add(nodes.size(), n));
    mag_add(error, error, panel_bound);
    nodes.reserve(sic::support::resources::add(nodes.size(), n));
    const auto& rule = gl_rule((long)n, wp);
    for (ulong k = 0; k < n; ++k) {
      nodes.emplace_back();
      arb_init(&nodes.back().abscissa);
      arb_init(&nodes.back().weight);
      arb_mul(&nodes.back().abscissa, &rule[k].abscissa, half.v, wp);
      arb_add(&nodes.back().abscissa, &nodes.back().abscissa, mid.v, wp);
      arb_mul(&nodes.back().weight, &rule[k].weight, half.v, wp);
    }
  }
  if (mag_cmp(error, total_budget) > 0)
    throw sic::Error(sic::ErrorKind::precision, "gl_nodes: certified total error exceeds budget");
  const double diagnostic = mag_get_d_log2_approx(error);
  return diagnostic;  // diagnostic only; the certified mag is propagated
}
}  // namespace sic::exact

namespace sic::exact::table_cleanup {
Nodes::Nodes(size_t count) : values(count) {
  for (auto& pair : values) {
    arb_init(&pair.abscissa);
    arb_init(&pair.weight);
  }
}

void Nodes::clear() {
  for (auto& pair : values) {
    arb_clear(&pair.abscissa);
    arb_clear(&pair.weight);
  }
  values.clear();
}

}  // namespace sic::exact::table_cleanup
