// SPDX-License-Identifier: AGPL-3.0-or-later
#include "input_model.hpp"

namespace sic_check::crt::sicx {
Z::Z() { fmpz_init(v); }

Z::Z(Int64 n) { fmpz_init_set_si(v, n); }

Z::Z(const Z& z) { fmpz_init_set(v, z.v); }

Z::Z(Z&& z) noexcept {
  fmpz_init(v);
  fmpz_swap(v, z.v);
}

Z& Z::operator=(const Z& z) {
  fmpz_set(v, z.v);
  return *this;
}

Z& Z::operator=(Z&& z) noexcept {
  fmpz_swap(v, z.v);
  return *this;
}

Z::~Z() { fmpz_clear(v); }

A::A() { arb_init(v); }

A::A(const A& x) {
  arb_init(v);
  arb_set(v, x.v);
}

A& A::operator=(const A& x) {
  arb_set(v, x.v);
  return *this;
}

A::~A() { arb_clear(v); }

AlgebraicNumber::AlgebraicNumber() { qqbar_init(v); }

AlgebraicNumber::AlgebraicNumber(Int64 n) {
  qqbar_init(v);
  qqbar_set_si(v, n);
}

AlgebraicNumber::AlgebraicNumber(const AlgebraicNumber& q) {
  qqbar_init(v);
  qqbar_set(v, q.v);
}

AlgebraicNumber& AlgebraicNumber::operator=(const AlgebraicNumber& q) {
  qqbar_set(v, q.v);
  return *this;
}

AlgebraicNumber::~AlgebraicNumber() { qqbar_clear(v); }

B::B() { acb_init(v); }

B::B(const B& b) {
  acb_init(v);
  acb_set(v, b.v);
}

B& B::operator=(const B& b) {
  acb_set(v, b.v);
  return *this;
}

B::~B() { acb_clear(v); }

auto Scope::describe() const -> string {
  return string(full ? "FULL verification" : "PARTIAL verification") + "; exact root " +
         (exact_trusted ? "trusted (" + exact_route + ")" : "self-consistent, untrusted") +
         "; file root " +
         (file_trusted ? "trusted (" + file_route + ")" : "self-consistent, untrusted") + "; " +
         std::to_string(chunks.size()) + " payloads checked; " + std::to_string(elements.size()) +
         " complete elements checked" +
         (exact_checked ? "; canonical branches and rescaling checked"
                        : "; global rescaling and canonical branches unchecked") +
         (full ? "" : "; unread ordinary headers/payloads and dependent semantics unchecked") +
         "; field/group/actions and representative kernel fixity are algebraic premises; "
         "BALL/exact spot check not performed; BALL construction/containment and binary64 "
         "agreement are finalizer obligations";
}
}  // namespace sic_check::crt::sicx

namespace sic_check::crt::sicx {
A::operator arb_struct*() { return v; }
A::operator const arb_struct*() const { return v; }
arb_struct* A::operator->() { return v; }
const arb_struct* A::operator->() const { return v; }
}  // namespace sic_check::crt::sicx

namespace sic_check::crt::sicx {
Z::operator fmpz*() { return v; }
Z::operator const fmpz*() const { return v; }
fmpz* Z::operator->() { return v; }
const fmpz* Z::operator->() const { return v; }
}  // namespace sic_check::crt::sicx

namespace sic_check::crt::sicx {
B::operator acb_struct*() { return v; }
B::operator const acb_struct*() const { return v; }
acb_struct* B::operator->() { return v; }
const acb_struct* B::operator->() const { return v; }
}  // namespace sic_check::crt::sicx

namespace sic_check::crt::sicx {
Magnitude::Magnitude() { mag_init(v); }
Magnitude::~Magnitude() { mag_clear(v); }
}  // namespace sic_check::crt::sicx

namespace sic_check::crt::sicx {
Magnitude::operator mag_struct*() { return v; }
Magnitude::operator const mag_struct*() const { return v; }
mag_struct* Magnitude::operator->() { return v; }
const mag_struct* Magnitude::operator->() const { return v; }
}  // namespace sic_check::crt::sicx
