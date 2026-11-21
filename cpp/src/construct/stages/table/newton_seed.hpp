// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "exact/runtime/newton_class_memory.hpp"
#include "exact/runtime/newton_memory_admission.hpp"
namespace sic::construct {
struct NewtonSeedState {
  std::unique_ptr<sic::support::Reservations>& class_memory;
  const bool& memory_owners;
  const bool& phase_admission;
  std::vector<std::string>& why;
  std::vector<char>& seedfail;
  std::vector<char>& seed_refused;
  std::vector<std::optional<sic::exact::phase_memory::Ceilings>>& retained_memory;
  const slong& target;
  const sic::support::resources::BitCount& matrix_hint;
  const std::vector<sic::exact::phase_memory::Options>& options;
};
}  // namespace sic::construct
