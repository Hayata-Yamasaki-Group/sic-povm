// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/action_elements.hpp"
#include "sicx/codec/rescale.hpp"
#pragma once
#include <filesystem>

#include "support/resources.hpp"
namespace sic::construct {
sic::support::resources::Bytes sicx_coordinate_bytes(sic::support::resources::BitCount bits);
sic::support::resources::Bytes sicx_element_bound(sic::support::resources::Count coordinates,
                                                  sic::support::resources::BitCount bits,
                                                  sic::support::resources::Count block);
sic::support::resources::Bytes sicx_free(const std::filesystem::path& p);
void sicx_admit(const std::filesystem::path& sicx_dir, const char* stage,
                sic::support::resources::Bytes required);
}  // namespace sic::construct

#include "exact/storage/leaf_source.hpp"
#include "sic/sicx/io.hpp"
namespace sic::construct {
sic::sicx::LeafSource tower_source_of(std::vector<sic::exact::Leaf>& leaves,
                                      sic::exact::storage::Source& source);
}
