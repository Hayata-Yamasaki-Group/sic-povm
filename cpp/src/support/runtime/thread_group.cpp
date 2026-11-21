// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/thread_group.hpp"

namespace sic::support {
void ThreadGroup::reserve(std::size_t count) { threads_.reserve(count); }
}  // namespace sic::support

namespace sic::support {
void ThreadGroup::join() {
  for (auto& thread : threads_)
    if (thread.joinable()) thread.join();
  threads_.clear();
}
}  // namespace sic::support

namespace sic::support {
ThreadGroup::~ThreadGroup() { join(); }
}  // namespace sic::support
