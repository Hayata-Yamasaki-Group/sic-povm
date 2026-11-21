// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <sys/statvfs.h>

#include <cstdio>

#include "support/resources.hpp"

namespace sic::exact {

void require_leaf_disk(FILE* file, sic::support::resources::Bytes extra);

}  // namespace sic::exact
