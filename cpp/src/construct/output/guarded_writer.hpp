// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "construct/components/components.hpp"
#include "construct/output/quarantine.hpp"
#include "sic/error.hpp"
namespace sic::construct {
std::unique_ptr<sic::sicx::ProducerWriter> open_guarded_writer(
    SicxWriter& component, QuarantineOutput& quarantine, const sic::sicx::Meta& header,
    const sic::sicx::StreamOptions& options, unsigned threads, const std::string& record,
    const sic::construct::ProductionPolicy& policy = {});
}  // namespace sic::construct
