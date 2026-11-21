// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::construct {
template <class Forecast>
int MemoryPlanner::phase_workers(int requested, Forecast&& forecast) const {
  if (requested < 1) throw std::invalid_argument("invalid phase worker count");
  while (requested > 1 && forecast(requested) > budget_.limit())
    requested = std::max(1, requested / 2);
  return requested;  // The phase admission still refuses if even one worker cannot fit.
}

template <class Reader>
std::optional<MemoryPlanner::IoPressure> MemoryPlanner::load_io_pressure(
    const std::vector<std::filesystem::path>& paths, Reader&& reader) {
  for (const auto& path : paths)
    if (const auto text = reader(path))
      if (const auto pressure = parse_io_pressure(*text, path.string())) return pressure;
  return {};
}

}  // namespace sic::construct
