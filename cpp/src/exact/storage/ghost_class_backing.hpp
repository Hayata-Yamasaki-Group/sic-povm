// SPDX-License-Identifier: AGPL-3.0-or-later
// Private class backing: exact full balls, or a successfully certified Newton
// root box and the metadata required to replay its original reconstruction.
#pragma once
#include "exact/analytic/table.hpp"
#include "exact/certification/reconstruction/ghost.hpp"
#include "exact/group/group.hpp"
#include "exact/storage/ball_file.hpp"
#include "sic/support/kernel_settings.hpp"

namespace sic::exact {

struct GhostClassBacking {
  GhostRootMetadata metadata;
  long group_element = -1;
  slong packing_precision = 0;
  std::shared_ptr<BallFile> state;

  sic::support::resources::Bytes workspace(int owned_workers = 1) const;

  static std::shared_ptr<GhostClassBacking> save(const CertifiedGhostRoot& root, long element,
                                                 slong precision);

  sic::support::ComplexBallVector load(const Group& group, const Table& table,
                                       sic::support::Index cls,
                                       const sic::support::resources::Budget& budget,
                                       int owned_workers = 1) const;
};

struct TableClassBacking {
  std::shared_ptr<BallFile> values;
  std::shared_ptr<GhostClassBacking> ghost;
  TableClassBacking() = default;
  TableClassBacking(std::shared_ptr<BallFile> full) : values(std::move(full)) {}
  explicit operator bool() const { return bool(values) || bool(ghost); }
  sic::support::resources::Bytes bytes() const {
    return ghost ? ghost->state->bytes() : (values ? values->bytes() : 0);
  }
  sic::support::resources::Bytes workspace(int owned_workers = 1) const {
    return ghost ? ghost->workspace(owned_workers) : 0;
  }
};

}  // namespace sic::exact
