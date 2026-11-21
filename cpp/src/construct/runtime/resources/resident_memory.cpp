// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/runtime/resources/resident_memory.hpp"

namespace sic::construct::resources {
void ResidentCensus::allocation(Bytes bytes) {
  storage.payload = sic::support::resources::add(storage.payload, bytes);
  if (bytes) storage.allowance = sic::support::resources::add(storage.allowance, 64);
}

void ResidentCensus::integer(const fmpz* value) {
  if (!COEFF_IS_MPZ(*value)) return;
  const auto native = COEFF_TO_PTR(*value);
  if (native->_mp_alloc < 0) throw std::overflow_error("negative native integer capacity");
  heap(sic::support::resources::add(
      sizeof(*native), sic::support::resources::mul(native->_mp_alloc, sizeof(mp_limb_t))));
}

void ResidentCensus::dynamic(const QuadraticNumber& value) {
  for (const Rational* part : {&value.a, &value.b}) {
    integer(fmpq_numref(part->v));
    integer(fmpq_denref(part->v));
  }
}

void ResidentCensus::dynamic(const acb_struct& value) {
  const auto bytes = acb_allocated_bytes(&value);
  if (bytes < 0) throw std::overflow_error("negative complex ball allocation");
  heap(static_cast<Bytes>(bytes));
}

void ResidentCensus::dynamic(const arb_struct& value) {
  const auto bytes = arb_allocated_bytes(&value);
  if (bytes < 0) throw std::overflow_error("negative real ball allocation");
  heap(static_cast<Bytes>(bytes));
}

void ResidentCensus::ball_file(const std::shared_ptr<BallFile>& file) {
  if (first(file)) allocation(64 * 1024);
}

void ResidentCensus::dynamic(const AnalyticGenerator& value) {
  dynamic(value.full);
  dynamic(value.scalar);
  dynamic(value.periodic);
  dynamic(value.root);
  allocation(sic::support::resources::mul(value.pieces.capacity(), sizeof(value.pieces[0])));
  // BallFile internals and callable captures are not exposed. Charge a
  // named bounded control allowance, never the on-disk ball payload size.
  ball_file(value.backing);
  for (const auto& file : value.pieces) ball_file(file);
  storage.allowance = sic::support::resources::add(storage.allowance, 4096);
}

void ResidentCensus::dynamic(const LeafSpool::Ticket& value) {
  if (first(value.index)) dynamic(*value.index);
  if (first(value.parts)) dynamic(*value.parts);
}

void ResidentCensus::dynamic(const TableClassBacking& value) {
  ball_file(value.values);
  if (first(value.ghost)) ball_file(value.ghost->state);
}

ResidentStorage inspect_group(const Group& group) {
  ResidentCensus census;
  census.allocation(sizeof(Group));
  census.allocation(sic::support::resources::mul(group.gens.capacity(), sizeof(GElem)));
  for (const auto& generator : group.gens) census.dynamic(generator.perm);
  census.dynamic(group.orbit);
  census.dynamic(group.invf);
  census.dynamic(group.stride);
  census.dynamic(group.base);
  census.dynamic(group.img);
  census.dynamic(group.nn);
  census.dynamic(group.chi);
  census.dynamic(group.rep_elem);
  census.dynamic(group.rep_base);
  return census.storage;
}

ResidentStorage inspect_arithmetic(const Arithmetic& arithmetic) {
  ResidentCensus census;
  census.allocation(sizeof(Arithmetic));
  census.dynamic(arithmetic.eps);
  census.allocation(sic::support::resources::mul(arithmetic.cls.capacity(), sizeof(ClassRep)));
  for (const auto& cls : arithmetic.cls) {
    census.capacity(cls.cycle);
    census.dynamic(cls.tau);
    census.dynamic(cls.digit);
    census.dynamic(cls.scal);
  }
  return census.storage;
}

ResidentStorage inspect_layouts(const Group& group,
                                std::initializer_list<const OrbitTableLayout*> layouts) {
  ResidentCensus census;
  for (const auto* layout : layouts)
    if (layout && census.shared.insert(layout).second) {
      if (&layout->canonical != &group.orbit)
        throw StorageContractError("resident layout canonical map is not owned by Group");
      census.allocation(sizeof(OrbitTableLayout) + 64);
      census.dynamic(layout->index);
      census.dynamic(layout->derived);
      census.dynamic(layout->conjugate_index);
    }
  return census.storage;
}

void require_class_controls(const std::vector<long>& subsystem, const std::string& reason,
                            std::initializer_list<const TableClassBacking*> backings) {
  ResidentCensus census;
  census.dynamic(subsystem);
  census.dynamic(reason);
  for (const auto* backing : backings)
    if (backing) census.dynamic(*backing);
  if (census.storage.bytes() > kNewtonClassControlLimit)
    throw sic::support::resources::AdmissionError(
        "retained Newton class controls exceed their resident reservation");
}

ResidentPlan resident_plan(Bytes model, Bytes fixed_inspected, Bytes caller_inspected,
                           Bytes caller_floor, Bytes rss, Bytes future) {
  // The caller floor remains additive to fixed inspected ownership: a large
  // Group must not hide a missing label obligation in a max of whole totals.
  return {model,
          sic::support::resources::add(fixed_inspected, std::max(caller_inspected, caller_floor)),
          rss, future};
}

Bytes newton_future_controls(sic::support::resources::Count dimension,
                             sic::support::resources::Count classes,
                             sic::support::resources::Count states) {
  using sic::support::resources::add;
  using sic::support::resources::mul;
  const auto variables = mul(2, dimension) - 2;
  // Tn/seed Table metadata, all per-class vectors (including optional retained
  // ceilings, prepared owners and retry work lists), saved subsystem hints,
  // bounded why/manifest/provenance strings and stdio/callable/thread controls.
  // State/grid payloads, packed output, roots, publication and replay are
  // separately charged by their actual prepared/callback grants.
  const auto table_metadata = add(
      sizeof(Table),
      add(mul(add(classes, mul(2, states)), 2 * sizeof(sic::support::Index)),
          mul(classes, sizeof(sic::support::ComplexBallVector) + sizeof(std::vector<acb_struct>))));
  const auto per_class =
      add(kNewtonClassControlLimit, add(table_metadata, mul(2 * sizeof(long), variables)));
  return add(64 * sic::support::resources::kMib, add(table_metadata, mul(classes, per_class)));
}

}  // namespace sic::construct::resources
