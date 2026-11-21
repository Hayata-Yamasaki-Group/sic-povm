// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/sicx/format.hpp"
namespace sic::sicx {
std::vector<uint64_t> words_add(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b,
                                const std::vector<uint64_t>& smith);
void derive_blocks(ElementInfo& e, uint64_t block_coordinates);
bool decimal_positive(const std::string& text);
int kind_rank(const std::string& kind);
ElementKey element_key(const ElementInfo& e);
bool element_less(const ElementInfo& a, const ElementInfo& b);
sic::support::Json meta_to_json(const Meta& m);
sic::support::Json rays_to_json(const Meta& m);
Meta meta_from_json(const sic::support::Json& j);
void rays_from_json(const sic::support::Json& j, Meta& m);
void validate_branch_string(const std::string& text);
}  // namespace sic::sicx
