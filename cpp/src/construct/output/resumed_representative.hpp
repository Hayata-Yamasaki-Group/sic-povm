// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <fstream>

#include "construct/output/action_elements.hpp"
#include "sicx/codec/integer.hpp"
#include "sicx/container/producer_writer.hpp"
namespace sic::construct {
struct ResumedRepresentative {
  std::ifstream file;
  const sic::sicx::Meta* meta = nullptr;
  size_t element = 0;
  std::vector<sic::sicx::IndexEntry> blocks;
  size_t next_block = 0, at = 0, last = 0;
  bool any = false;
  long prefix = 0, half = 0;
  std::vector<uint64_t> shifts;
  struct StoredLeaf {
    size_t index;
    std::unique_ptr<sic::sicx::FmpzPair> value;
  };
  std::vector<StoredLeaf> pairs;
  ResumedRepresentative(const std::filesystem::path& path, sic::sicx::ProducerWriter& output,
                        size_t element_index, long position, long stored_prefix, long logical_half,
                        const std::vector<uint64_t>& generator_shifts);
  bool load();
  void fill(size_t first, std::span<Leaf> out);
  void finish();
};
}  // namespace sic::construct
