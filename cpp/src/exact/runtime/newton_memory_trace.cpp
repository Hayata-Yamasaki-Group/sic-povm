// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/runtime/newton_memory_trace.hpp"

namespace sic::exact::phase_memory {
void Trace::source(const std::vector<acb_struct>& values) {
  const auto storage = inspect_storage(values);
  if (!authoritative_) {
    ceilings_.source_bits = std::max<BitCount>(64, storage.largest_mantissa);
    ceilings_.selection_bits = std::max(ceilings_.selection_bits, ceilings_.source_bits);
    ceilings_.source_storage_bytes = storage.bytes();
  }
  emit("caller seed", storage,
       authoritative_
           ? forecast(ceilings()).source_grid
           : sic::support::resources::mul(
                 values.size(), sic::support::resources::complex_bytes(
                                    sic::support::resources::add(ceilings_.source_bits, 128))));
}

void Trace::selection(sic::support::resources::Count rows, sic::support::resources::BitCount bits,
                      bool modular) {
  if (!authoritative_) {
    ceilings_.candidate_rows = std::max(ceilings_.candidate_rows, rows);
    ceilings_.selection_bits = std::max(ceilings_.selection_bits, bits);
    ceilings_.modular = modular;
  }
  enter(Phase::selection);
}

void Trace::inverse(sic::support::resources::BitCount bits) {
  if (!authoritative_) {
    ceilings_.inverse_bits = std::max(ceilings_.inverse_bits, bits);
    ceilings_.stored_matrix_bits = std::max(ceilings_.stored_matrix_bits, bits);
  }
  enter(Phase::inverse);
}

void Trace::high(sic::support::resources::BitCount bits) {
  if (!authoritative_) ceilings_.high_bits = std::max(ceilings_.high_bits, bits);
}

void Trace::matrix(const char* name, const acb_mat_struct* values,
                   sic::support::resources::BitCount bits, bool native_margin) {
  emit(name, inspect_storage(values),
       matrix_bytes(acb_mat_nrows(values), acb_mat_ncols(values),
                    sic::support::resources::complex_bytes(
                        native_margin ? sic::support::resources::newton_matrix_bits(bits)
                                      : sic::support::resources::add(bits, 128))));
}

void Trace::vector(const char* name, const std::vector<acb_struct>& values,
                   sic::support::resources::BitCount bits) {
  emit(name, inspect_storage(values),
       sic::support::resources::mul(
           values.capacity(),
           sic::support::resources::complex_bytes(sic::support::resources::add(bits, 128))));
}

void Trace::cache(const char* name, const BallStorage& storage,
                  sic::support::resources::BitCount bits) {
  emit(name, storage,
       sic::support::resources::mul(storage.values, sic::support::resources::complex_bytes(
                                                        sic::support::resources::add(bits, 128))));
}

void Trace::publish(const char* name, const BallStorage& storage, Bytes predicted, bool observed) {
  Event event;
  event.phase = phase_;
  event.ceilings = ceilings();
  event.object = name;
  event.storage = storage;
  event.object_forecast = predicted;
  event.callback_slot = callback_slot_;
  event.observed_storage = observed;
  if (!event.storage_fits()) observed_contract_ok_ = false;
  event.observed_contract_ok = observed_contract_ok_;
  try {
    event.accounting = forecast(ceilings());
    event.forecast_valid = true;
  } catch (const std::overflow_error&) {
  } catch (const std::invalid_argument&) {
  }
  if (observer_) observer_(event);
}

}  // namespace sic::exact::phase_memory
