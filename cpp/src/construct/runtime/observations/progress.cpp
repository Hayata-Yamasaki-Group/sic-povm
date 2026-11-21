// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/runtime/observations/progress.hpp"

#include "sic/support/flint_raii.hpp"
#include "sic/support/kernel_settings.hpp"

namespace sic::construct {
void print_arb(const arb_t value, slong digits, ulong flags) {
  if (!sic::support::current_progress_sink || !sic::support::current_progress_sink->emit) {
    arb_printn(value, digits, flags);
    return;
  }
  sic::support::flint::String text(arb_get_str(value, digits, flags));
  const std::string owned(text.get());
  sic::support::progressf("%s", owned.c_str());
}

void print_acb(const acb_t value, slong digits, ulong flags) {
  if (!sic::support::current_progress_sink || !sic::support::current_progress_sink->emit) {
    acb_printn(value, digits, flags);
    return;
  }
  print_arb(acb_realref(value), digits, flags);
  sic::support::progressf(" + (");
  print_arb(acb_imagref(value), digits, flags);
  sic::support::progressf(")*I");
}

}  // namespace sic::construct
