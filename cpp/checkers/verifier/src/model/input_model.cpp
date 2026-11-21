// SPDX-License-Identifier: AGPL-3.0-or-later
#include "input_model.hpp"

namespace sic_check::verifier {
Real::Real() { arb_init(v); }

Real::~Real() { arb_clear(v); }

Integer::Integer() { fmpz_init(v); }

Integer::~Integer() { fmpz_clear(v); }

Integer::operator fmpz*() { return v; }

Integer::operator const fmpz*() const { return v; }

auto Jv::get(const string& k) const -> const Jv* {
  for (auto& kv : o)
    if (kv.first == k) return &kv.second;
  return nullptr;
}

Elem::Elem() { fmpz_init(scale); }

Elem::~Elem() { fmpz_clear(scale); }

ACB::ACB() { acb_init(v); }

ACB::ACB(const ACB& o) {
  acb_init(v);
  acb_set(v, o.v);
}

ACB::ACB(ACB&& o) noexcept {
  acb_init(v);
  acb_swap(v, o.v);
}

ACB& ACB::operator=(const ACB& o) {
  acb_set(v, o.v);
  return *this;
}

ACB& ACB::operator=(ACB&& o) noexcept {
  acb_swap(v, o.v);
  return *this;
}

ACB::~ACB() { acb_clear(v); }

ExactRoot::ExactRoot() { qqbar_init(v); }

ExactRoot::ExactRoot(const ExactRoot& x) {
  qqbar_init(v);
  qqbar_set(v, x.v);
}

ExactRoot& ExactRoot::operator=(const ExactRoot& x) {
  qqbar_set(v, x.v);
  return *this;
}

ExactRoot::~ExactRoot() { qqbar_clear(v); }
}  // namespace sic_check::verifier

namespace sic_check::verifier {
Real::operator arb_struct*() { return v; }
Real::operator const arb_struct*() const { return v; }
arb_struct* Real::operator->() { return v; }
const arb_struct* Real::operator->() const { return v; }
}  // namespace sic_check::verifier

namespace sic_check::verifier {
ACB::operator acb_struct*() { return v; }
ACB::operator const acb_struct*() const { return v; }
acb_struct* ACB::operator->() { return v; }
const acb_struct* ACB::operator->() const { return v; }
}  // namespace sic_check::verifier
