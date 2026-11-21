// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/group/maximal_order_orbits.hpp"

namespace sic::exact::reduced_order {
long configure_group_orbits(Group& G, const CycleUnit& unit) {
  const Arithmetic& ar = G.ar;
  contract(unit.k > 1 && G.gens.empty() && G.N == 0, "group before generators");
  const long grid = ar.d * ar.d;
  const std::vector<sic::support::Index> eps_orbit = G.orbit;
  G.orbit_ready = false;
  std::iota(G.orbit.begin(), G.orbit.end(), sic::support::Index{0});
  GElem e;
  e.perm.assign(G.npts, -1);
  for (sic::support::Index c = 0; c < ar.h(); ++c) G.fill_map(e, c, c, unit.u);
  std::vector<sic::support::Index> orbit(G.npts);
  long nonzero = 0, covered = 0;
  const long period = 3L * unit.k;
  for (long x = 0; x < G.npts; ++x) {
    long y = x, best = x, visits = 0;
    do {
      contract(e.perm[y] >= 0 && e.perm[y] < G.npts && e.perm[y] / grid == y / grid,
               "u maps each class grid to itself", sic::ErrorKind::construction);
      best = std::min(best, y);
      y = e.perm[y];
      contract(++visits <= period, "u orbit size at most 3k", sic::ErrorKind::construction);
    } while (y != x);
    contract(period % visits == 0, "u orbit size divides 3k", sic::ErrorKind::construction);
    contract(x % grid != 0 || visits == 1, "zero points are fixed", sic::ErrorKind::construction);
    orbit[x] = sic::support::checked_index(best);
    if (best == x) {
      covered += visits;
      if (x % grid != 0) ++nonzero;
    }
  }
  contract(covered == G.npts, "u orbits partition the points");
  for (long x = 0; x < G.npts; ++x)
    contract(orbit[eps_orbit[x]] == orbit[x], "epsilon orbit inside u orbit");
  G.orbit = orbit;
  G.orbit_ready = true;
  return nonzero;
}

}  // namespace sic::exact::reduced_order
