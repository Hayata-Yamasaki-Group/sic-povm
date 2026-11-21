// SPDX-License-Identifier: AGPL-3.0-or-later
// Fixture-owned fault controls, with no dependency from checker modules to fixtures.
#pragma once
namespace sic_check::verifier::test {
extern int stat_fault, seek_fault, tell_fault;
extern int root_fault, finite_fault;
extern bool pad_real_roots;
extern int padded_roots;
}  // namespace sic_check::verifier::test
