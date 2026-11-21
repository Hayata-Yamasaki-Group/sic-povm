// SPDX-License-Identifier: AGPL-3.0-or-later
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "exact/analytic/table.hpp"

namespace {

void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}

struct ArbValue {
  arb_t v;
  ArbValue() { arb_init(v); }
  ~ArbValue() { arb_clear(v); }
  ArbValue(const ArbValue&) = delete;
  ArbValue& operator=(const ArbValue&) = delete;
};

struct AcbValue {
  acb_t v;
  AcbValue() { acb_init(v); }
  ~AcbValue() { acb_clear(v); }
  AcbValue(const AcbValue&) = delete;
  AcbValue& operator=(const AcbValue&) = delete;
};

struct MagValue {
  mag_t v;
  MagValue() { mag_init(v); }
  ~MagValue() { mag_clear(v); }
  MagValue(const MagValue&) = delete;
  MagValue& operator=(const MagValue&) = delete;
};

struct IntegrandParam {
  sic::exact::FaddeevParam value;
};

void test_invalid_inputs(slong p) {
  ArbValue b, eta, pi, M, length;
  arb_one(b.v);
  arb_const_pi(pi.v, p);
  arb_set(eta.v, pi.v);
  bool rejected = false;
  try {
    sic::exact::certified::ContourBounds bad(b.v, eta.v, 0.5, p);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  require(rejected, "contour crossing a pole was accepted");

  arb_set_d(eta.v, 1.0);
  sic::exact::certified::ContourBounds bounds(b.v, eta.v, 0.5, p);
  require(!bounds.central(M.v, 100.0), "ellipse crossing a pole was accepted");
  require(!bounds.off_axis(M.v, 1.0, 1.0), "invalid off-axis ellipse radius was accepted");
  arb_zero(length.v);
  MagValue error;
  rejected = false;
  try {
    bounds.panel_error(error.v, M.v, length.v, 2.0, 4);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  require(rejected, "zero-length panel was accepted");

  sic::exact::Arithmetic ar(2);
  sic::exact::Table table(ar, p);
  std::vector<sic::exact::QuadratureNode> nodes;
  arb_add_error_2exp_si(eta.v, -p);
  rejected = false;
  try {
    table.gl_nodes(nodes, b.v, eta.v, p + 40, 0.5, error.v);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "inexact contour height was silently replaced by its midpoint");
}

void test_bounds(slong p) {
  ArbValue b, eta, Mcentral, Moff, length;
  arb_set_d(b.v, 1.5);
  arb_set_d(eta.v, 0.8);
  sic::exact::certified::ContourBounds bounds(b.v, eta.v, 0.5, p);
  require(bounds.central(Mcentral.v, 1.25), "valid central ellipse was rejected");
  require(arb_is_positive(Mcentral.v) && arb_is_finite(Mcentral.v),
          "central bound is not finite and positive");
  require(bounds.off_axis(Moff.v, 0.8, 2.0), "valid off-axis ellipse was rejected");
  require(arb_is_positive(Moff.v) && arb_is_finite(Moff.v),
          "off-axis bound is not finite and positive");
  arb_set_d(length.v, 0.8);
  MagValue panel, tail1, tail2;
  bounds.panel_error(panel.v, Mcentral.v, length.v, 1.25, 32);
  require(!mag_is_zero(panel.v) && !mag_is_inf(panel.v), "panel error is not finite and positive");
  bounds.tail_error(tail1.v, 2.0);
  bounds.tail_error(tail2.v, 4.0);
  require(!mag_is_zero(tail1.v) && !mag_is_inf(tail1.v), "tail error is not finite and positive");
  require(mag_cmp(tail2.v, tail1.v) < 0, "tail bound did not decrease as the endpoint increased");
}

double contour_height(double bmid) { return M_PI * std::min(bmid, 1.0 / bmid) / 2.0; }

double quadrature_endpoint(const std::vector<sic::exact::QuadratureNode>& nodes, double eta) {
  double largest = 0.0;
  for (const auto& node : nodes) {
    const double x = std::abs(arf_get_d(arb_midref(&node.abscissa), ARF_RND_NEAR));
    largest = std::max(largest, x);
  }
  double endpoint = eta;
  while (endpoint <= largest) {
    endpoint *= 2.0;
    if (!std::isfinite(endpoint))
      throw std::runtime_error("could not recover finite panel endpoint");
  }
  return endpoint;
}

void clear_nodes(std::vector<sic::exact::QuadratureNode>& nodes) {
  for (auto& node : nodes) {
    arb_clear(&node.abscissa);
    arb_clear(&node.weight);
  }
  nodes.clear();
}

void compare_quadrature_with_reference(sic::exact::Table& table, double bmid, slong p, double u) {
  const slong wp = p + 40;
  const double eta_mid = contour_height(bmid);
  const double umax = 0.500000001;
  ArbValue b, eta, t, endpoint_arb;
  arb_set_d(b.v, bmid);
  // Give b a nonzero radius at each working precision. The production bounds
  // must cover the whole input ball, while eta remains the selected dyadic line.
  arb_add_error_2exp_si(b.v, -p + 1);
  arb_set_d(eta.v, eta_mid);

  std::vector<sic::exact::QuadratureNode> nodes;
  MagValue quadrature_error, error_budget;
  table.gl_nodes(nodes, b.v, eta.v, wp, umax, quadrature_error.v);
  mag_set_ui_2exp_si(error_budget.v, 1, -(p + 24));
  require(mag_cmp(quadrature_error.v, error_budget.v) <= 0,
          "production quadrature error exceeds 2^-(prec+24)");
  const double endpoint = quadrature_endpoint(nodes, eta_mid);
  arb_set_d(endpoint_arb.v, endpoint);

  IntegrandParam param;
  arb_set(param.value.b, b.v);
  arb_inv(param.value.binv, param.value.b, wp);
  arb_set_d(t.v, u);
  arb_mul_2exp_si(t.v, t.v, 1);
  arb_div(param.value.nu2, t.v, param.value.b, wp);

  AcbValue production, w, f;
  acb_zero(production.v);
  for (const auto& node : nodes) {
    arb_set(acb_realref(w.v), &node.abscissa);
    arb_set(acb_imagref(w.v), eta.v);
    sic::exact::faddeev_integrand(f.v, w.v, &param.value, 0, wp);
    acb_mul_arb(f.v, f.v, &node.weight, wp);
    acb_add(production.v, production.v, f.v, wp);
  }
  acb_add_error_mag(production.v, quadrature_error.v);

  AcbValue a, z, reference;
  arb_neg(acb_realref(a.v), endpoint_arb.v);
  arb_set(acb_imagref(a.v), eta.v);
  arb_set(acb_realref(z.v), endpoint_arb.v);
  arb_set(acb_imagref(z.v), eta.v);
  MagValue integration_tolerance, tail_error;
  mag_set_ui_2exp_si(integration_tolerance.v, 1, -(p + 32));
  acb_calc_integrate_opt_t opt;
  acb_calc_integrate_opt_init(opt);
  opt->depth_limit = 4 * wp;
  opt->eval_limit = 10000000;
  const int status = acb_calc_integrate(reference.v, sic::exact::faddeev_integrand, &param.value,
                                        a.v, z.v, wp, integration_tolerance.v, opt, wp);
  require(status == ARB_CALC_SUCCESS, "independent acb_calc_integrate failed");

  sic::exact::certified::ContourBounds bounds(b.v, eta.v, umax, wp);
  bounds.tail_error(tail_error.v, endpoint);
  acb_add_error_mag(reference.v, tail_error.v);
  require(acb_overlaps(production.v, reference.v),
          "production Gauss rule with its certified error misses the reference integral plus tail");

  clear_nodes(nodes);
}

void test_reference_integrals(slong p) {
  sic::exact::Arithmetic ar(2);
  sic::exact::Table table(ar, p);
  const std::array<double, 3> bs{1.1, 1.8, 3.0};
  for (double b : bs) {
    for (double u : {-0.500000001, 0.0, 0.500000001})
      compare_quadrature_with_reference(table, b, p, u);
  }
}

void test_sparse_class_tables() {
  sic::exact::Arithmetic ar(3);
  sic::exact::Table sparse(ar, 80), dense(ar, 80);
  sparse.build_class(0);
  require(sparse.val.size() == static_cast<size_t>(ar.h()), "missing class slots");
  for (int c = 0; c < ar.h(); ++c)
    require(sparse.val[c].size() == (c == 0 ? static_cast<size_t>(ar.d * ar.d) : 0),
            "singleton table allocated an unrelated class");
  dense.build_batched();
  for (int c = 0; c < ar.h(); ++c) {
    if (c != 0) sparse.build_class(c);
    for (long q = 0; q < ar.d * ar.d; ++q)
      require(acb_equal(&sparse.val[c][q], &dense.val[c][q]),
              "sparse class differs from full-table computation");
  }
  bool rejected = false;
  try {
    sparse.build_class(ar.h());
  } catch (const std::out_of_range&) {
    rejected = true;
  }
  require(rejected, "out-of-range sparse class was accepted");
  sic::exact::Table reference(ar, 240);
  reference.build_class(0);
  for (long q = 1; q < ar.d * ar.d; ++q)
    require(acb_overlaps(&sparse.val[0][q], &reference.val[0][q]),
            "shift products disagree across precision");

  sic::exact::Table stopped(ar, 80);
  struct StopClass {};
  int completed = 0;
  stopped.class_completed = [&](int c) {
    require(c == 0, "class completion check was delayed");
    ++completed;
    throw StopClass{};
  };
  bool stopped_early = false;
  try {
    stopped.build_batched();
  } catch (const StopClass&) {
    stopped_early = true;
  }
  require(stopped_early && completed == 1, "class validation did not stop the build");
  require(stopped.sd[stopped.soff[1]].nodes == 0,
          "later class was computed after validation failed");
}

}  // namespace

int main() {
  try {
    for (slong p : {64, 160}) {
      test_invalid_inputs(p);
      test_bounds(p);
      test_reference_integrals(p);
    }
    test_sparse_class_tables();
    std::cout << "quadrature bounds tests passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "quadrature bounds test failed: " << e.what() << '\n';
    return 1;
  }
}
