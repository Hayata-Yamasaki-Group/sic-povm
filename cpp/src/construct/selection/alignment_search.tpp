// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::construct::alignment::detail {
template <class F>
void par(long n, int threads, F&& body) {
  sic::exact::parallel_for(n, threads, std::forward<F>(body));
}

}  // namespace sic::construct::alignment::detail
