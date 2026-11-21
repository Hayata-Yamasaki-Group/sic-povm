// SPDX-License-Identifier: AGPL-3.0-or-later
// Read-only whole-caller census before Newton worker admission. These are
// retained allocations plus explicit engineering allowances, not an allocator
// theorem. Source-key-bearing Group/Table/layout headers remain unchanged.
#pragma once

#include <gmp.h>

#include <set>
#include <type_traits>

#include "construct/runtime/resources/table_memory.hpp"
#include "exact/certification/bounds/height_bounds.hpp"
#include "exact/group/group.hpp"
#include "exact/storage/analytic_generator.hpp"
#include "exact/storage/ghost_class_backing.hpp"
#include "exact/storage/leaf_store.hpp"

namespace sic::construct::resources {
using sic::exact::AnalyticGenerator;
using sic::exact::Arithmetic;
using sic::exact::BallFile;
using sic::exact::ClassRep;
using sic::exact::GElem;
using sic::exact::Group;
using sic::exact::Leaf;
using sic::exact::LeafSpool;
using sic::exact::OrbitTableLayout;
using sic::exact::QuadraticNumber;
using sic::exact::Rational;
using sic::exact::Table;
using sic::exact::TableClassBacking;
using sic::exact::phase_memory::BallStorage;
using sic::exact::phase_memory::CopySource;
using sic::exact::phase_memory::StorageContractError;
using sic::support::resources::Bytes;
namespace certified = sic::exact::certified;

struct ResidentStorage {
  Bytes payload = 0, allowance = 0;
  Bytes bytes() const { return sic::support::resources::add(payload, allowance); }
  void append(const ResidentStorage& other) {
    payload = sic::support::resources::add(payload, other.payload);
    allowance = sic::support::resources::add(allowance, other.allowance);
  }
};

// Call dynamic() on an object whose inline header is already owned by its
// parent, and allocation() only for the independently owned allocation.
// Shared ticket/provenance allocations are deduplicated within this census.
struct ResidentCensus {
  ResidentStorage storage;
  std::set<const void*> shared;

  void allocation(Bytes bytes);
  void heap(Bytes bytes) { allocation(bytes); }
  template <class T>
  static constexpr bool kPlainLabel = std::is_arithmetic_v<T> || std::is_enum_v<T>;
  template <class T>
  void dynamic(const T&);
  void dynamic(const std::string& text) {
    allocation(sic::support::resources::add(text.capacity(), 1));
  }
  // Explicit promise by the caller: inline objects contain no dynamic owner.
  // Use this for local structs made only of integer labels, never native balls.
  template <class T>
  void capacity(const std::vector<T>& values);
  template <class T>
  void dynamic(const std::vector<T>& values);
  void integer(const fmpz* value);
  void dynamic(const QuadraticNumber& value);
  void dynamic(const Leaf& value) {
    integer(value.x);
    integer(value.y);
  }
  void dynamic(const certified::HeightBound& bound) { integer(MAG_EXPREF(bound.get())); }
  void dynamic(const acb_struct& value);
  void dynamic(const arb_struct& value);
  void dynamic(const sic::support::ComplexBallVector& values) { dynamic(values.v); }
  void dynamic(const sic::support::RealBallVector& values) { dynamic(values.v); }
  template <class T>
  bool first(const std::shared_ptr<T>& pointer);
  void ball_file(const std::shared_ptr<BallFile>& file);
  void dynamic(const AnalyticGenerator& value);
  void dynamic(const LeafSpool::Ticket& value);
  void dynamic(const TableClassBacking& value);
};

ResidentStorage inspect_group(const Group& group);

ResidentStorage inspect_arithmetic(const Arithmetic& arithmetic);

// Count each distinct layout once; its canonical map is a reference to the
// already counted Group::orbit. A different canonical owner is unsupported.
ResidentStorage inspect_layouts(const Group& group,
                                std::initializer_list<const OrbitTableLayout*> layouts);

struct ResidentPlan {
  Bytes model = 0, inspected = 0, rss = 0, future = 0;
  Bytes total() const {
    return sic::support::resources::add(std::max({model, inspected, rss}), future);
  }
};

inline constexpr Bytes kNewtonClassControlLimit = 4 * sic::support::resources::kMib;

void require_class_controls(const std::vector<long>& subsystem, const std::string& reason,
                            std::initializer_list<const TableClassBacking*> backings);

ResidentPlan resident_plan(Bytes model, Bytes fixed_inspected, Bytes caller_inspected,
                           Bytes caller_floor, Bytes rss, Bytes future);

Bytes newton_future_controls(sic::support::resources::Count dimension,
                             sic::support::resources::Count classes,
                             sic::support::resources::Count states);

}  // namespace sic::construct::resources

#include "construct/runtime/resources/resident_memory.tpp"
