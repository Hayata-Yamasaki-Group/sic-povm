// SPDX-License-Identifier: AGPL-3.0-or-later
// Exact group-stage checkpoint. This contains no analytic midpoints, tower,
// working precision or physical branch. Source/parameter identity and SHA-256
// bind it to a completed validated producer; loading is not a new group proof.
#pragma once

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

#include "group.hpp"
#include "sic/error.hpp"
#include "sic/support/durable_file.hpp"
#include "support/sha256.hpp"

namespace sic::exact {

struct GroupProjection {
  int exponent = 0;
  long discriminant = 0, original_discriminant = 0, order_index = 0;
  long classes = 0, points = 0, elements = 0;
  std::vector<long> smith, stride;
  std::vector<sic::support::Index> axis_classes;
  std::string source, digest;
  std::uint64_t bytes = 0;
};

inline std::string checkpoint_digest(const std::filesystem::path& path) {
  return sic::support::sha256::file_hex(path);
}

struct GroupCheckpoint {
  static std::string source_id();
#ifdef SIC_ENABLE_TEST_HOOKS
  static inline std::function<void()> rename_fault;
  static inline std::function<sic::support::resources::Bytes()> disk_available;
#endif

  template <class T>
  static void write(FILE* file, const T* values, size_t count = 1);
  template <class T>
  static void read(FILE* file, T* values, size_t count = 1);
  static sic::support::resources::Bytes bytes(long npts, long elements, size_t bases, size_t rank);

  static void save(const Group& group, const std::filesystem::path& path);

  static void load(Group& group, const std::filesystem::path& path, long expected_elements,
                   const std::function<void(size_t)>& admit);

  // Interpret only the principal base and Smith rows, from the bytes covered by
  // the complete file digest. A caller-provided pin takes precedence over the sidecar.
  static GroupProjection project(const std::filesystem::path& path, std::string digest,
                                 bool read_ahead);
};

}  // namespace sic::exact

#include "exact/group/group_checkpoint.tpp"
