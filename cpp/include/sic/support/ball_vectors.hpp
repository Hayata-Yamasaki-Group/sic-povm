// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <flint/acb.h>

#include <span>
#include <vector>

namespace sic::support {

// Owns initialized FLINT balls. Copying allocates and deep-copies every ball:
// cost is linear in the count plus the total stored midpoint/radius payload.
// Move construction transfers storage in constant time. Move assignment also
// clears the destination's old balls, so its cost includes that old payload.
struct ComplexBallVector {
  // Public storage for inspection and FLINT value access, owned by this vector.
  // Do not copy, erase, resize or replace raw structs; use owner copy/move.
  std::vector<acb_struct> v;
  explicit ComplexBallVector(size_t n = 0);
  ComplexBallVector(const ComplexBallVector& other);
  ComplexBallVector& operator=(const ComplexBallVector& other);
  ComplexBallVector(ComplexBallVector&& other) noexcept;
  ComplexBallVector& operator=(ComplexBallVector&& other) noexcept;
  ~ComplexBallVector();
  // Non-owning, allocation-free access. The owner must outlive every borrow.
  // Reacquire after owner assignment/move or any structural mutation of v.
  // Mutable borrows may change ball values through FLINT, but must not clear,
  // initialize, shallow-copy or transfer ownership of their raw structs.
  acb_ptr data() & noexcept { return v.data(); }
  acb_srcptr data() const& noexcept { return v.data(); }
  acb_ptr data() && = delete;
  acb_srcptr data() const&& = delete;
  std::span<acb_struct> span() & noexcept { return v; }
  std::span<const acb_struct> span() const& noexcept { return v; }
  std::span<acb_struct> span() && = delete;
  std::span<const acb_struct> span() const&& = delete;
  acb_ptr operator[](size_t i) { return &v[i]; }
  acb_srcptr operator[](size_t i) const { return &v[i]; }
  size_t size() const { return v.size(); }
};

// Owns initialized FLINT balls and cannot be copied. Move costs and borrowed
// access rules are the same as for ComplexBallVector.
struct RealBallVector {
  // Public owned storage; the raw-storage restrictions above apply.
  std::vector<arb_struct> v;
  explicit RealBallVector(size_t n = 0);
  RealBallVector(const RealBallVector&) = delete;
  RealBallVector& operator=(const RealBallVector&) = delete;
  RealBallVector(RealBallVector&& other) noexcept;
  RealBallVector& operator=(RealBallVector&& other) noexcept;
  ~RealBallVector();
  arb_ptr data() & noexcept { return v.data(); }
  arb_srcptr data() const& noexcept { return v.data(); }
  arb_ptr data() && = delete;
  arb_srcptr data() const&& = delete;
  std::span<arb_struct> span() & noexcept { return v; }
  std::span<const arb_struct> span() const& noexcept { return v; }
  std::span<arb_struct> span() && = delete;
  std::span<const arb_struct> span() const&& = delete;
  arb_ptr operator[](size_t i) { return &v[i]; }
  arb_srcptr operator[](size_t i) const { return &v[i]; }
  size_t size() const { return v.size(); }
};

}  // namespace sic::support
