// SPDX-License-Identifier: AGPL-3.0-or-later
#include "search/stabilizer.hpp"
namespace sic::search {

sic::core::clifford::Int element_order(const sic::core::clifford::CliffordElement& el,
                                       sic::core::clifford::Int d) {
  const sic::core::clifford::Mat2 f = sic::core::clifford::reduce(el.F, d), id{1, 0, 0, 1};
  sic::core::clifford::Mat2 x = f;
  sic::core::clifford::Int s = el.sign;
  for (sic::core::clifford::Int k = 1; k <= 8 * d * d; ++k) {
    if (sic::core::clifford::reduce(x, d) == sic::core::clifford::reduce(id, d) && s == 1) return k;
    x = sic::core::clifford::mul(x, f, d);
    s *= el.sign;
  }
  return 0;
}

bool write_stabilizer(std::FILE* out, const std::string& label, const std::string& order,
                      const std::string& status, const sic::core::Vector& phi,
                      const StabilizerOptions& o) {
  const sic::core::clifford::Int d = static_cast<sic::core::clifford::Int>(phi.size());
  const sic::core::clifford::Int db = sic::core::clifford::dbar_of(d);
  const sic::core::SicCheck chk = sic::core::check_sic(phi);
  const double bound = sic::search::z_error_bound(d, o.entry_error);
  sic::search::MatchOptions mo;
  mo.z_tol = o.z_tol;
  mo.residual_tol = o.residual_tol;
  mo.find_all = true;
  sic::search::MatchStats st;
  auto stab = sic::search::clifford_maps(phi, phi, mo, &st);
  std::sort(stab.begin(), stab.end(), [&](const auto& x, const auto& y) {
    const auto ox = element_order(x, d), oy = element_order(y, d);
    if (ox != oy) return ox < oy;
    return std::tie(x.sign, x.F.a, x.F.b, x.F.c, x.F.e) >
           std::tie(y.sign, y.F.a, y.F.b, y.F.c, y.F.e);
  });
  bool closed = true, has_identity = false;
  for (const auto& x : stab) {
    if (sic::core::clifford::reduce(x.F, d) ==
            sic::core::clifford::reduce(sic::core::clifford::Mat2{1, 0, 0, 1}, d) &&
        x.sign == 1)
      has_identity = true;
    for (const auto& y : stab) {
      const sic::core::clifford::Mat2 xy = sic::core::clifford::mul(
          sic::core::clifford::reduce(x.F, d), sic::core::clifford::reduce(y.F, d), d);
      bool in = false;
      for (const auto& z : stab)
        in = in || (sic::core::clifford::reduce(z.F, d) == xy && z.sign == x.sign * y.sign);
      closed = closed && in;
    }
  }
  std::size_t unitary = 0, zauner = 0;
  double max_res = 0, max_zdev = 0;
  for (const auto& x : stab) {
    unitary += x.sign == 1;
    max_res = std::max(max_res, std::abs(x.residual));
    max_zdev = std::max(max_zdev, x.z_deviation);
  }
  const bool exhaustive = 2 * bound <= o.z_tol;
  const bool ok = chk.passes(o.sic_tol) && closed && has_identity && exhaustive;
  std::fprintf(
      out,
      "ray %s d %lld order %s status %s group_order %zu unitary_order %zu closed %s exhaustive %s "
      "max_overlap_error %.3e z_tol %.1e z_error_bound %.3e max_element_z_deviation %.3e "
      "max_element_residual %.3e min_rejected_z_deviation %.3e min_rejected_residual %.3e "
      "level_set_sizes %zu candidate_pairs %zu\n",
      label.c_str(), static_cast<long long>(d), order.c_str(), status.c_str(), stab.size(), unitary,
      closed ? "yes" : "NO", exhaustive ? "yes" : "NO", chk.max_overlap_error, o.z_tol, bound,
      max_zdev, max_res, st.best_rejected_z, st.best_rejected_residual, st.level_set_sizes,
      st.candidate_pairs);
  std::size_t index = 0;
  for (const auto& x : stab) {
    const sic::core::clifford::Int ord = element_order(x, d),
                                   tr = sic::core::clifford::trace(x.F, db);
    const bool z_type =
        ord == 3 && x.sign == 1 &&
        sic::core::clifford::mod(tr, d) == sic::core::clifford::mod(-1, d);  // trace -1 mod d
    zauner += z_type;
    std::fprintf(
        out,
        "elem %s %zu det %+lld F %lld %lld %lld %lld chi %lld %lld order %lld trace %lld%s z_dev "
        "%.3e residual %.3e\n",
        label.c_str(), index++, static_cast<long long>(x.sign), static_cast<long long>(x.F.a),
        static_cast<long long>(x.F.b), static_cast<long long>(x.F.c), static_cast<long long>(x.F.e),
        static_cast<long long>(x.chi1), static_cast<long long>(x.chi2), static_cast<long long>(ord),
        static_cast<long long>(tr), z_type ? " zauner" : "", x.z_deviation, std::abs(x.residual));
  }
  std::fflush(out);
  std::fprintf(stderr, "%s: |Stab| = %zu (unitary %zu, Zauner-type order-3 elements %zu)%s\n",
               label.c_str(), stab.size(), unitary, zauner, ok ? "" : "  CHECK FAILED");
  return ok;
}
}  // namespace sic::search
