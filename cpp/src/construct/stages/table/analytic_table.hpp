// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "exact/analytic/table.hpp"
#include "sic/error.hpp"
#include "support/index.hpp"
namespace sic::construct {
struct TablePrecisionError : sic::Error {
  TablePrecisionError(sic::support::Index cls, size_t slot, slong precision);
};
}  // namespace sic::construct
