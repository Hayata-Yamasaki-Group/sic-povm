// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact {
template <class Function>
void Table::for_each_value(const Function& function) {
  if (layout) {
    for (sic::support::Index c = 0; c < ar.h(); ++c)
      for (auto& value : packed[c].v) function(c, &value);
  } else {
    for (sic::support::Index c = 0; c < ar.h(); ++c)
      for (auto& value : val[c]) function(c, &value);
  }
}

}  // namespace sic::exact
