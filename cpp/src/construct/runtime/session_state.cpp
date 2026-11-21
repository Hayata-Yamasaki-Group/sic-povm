// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/model/session_state.hpp"

#include "construct/output/quarantine.hpp"
#include "construct/runtime/observations/clock.hpp"
#include "construct/runtime/resources/construction_model.hpp"
#include "construct/runtime/resources/resident_memory.hpp"
namespace sic::construct {
Construction::~Construction() = default;
}
namespace sic::construct {

Construction::Construction(const ProducerOptions& options, Components chosen,
                           const RunServices& runtime)
    : producer_options(options), components(std::move(chosen)) {
  component_context.stop = runtime.stop;
  component_context.progress = runtime.progress;
  components.require_native_supported();
}

}  // namespace sic::construct
