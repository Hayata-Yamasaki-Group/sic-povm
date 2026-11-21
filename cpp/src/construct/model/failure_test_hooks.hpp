// SPDX-License-Identifier: AGPL-3.0-or-later
// Owner-thread failure injection for construction catch-boundary controls.
#pragma once
#ifdef SIC_ENABLE_TEST_HOOKS
#include <cstdint>
#include <functional>
#include <vector>
namespace sic::construct::failure_test_hooks {
inline thread_local std::function<void()> pairing, coarse_table, direct_eta;
inline thread_local std::function<void()> write_attempt;
inline thread_local std::function<void(std::vector<uint64_t>&)> resume_shifts;
inline thread_local bool park_coarse = false;
}  // namespace sic::construct::failure_test_hooks
#endif
