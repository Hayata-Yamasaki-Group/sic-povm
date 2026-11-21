// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::construct {
template <class F>
void Construction::for_each_tower_element(const std::vector<std::unique_ptr<ActionElement>>& el,
                                          F&& f) {
  for (int i = 0; i < t; ++i) {
    for (size_t q = 0; q < items.size(); ++q) {
      if (items[q].i != i) continue;
      sic::sicx::ElementId id{items[q].k < 0 ? "wsq" : "fcoef", (unsigned)i, -1,
                              items[q].k < 0 ? -1 : items[q].k, -1};
      f(id, tower_source_of(ilv[q], item_sources[q]));
    }
    for (auto& ep : el) {
      if (ep->i != i) continue;
      sic::sicx::ElementId id{ep->k == -2   ? "actdelta"
                              : ep->k == -1 ? "act"
                                            : "actodd",
                              (unsigned)i, ep->k == -2 ? -1 : (int64_t)ep->l,
                              ep->k >= 0 ? ep->k : -1, -1};
      f(id, tower_source_of(ep->lv, ep->source));
    }
  }
}
}  // namespace sic::construct
