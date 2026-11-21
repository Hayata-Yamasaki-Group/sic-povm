// SPDX-License-Identifier: AGPL-3.0-or-later
// Canonical signed binary integer primitives for private ball files.
#pragma once

#include <sys/statvfs.h>

#include <array>
#include <limits>

#include "leaf_store.hpp"
#include "support/resources.hpp"

namespace sic::exact::leaf_codec {

using Count = std::uint64_t;

inline void require_disk(FILE* file, Count extra) { require_leaf_disk(file, extra); }

size_t varint_size(Count value);

void write_unsigned(FILE* file, Count value);

Count read_unsigned(FILE* file);

Count integer_code(const fmpz_t value);

void write_integer(FILE* file, const fmpz_t value);

void read_integer(FILE* file, fmpz_t value, Count& payload_budget);

}  // namespace sic::exact::leaf_codec
