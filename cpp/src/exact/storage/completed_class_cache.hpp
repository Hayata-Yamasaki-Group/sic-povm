// SPDX-License-Identifier: AGPL-3.0-or-later
// Trusted completed-producer cache, not an independently replayable SIC proof.
#pragma once

#include <dlfcn.h>
#include <flint/flint.h>
#include <gmp.h>
#include <mpfr.h>

#include <iomanip>
#include <mutex>
#include <sstream>

#include "exact/storage/ghost_class_backing.hpp"
#include "table_checkpoint.hpp"
#if defined(__linux__)
#include <sys/stat.h>
#include <sys/sysmacros.h>
#endif

namespace sic::exact {

std::string completed_loaded_object_digest(const void* address);

const std::string& completed_class_dependencies();

std::string cache_checksum_file(const std::filesystem::path& path);

void verify_cached_ball_file(const BallFile& file, size_t count);

struct CompletedClassIdentity {
  long s = 0, d = 0, delta = 0, order = 0, degree = 0, slots = 0, cc = -1;
  slong precision = 0;
  bool paired = false;
  std::string producer, group_digest, coarse_digest;
  // Native analytic checkpoint source identity.
  std::string coarse_source;
  std::vector<char> derived;
  std::vector<long> elements;

  std::string text() const;
};

class CompletedClassCache {
  CompletedClassIdentity identity_;
  std::string identity_text_;
  std::filesystem::path generation_;
  const Group& group_;
  std::mutex publication_;

  std::filesystem::path path(sic::support::Index cls) const {
    return generation_ / ("class-" + std::to_string(cls));
  }
  void validate(sic::support::Index cls, const TableClassBacking& backing,
                const std::string& method) const;

 public:
  struct Inventory {
    std::vector<TableClassBacking> backing;
    size_t hits = 0, missing = 0;
    sic::support::resources::Bytes replay_workspace = 0;
  };

  CompletedClassCache(std::filesystem::path root, CompletedClassIdentity identity,
                      const Group& group);

  TableClassBacking load(sic::support::Index cls) const;

 public:
  Inventory inventory(int replay_workers = 1) const;

  TableClassBacking publish(sic::support::Index cls, TableClassBacking&& source,
                            const std::string& method, const std::string& options);
};

sic::support::resources::Bytes completed_class_new_disk(
    size_t missing, sic::support::resources::Bytes class_bytes,
    sic::support::resources::Bytes generator_bytes, sic::support::resources::Bytes temporary_bytes);

}  // namespace sic::exact

namespace sic::exact {

struct CompletedCacheRequirement {
  bool enabled = false;

  static CompletedCacheRequirement parse(const char* value);

  void require_context(bool supported_mode, bool streamed_arb, const char* completed_root,
                       const char* group, const char* coarse_root) const;

  void enforce(long precision, std::size_t reused, std::size_t missing, std::size_t physical) const;
};

}  // namespace sic::exact
