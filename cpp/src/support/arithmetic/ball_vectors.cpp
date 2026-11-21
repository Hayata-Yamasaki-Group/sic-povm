// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/ball_vectors.hpp"

namespace sic::support {
ComplexBallVector::ComplexBallVector(size_t n) : v(n) {
  for (auto& z : v) acb_init(&z);
}
}  // namespace sic::support

namespace sic::support {
ComplexBallVector::ComplexBallVector(const ComplexBallVector& other) : v(other.v.size()) {
  for (size_t i = 0; i < v.size(); ++i) {
    acb_init(&v[i]);
    acb_set(&v[i], &other.v[i]);
  }
}
}  // namespace sic::support

namespace sic::support {
ComplexBallVector& ComplexBallVector::operator=(const ComplexBallVector& other) {
  if (this != &other) {
    ComplexBallVector copy(other);
    v.swap(copy.v);
  }
  return *this;
}
}  // namespace sic::support

namespace sic::support {
ComplexBallVector::ComplexBallVector(ComplexBallVector&& other) noexcept : v(std::move(other.v)) {
  other.v.clear();
}
}  // namespace sic::support

namespace sic::support {
ComplexBallVector& ComplexBallVector::operator=(ComplexBallVector&& other) noexcept {
  if (this != &other) {
    for (auto& z : v) acb_clear(&z);
    v = std::move(other.v);
    other.v.clear();
  }
  return *this;
}
}  // namespace sic::support

namespace sic::support {
ComplexBallVector::~ComplexBallVector() {
  for (auto& z : v) acb_clear(&z);
}
}  // namespace sic::support

namespace sic::support {
RealBallVector::RealBallVector(size_t n) : v(n) {
  for (auto& z : v) arb_init(&z);
}
}  // namespace sic::support

namespace sic::support {
RealBallVector::RealBallVector(RealBallVector&& other) noexcept : v(std::move(other.v)) {
  other.v.clear();
}
}  // namespace sic::support

namespace sic::support {
RealBallVector& RealBallVector::operator=(RealBallVector&& other) noexcept {
  if (this != &other) {
    for (auto& z : v) arb_clear(&z);
    v = std::move(other.v);
    other.v.clear();
  }
  return *this;
}
}  // namespace sic::support

namespace sic::support {
RealBallVector::~RealBallVector() {
  for (auto& z : v) arb_clear(&z);
}
}  // namespace sic::support
