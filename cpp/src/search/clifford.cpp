// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/search/clifford.hpp"

namespace sic::search {
namespace detail {
// Labels q != 0 with |z(q) - w| <= tol, using an angle-sorted index.
class LevelIndex {
 public:
  explicit LevelIndex(const PhaseTable& t) : t_(t) {
    for (std::size_t i = 1; i < t.z.size(); ++i) entries_.push_back({std::arg(t.z[i]), i});
    std::sort(entries_.begin(), entries_.end());
  }
  std::vector<std::size_t> find(sic::core::Complex w, sic::core::Real tol) const {
    std::vector<std::size_t> out;
    if (entries_.empty()) return out;
    const sic::core::Real pi = std::numbers::pi_v<sic::core::Real>, theta = std::arg(w);
    const sic::core::Real window =
        std::min(pi, 2 * std::asin(std::min<sic::core::Real>(1, tol / 2)) + 1e-12);
    auto scan = [&](sic::core::Real lo, sic::core::Real hi) {
      auto it =
          std::lower_bound(entries_.begin(), entries_.end(), std::make_pair(lo, std::size_t{0}));
      for (; it != entries_.end() && it->first <= hi; ++it)
        if (std::abs(t_.z[it->second] - w) <= tol) out.push_back(it->second);
    };
    sic::core::Real lo = theta - window, hi = theta + window;
    if (window >= pi) {
      scan(-pi - 1, pi + 1);
    } else {
      scan(std::max(lo, -pi - 1), std::min(hi, pi + 1));
      if (lo < -pi) scan(lo + 2 * pi, pi + 1);
      if (hi > pi) scan(-pi - 1, hi - 2 * pi);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
  }

 private:
  const PhaseTable& t_;
  std::vector<std::pair<sic::core::Real, std::size_t>> entries_;
};
}  // namespace detail
PhaseTable phase_table(const sic::core::SicFrame& frame) {
  PhaseTable t;
  t.d = frame.dim();
  const auto n = static_cast<std::size_t>(t.d);
  t.z.resize(n * n);
  t.min_abs_overlap = 1;
  sic::core::Vector row;
  const sic::core::Real two_d = 2 * static_cast<sic::core::Real>(t.d),
                        two_pi = 2 * std::numbers::pi_v<sic::core::Real>;
  for (std::size_t p1 = 0; p1 < n; ++p1) {
    frame.overlap_row(static_cast<sic::core::clifford::Int>(p1), row);
    for (std::size_t p2 = 0; p2 < n; ++p2) {
      const sic::core::Complex a = row[p2];
      if (p1 == 0 && p2 == 0) {
        t.z[0] = 1;
        continue;
      }
      t.min_abs_overlap = std::min(t.min_abs_overlap, std::abs(a));
      t.z[p1 * n + p2] =
          std::polar(sic::core::Real(1), std::remainder(two_d * std::arg(a), two_pi));
    }
  }
  return t;
}
}  // namespace sic::search

namespace sic::search {
std::vector<sic::core::Real> clifford_moments(const PhaseTable& t, int count) {
  std::vector<sic::core::Real> m(static_cast<std::size_t>(count), 0);
  if (t.d < 2) return m;
  for (std::size_t i = 1; i < t.z.size(); ++i) {
    sic::core::Complex w = t.z[i];
    for (int k = 0; k < count; ++k) {
      m[static_cast<std::size_t>(k)] += w.real();
      w *= t.z[i];
    }
  }
  for (sic::core::Real& x : m) x /= static_cast<sic::core::Real>(t.z.size() - 1);
  return m;
}
}  // namespace sic::search

namespace sic::search {
sic::core::Real z_error_bound(sic::core::clifford::Int d, sic::core::Real entry_error) {
  const sic::core::Real eps = 1.1102230246251565e-16, dd = static_cast<sic::core::Real>(d);
  const sic::core::Real da = 2 * std::sqrt(dd) * entry_error + dd * entry_error * entry_error +
                             (4 * std::log2(std::max<sic::core::Real>(dd, 2)) + 8) * eps;
  const sic::core::Real rel = da * std::sqrt(dd + 1);
  if (rel >= 0.5) return 2;
  // The last term covers the rounding of arg(a), of 2d * arg(a) and of the remainder/polar steps.
  return 2 * dd * (std::numbers::pi_v<sic::core::Real> / 2) * rel / (1 - rel) +
         (8 * dd * std::numbers::pi_v<sic::core::Real> + 8) * eps;
}
}  // namespace sic::search

namespace sic::search {
std::vector<sic::core::clifford::CliffordElement> clifford_maps(const sic::core::Vector& phi_in,
                                                                const sic::core::Vector& psi_in,
                                                                const MatchOptions& opt,
                                                                MatchStats* stats) {
  if (phi_in.size() != psi_in.size() || phi_in.empty())
    throw sic::Error(ErrorKind::invalid_argument, "clifford_maps: dimension mismatch");
  const sic::core::clifford::Int d = static_cast<sic::core::clifford::Int>(phi_in.size());
  const sic::core::clifford::Int db = sic::core::clifford::dbar_of(d);
  const sic::core::Vector psi = sic::core::normalized_copy(psi_in);
  const sic::core::SicFrame psi_frame(psi, false);
  const PhaseTable zpsi = phase_table(psi_frame);
  const sic::core::clifford::CliffordAction action(d);
  MatchStats local;
  MatchStats& st = stats ? *stats : local;
  std::vector<sic::core::clifford::CliffordElement> found;
  const auto n = static_cast<std::size_t>(d);

  for (int pass = 0; pass < (opt.antiunitary ? 2 : 1); ++pass) {
    sic::core::Vector phi = sic::core::normalized_copy(phi_in);
    if (pass == 1)
      for (sic::core::Complex& z : phi) z = std::conj(z);
    const PhaseTable zphi = phase_table(sic::core::SicFrame(phi, false));
    const detail::LevelIndex index(zphi);
    std::vector<std::size_t> q1s, q2s;
    if (d == 1) {
      q1s = q2s = {0};
    } else {
      q1s = index.find(zpsi.at(1, 0), opt.z_tol);
      q2s = index.find(zpsi.at(0, 1), opt.z_tol);
    }
    st.level_set_sizes += q1s.size() + q2s.size();
    for (std::size_t i1 : q1s) {
      const sic::core::clifford::Int g11 = static_cast<sic::core::clifford::Int>(i1 / n),
                                     g21 = static_cast<sic::core::clifford::Int>(i1 % n);
      for (std::size_t i2 : q2s) {
        const sic::core::clifford::Int g12 = static_cast<sic::core::clifford::Int>(i2 / n),
                                       g22 = static_cast<sic::core::clifford::Int>(i2 % n);
        const sic::core::clifford::Mat2 g{g11, g12, g21, g22};  // columns G e1, G e2
        if (sic::core::clifford::det(g, d) != sic::core::clifford::mod(1, d)) continue;
        ++st.candidate_pairs;
        sic::core::Real dev = 0;
        for (sic::core::clifford::Int p1 = 0; p1 < d; ++p1) {
          sic::core::clifford::Int q1 = sic::core::clifford::mulmod(g.a, p1, d),
                                   q2 = sic::core::clifford::mulmod(g.c, p1, d);
          for (sic::core::clifford::Int p2 = 0; p2 < d; ++p2) {
            const sic::core::Real e = std::abs(zpsi.z[static_cast<std::size_t>(p1 * d + p2)] -
                                               zphi.z[static_cast<std::size_t>(q1 * d + q2)]);
            dev = std::max(dev, e);
            q1 += g.b;
            if (q1 >= d) q1 -= d;
            q2 += g.e;
            if (q2 >= d) q2 -= d;
          }
        }
        if (!(dev <= opt.z_tol)) {
          if (st.best_rejected_z < 0 || dev < st.best_rejected_z) st.best_rejected_z = dev;
          continue;
        }
        ++st.z_survivors;
        // Unitary part: psi ~ D_chi U_Fs phi' with Fs = G^(-1) in SL(2, Z_dbar).
        const sic::core::clifford::Mat2 fs_mod_d = sic::core::clifford::inverse(g, d);
        sic::core::clifford::CliffordElement el;
        el.sign = pass == 0 ? 1 : -1;
        el.F = sic::core::clifford::canonical_lift(
            pass == 0 ? fs_mod_d : sic::core::clifford::mul(fs_mod_d, sic::core::clifford::kJ, d),
            d, el.sign);
        const sic::core::clifford::Mat2 fs =
            pass == 0 ? el.F
                      : sic::core::clifford::mul(el.F, sic::core::clifford::kJ,
                                                 db);  // U_F = U_(FJ) K, K applied above
        sic::core::Vector u;
        action.apply(fs, phi, u);
        const sic::core::SicFrame u_frame(u, false);
        sic::core::Vector row;
        sic::core::Real best = -1;
        for (sic::core::clifford::Int c1 = 0; c1 < d; ++c1) {
          u_frame.cross_overlap_row(c1, psi, row);
          for (sic::core::clifford::Int c2 = 0; c2 < d; ++c2) {
            const sic::core::Real m = std::abs(row[static_cast<std::size_t>(c2)]);
            if (m > best) {
              best = m;
              el.chi1 = c1;
              el.chi2 = c2;
            }
          }
        }
        el.z_deviation = dev;
        el.residual = 1 - best;
        if (std::abs(el.residual) <= opt.residual_tol) {
          found.push_back(el);
          if (!opt.find_all) return found;
        } else if (st.best_rejected_residual < 0 || el.residual < st.best_rejected_residual) {
          st.best_rejected_residual = el.residual;
        }
      }
    }
  }
  return found;
}
}  // namespace sic::search
