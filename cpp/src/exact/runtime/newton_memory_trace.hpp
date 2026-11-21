// SPDX-License-Identifier: AGPL-3.0-or-later
// Observations of real refiner phases, optionally bound to admission state.
#pragma once

#include <functional>

#include "exact/runtime/memory/ball_storage.hpp"
#include "exact/runtime/memory/phase_accounting.hpp"

namespace sic::exact::phase_memory {

enum class Refusal { none, class_slot, helper, storage_contract };

struct Event {
  Phase phase = Phase::roots;
  Ceilings ceilings;
  Accounting accounting;
  const char* object = "phase";
  BallStorage storage;
  Bytes object_forecast = 0, callback_slot = 0;
  // forecast_valid means formula evaluation only, not a storage certificate.
  bool forecast_valid = false, observed_storage = false;
  bool observed_contract_ok = true;

  bool storage_fits() const {
    return !observed_storage || (storage.finite_lagom && storage.bytes() <= object_forecast);
  }
};

using Observer = std::function<void(const Event&)>;

// The observer is trusted and may throw, just like the existing after_b0
// calibration seam. Captures are separately owned/accounted. This tracer
// does not admit allocations or turn a failed storage observation into a
// certificate. Proposals remain engineering forecasts until all ownership
// and growth gates are integrated with process-wide grants.
class Trace {
 public:
  Trace(Ceilings proposal, Bytes callback_slot, Observer observer,
        const Ceilings* authoritative = nullptr)
      : ceilings_(proposal),
        callback_slot_(callback_slot),
        observer_(std::move(observer)),
        authoritative_(authoritative) {}

  void source(const std::vector<acb_struct>& values);
  void enter(Phase phase) {
    phase_ = phase;
    emit();
  }
  void callback_slot(Bytes bytes) { callback_slot_ = bytes; }
  void selection(sic::support::resources::Count rows, sic::support::resources::BitCount bits,
                 bool modular);
  void inverse(sic::support::resources::BitCount bits);
  void high(sic::support::resources::BitCount bits);
  void matrix(const char* name, const acb_mat_struct* values,
              sic::support::resources::BitCount bits, bool native_margin = true);
  void vector(const char* name, const std::vector<acb_struct>& values,
              sic::support::resources::BitCount bits);
  void cache(const char* name, const BallStorage& storage, sic::support::resources::BitCount bits);
  const Ceilings& ceilings() const { return authoritative_ ? *authoritative_ : ceilings_; }

 private:
  Ceilings ceilings_;
  Bytes callback_slot_;
  Observer observer_;
  const Ceilings* authoritative_;
  Phase phase_ = Phase::roots;
  bool observed_contract_ok_ = true;

  void emit() { publish("phase", {}, 0, false); }
  void emit(const char* name, const BallStorage& storage, Bytes forecast) {
    publish(name, storage, forecast, true);
  }
  void publish(const char* name, const BallStorage& storage, Bytes predicted, bool observed);
};

}  // namespace sic::exact::phase_memory
