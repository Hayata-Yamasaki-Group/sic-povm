// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "sic/error.hpp"
#include "sic/sicx/format.hpp"

namespace sic::sicx {
std::string ray_label(std::size_t index);

template <typename Digit>
void validate_ray_identity(std::size_t index, std::string_view label,
                           const std::vector<Digit>& word, const std::vector<Digit>& smith,
                           const std::vector<Digit>* previous) {
  if (label != ray_label(index))
    throw sic::Error(sic::ErrorKind::invalid_format, "ray labels must be consecutive from c0");
  if (word.size() != smith.size())
    throw sic::Error(sic::ErrorKind::invalid_format, "ray word width differs from the Smith basis");
  for (std::size_t axis = 0; axis < word.size(); ++axis)
    if (word[axis] >= smith[axis])
      throw sic::Error(sic::ErrorKind::invalid_format, "ray word digit out of range");
  if (previous && !(*previous < word))
    throw sic::Error(sic::ErrorKind::invalid_format,
                     "ray words must be in increasing lexicographic order");
}

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
