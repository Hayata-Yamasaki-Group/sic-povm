// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/core/sic_frame.hpp"
namespace sic::core {
struct FrameDiagnostics {
  double worst;
  Complex checksum;
};
FrameDiagnostics frame_diagnostics(const SicFrame& frame);
}  // namespace sic::core
