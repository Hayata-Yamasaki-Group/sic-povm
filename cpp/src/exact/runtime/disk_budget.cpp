// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/runtime/disk_budget.hpp"

#include "sic/error.hpp"

namespace sic::exact {
void require_leaf_disk(FILE* file, sic::support::resources::Bytes extra) {
  struct statvfs state {};
  if (fstatvfs(fileno(file), &state) != 0)
    throw sic::Error(sic::ErrorKind::io, "cannot inspect exact-leaf disk headroom");
  const auto available = sic::support::resources::mul(state.f_bavail, state.f_frsize);
  const auto required = sic::support::resources::add(extra, sic::support::resources::kGib);
  sic::support::admission::record("disk", "exact leaves with 1 GiB reserve",
                                  required <= available ? "admit" : "refuse",
                                  sic::support::resources::kGib, extra, available);
  if (available < required)
    throw sic::Error(sic::ErrorKind::resource,
                     "insufficient disk headroom for exact leaves (1 GiB reserve)");
}

}  // namespace sic::exact
