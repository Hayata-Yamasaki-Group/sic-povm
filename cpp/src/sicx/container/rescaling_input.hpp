// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/sicx/io.hpp"
#include "sic/sicx/rescaling_rule.hpp"
namespace sic::sicx {
LeafSource unrescaled_source(const Reader& reader, size_t element);
using StoredBlocks = std::function<void(const std::function<void(std::span<const uint8_t>)>&)>;
LeafSource stored_element_source(const Meta& meta, size_t id, StoredBlocks blocks);
LeafSource stored_element_source(const Reader& reader, size_t id);
GeneratorRescaling rescaling_of_file(const Reader& reader, unsigned threads);
}  // namespace sic::sicx
