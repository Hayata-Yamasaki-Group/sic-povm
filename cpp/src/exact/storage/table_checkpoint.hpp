// SPDX-License-Identifier: AGPL-3.0-or-later
// Completed analytic enclosures, bound to arithmetic/group/table sources and
// precision. Loading preserves producer certificates; it is not a new proof.
#pragma once

#include <fstream>

#include "ball_file.hpp"
#include "exact/analytic/table.hpp"

namespace sic::exact {

struct TableCheckpoint {
  static std::string source_id();
  static std::filesystem::path directory(const std::filesystem::path& root, const Arithmetic& ar,
                                         slong precision);
  static void sync_file(const std::filesystem::path& path);
  static void save(const Table& table, const Group& group, long cc,
                   const std::vector<char>& derived, const std::filesystem::path& path);

  static std::unique_ptr<Table> load(const Arithmetic& ar, const Group& group,
                                     const std::shared_ptr<OrbitTableLayout>& layout,
                                     slong precision, int threads,
                                     const std::filesystem::path& path, long& cc,
                                     std::vector<char>& derived,
                                     const std::function<void(size_t)>& admit);
};

}  // namespace sic::exact
