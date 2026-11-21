// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::crt::sicx {

struct BallString {
  Z M, E, m, e;

  explicit BallString(const string& s);

  void ball(arb_t out) const;
};
}  // namespace sic_check::crt::sicx
