// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/runtime/newton_class_memory.hpp"
#include "exact/runtime/newton_memory_admission.hpp"
#pragma once
#include "exact/analytic/table.hpp"
#include "exact/certification/reconstruction/ghost.hpp"
namespace sic::construct {
// Borrows the current scheduler retry state for one synchronous class attempt.
struct ClassRefinement {
  const bool& phase_admission;
  const bool& memory_audit;
  const bool& root_backing;
  const slong& target;
  const sic::support::resources::BitCount& matrix_hint;
  int& effective_correction_passes;
  const std::vector<int>& class_counts;
  const sic::exact::phase_memory::Publication& publication;
  std::vector<std::optional<sic::exact::phase_memory::Ceilings>>& retained_memory;
  std::vector<std::vector<long>>& Shint;
  std::vector<double>& cond;
  std::vector<double>& ratio;
  std::vector<sic::exact::GhostRefiner::SelectionKind>& selection_kinds;
  std::vector<slong>& inverse_hints;
  std::vector<char>& ok;
  std::vector<char>& seedfail;
  std::vector<char>& used_quadrature;
  std::vector<std::string>& why;
  std::vector<sic::exact::phase_memory::ClassOutcome>& outcomes;
  std::vector<sic::support::resources::Bytes>& private_requests;
  std::unique_ptr<sic::exact::Table>& Tn;
  std::vector<std::unique_ptr<sic::exact::CertifiedGhostRoot>>& completed_roots;
  std::vector<long>& sig;
  slong& P;
#ifdef SIC_ENABLE_TEST_HOOKS
  const sic::support::resources::Count& defer_class;
  std::vector<char>& injected_defer;
  std::vector<char>& injected_entry;
  const bool& defer_entry;
#endif
};
}  // namespace sic::construct
