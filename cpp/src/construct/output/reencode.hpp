// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/rescaling_rule.hpp"
#include "sicx/container/rescaling_input.hpp"
#pragma once
#include "construct/components/branch_admission.hpp"
#include "construct/components/components.hpp"
#include "construct/output/quarantine.hpp"
#include "sicx/codec/unrescaled.hpp"
namespace sic::construct {
void reencode(const sic::sicx::Reader& reader, const std::filesystem::path& output,
              unsigned threads, const sic::sicx::VerificationPolicy& policy = {});
}  // namespace sic::construct
