// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/core/sic_frame.hpp"

namespace sic::core {
Vector normalized_copy(const Vector& psi, Real* norm) {
  if (psi.empty()) throw sic::Error(ErrorKind::invalid_argument, "normalized_copy: empty vector");
  Real scale = 0;
  for (const Complex& z : psi) {
    if (!std::isfinite(z.real()) || !std::isfinite(z.imag()))
      throw sic::Error(ErrorKind::invalid_argument, "normalized_copy: nonfinite entry");
    scale = std::max({scale, std::abs(z.real()), std::abs(z.imag())});
  }
  if (scale == 0) throw sic::Error(ErrorKind::invalid_argument, "normalized_copy: zero vector");
  Real s2 = 0;
  for (const Complex& z : psi) s2 += std::norm(z / scale);
  const Real s = std::sqrt(s2);
  if (norm) *norm = scale * s;
  Vector out(psi);
  for (Complex& z : out) z = (z / scale) / s;
  return out;
}
}  // namespace sic::core

namespace sic::core {
SicFrame::SicFrame(const Vector& fiducial, bool normalize, uint64_t maximum_dimension)
    : d_(check_dim(fiducial.size(), maximum_dimension)), wh_(static_cast<int>(d_)) {
  psi_ = normalize ? normalized_copy(fiducial, &input_norm_) : fiducial;
  if (!normalize) {
    Real n2 = 0;
    for (const Complex& z : psi_) n2 += std::norm(z);
    input_norm_ = std::sqrt(n2);
  }
}
}  // namespace sic::core

namespace sic::core {
void SicFrame::row_block(std::int64_t p1, Vector& out) const {
  out.resize(d_ * d_);
  for (std::size_t p2 = 0; p2 < d_; ++p2)
    displace(p1, static_cast<std::int64_t>(p2), psi_.data(), out.data() + p2 * d_);
}
}  // namespace sic::core

namespace sic::core {
std::vector<Complex> SicFrame::overlaps() const { return wh_.overlaps(psi_); }
}  // namespace sic::core

namespace sic::core {
std::size_t SicFrame::check_dim(std::size_t n, uint64_t maximum_dimension) {
  if (n == 0) throw sic::Error(ErrorKind::invalid_argument, "SicFrame: empty fiducial");
  if (n > maximum_dimension || n > (uint64_t{1} << 30))
    throw sic::Error(ErrorKind::resource, "SicFrame: dimension too large");
  return n;
}
}  // namespace sic::core

namespace sic::core {
bool SicVectorStream::next(Vector& out) {
  const auto d = static_cast<std::int64_t>(frame_.dim());
  if (p1_ >= d) return false;
  cur1_ = p1_;
  cur2_ = p2_;
  frame_.vector(p1_, p2_, out);
  if (++p2_ == d) {
    p2_ = 0;
    ++p1_;
  }
  return true;
}
}  // namespace sic::core

namespace sic::core {
SicCheck check_sic(const SicFrame& frame) {
  SicCheck r;
  r.d = frame.dim();
  r.input_norm = frame.input_norm();
  r.norm_defect = std::abs(r.input_norm - 1);
  const Real target = 1 / (static_cast<Real>(r.d) + 1);
  Real sum = 0;
  Vector row;
  for (std::int64_t p1 = 0; p1 < r.d; ++p1) {
    frame.overlap_row(p1, row);
    for (std::int64_t p2 = 0; p2 < r.d; ++p2) {
      const Real s = std::norm(row[static_cast<std::size_t>(p2)]);
      sum += s;
      if (p1 == 0 && p2 == 0) continue;
      const Real err = std::abs(s - target);
      if (!std::isfinite(err)) throw std::runtime_error("check_sic: nonfinite overlap");
      if (err > r.max_overlap_error) {
        r.max_overlap_error = err;
        r.worst_p1 = p1;
        r.worst_p2 = p2;
      }
    }
  }
  r.parseval_defect = std::abs(sum - r.d);
  return r;
}
}  // namespace sic::core

namespace sic::core {
SicCheck check_sic(const Vector& psi) { return check_sic(SicFrame(psi)); }
}  // namespace sic::core

namespace sic::core {
void SicFrame::vector(std::int64_t p1, std::int64_t p2, Vector& out) const {
  out.resize(d_);
  displace(p1, p2, psi_.data(), out.data());
}
}  // namespace sic::core
