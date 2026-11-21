// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/stages/table/newton_admission.hpp"

#include "construct/model/session_state.hpp"
#include "construct/runtime/resources/construction_model.hpp"
#include "construct/runtime/resources/resident_memory.hpp"
namespace sic::construct {
NewtonResidentPlan Construction::newton_resident_plan(
    slong precision, sic::support::resources::Bytes retained_labels, bool stream) {
  const auto current = sic::support::resources::add(
      (*model_).group_resident(),
      sic::support::resources::add((*model_).table(T->prec), retained_labels));
  sic::construct::resources::ResidentCensus caller;
  caller.dynamic(inSupp);
  caller.dynamic(inA);
  caller.dynamic(sig_odd);
  caller.dynamic((*product_).factors);
  caller.dynamic((*product_).certified_height);

  caller.dynamic(derived);
  caller.dynamic(completed_elements);
  caller.dynamic(pending_table_backing);
  caller.dynamic(completed_inventory.backing);
  caller.allocation(
      sic::support::resources::mul(relative_certificates.capacity(), sizeof(RelativeCertificate)));
  for (const auto& certificate : relative_certificates) {
    caller.dynamic(certificate.kernel_generators);
    caller.dynamic(certificate.target_generators);
    caller.dynamic(certificate.real_ball);
    caller.dynamic(certificate.imaginary_ball);
  }
  if (inspect_newton_caller) inspect_newton_caller(caller);
  const auto group_storage = sic::construct::resources::inspect_group((*group_));
  const auto arithmetic_storage = sic::construct::resources::inspect_arithmetic((*ar_));
  const auto layout_storage = sic::construct::resources::inspect_layouts(
      (*group_), {analytic_layout.get(), T->layout.get()});
  const auto table_storage = sic::construct::resources::inspect_retained_table(*T);
  const auto fixed_storage = sic::support::resources::add(
      64 * sic::support::resources::kMib,
      sic::support::resources::add(
          group_storage.bytes(),
          sic::support::resources::add(
              arithmetic_storage.bytes(),
              sic::support::resources::add(layout_storage.bytes(), table_storage.owned))));
  const auto future_controls =
      sic::construct::resources::newton_future_controls(d, (*ar_).h(), T->nstates());
  const auto future = sic::support::resources::add(
      future_controls,
      stream ? 0 : sic::support::resources::mul(2, (*model_).table(precision + 16)));
  const auto resident_plan = sic::construct::resources::resident_plan(
      current, fixed_storage, caller.storage.bytes(), retained_labels,
      sic::support::resources::inspect().rss, future);
  const auto resident = resident_plan.total();
  sic::support::progressf(
      "Newton resident admission: model=%llu inspected=%llu rss=%llu future=%llu total=%llu "
      "group=%llu arithmetic=%llu layouts=%llu coarse=%llu caller=%llu caller_floor=%llu "
      "future_controls=%llu; capacities include allowances, shared canonical counted once\n",
      (unsigned long long)resident_plan.model, (unsigned long long)resident_plan.inspected,
      (unsigned long long)resident_plan.rss, (unsigned long long)resident_plan.future,
      (unsigned long long)resident, (unsigned long long)group_storage.bytes(),
      (unsigned long long)arithmetic_storage.bytes(), (unsigned long long)layout_storage.bytes(),
      (unsigned long long)table_storage.owned, (unsigned long long)caller.storage.bytes(),
      (unsigned long long)retained_labels, (unsigned long long)future_controls);
  return {resident, future_controls, table_storage};
}
}  // namespace sic::construct
