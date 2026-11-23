// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <filesystem>
#include <functional>
#include <iosfwd>

#include "sic/support/durable_file.hpp"

namespace sic::support {
// Link the checked descriptor on Linux; remove the private name only if it
// still names that inode. Other platforms retain the checked-path fallback.
void publish_pinned_no_replace(int fd, const std::filesystem::path& from,
                               const std::filesystem::path& to);

// Stream into the durable-file owner's private-file publication path with
// bounded buffering. The callback borrows the stream only for this call.
// Its exceptions propagate, with the unpublished temporary removed. Stream I/O
// failures throw Error(io); publication/durability semantics match atomic_write.
void atomic_write_stream(const std::filesystem::path& path,
                         const std::function<void(std::ostream&)>& write, bool replace = true,
                         mode_t mode = 0644, bool data_only = false);
}  // namespace sic::support
