// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/sicx/io.hpp"
namespace sic::sicx {
std::vector<LeafSpan> leaf_spans(std::span<const uint8_t> payload,
                                 const ResourceLimits& limits = {});
std::vector<LogicalEntry> chunk_leaves(const IndexEntry& entry, std::span<const uint8_t> bytes,
                                       const ResourceLimits& limits = {},
                                       const WorkspaceAdmission& admit = {});

}  // namespace sic::sicx
