// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/sicx/exact.hpp"
namespace sic::sicx::exact {
sic::support::Json conjugation_word_json(std::span<const uint64_t> word);
}
