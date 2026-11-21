// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <filesystem>

#include "sic/sicx/io.hpp"

void producer_failures(const sic::sicx::Reader& good, const std::filesystem::path& work);
