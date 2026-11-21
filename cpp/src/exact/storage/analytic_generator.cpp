// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/storage/analytic_generator.hpp"

#include "support/resources/platform_snapshot.hpp"

namespace sic::exact {
int eighth_character(long norm) {
  const long residue = (norm % 8 + 8) % 8;
  if (residue == 1 || residue == 3) return 1;
  if (residue == 5 || residue == 7) return -1;
  throw std::invalid_argument("eighth-root character requires an odd norm");
}

void AnalyticGenerator::assign(sic::support::ComplexBallVector&& values, bool quadratic,
                               bool realtail, bool top) {
  periodic = sic::support::RealBallVector();
  periodic_index = {};
  backing.reset();
  pieces.clear();
  resident_first = 0;
  count = (long)values.size();
  paired = quadratic && realtail;
  imaginary = paired && top;
  character = false;
  root = sic::support::ComplexBallVector();
  sign = {};
  if (paired) {
    scalar = sic::support::RealBallVector(count / 2);
    for (long c = 0; c < count / 2; ++c)
      arb_swap(scalar[c], imaginary ? acb_imagref(values[2 * c]) : acb_realref(values[2 * c]));
    full = sic::support::ComplexBallVector();
  } else {
    scalar = sic::support::RealBallVector();
    full = std::move(values);
  }
}

void AnalyticGenerator::assign_scalar(sic::support::RealBallVector&& values, bool top) {
  periodic = sic::support::RealBallVector();
  periodic_index = {};
  backing.reset();
  pieces.clear();
  resident_first = 0;
  count = 2 * (long)values.size();
  paired = true;
  imaginary = top;
  character = false;
  scalar = std::move(values);
  full = sic::support::ComplexBallVector();
  root = sic::support::ComplexBallVector();
  sign = {};
}

void AnalyticGenerator::assign_periodic(sic::support::RealBallVector&& values, long logical_count,
                                        std::function<size_t(long)> index) {
  if (values.size() == 0 || logical_count <= 0 || logical_count % 2 || !index)
    throw std::invalid_argument("invalid periodic analytic generator");
  backing.reset();
  pieces.clear();
  resident_first = 0;
  full = sic::support::ComplexBallVector();
  scalar = sic::support::RealBallVector();
  root = sic::support::ComplexBallVector();
  sign = {};
  periodic = std::move(values);
  periodic_index = std::move(index);
  count = logical_count;
  paired = true;
  imaginary = character = false;
}

void AnalyticGenerator::get(acb_ptr out, long index) const {
  if (!paired) {
    acb_set(out, full[index]);
    return;
  }
  acb_zero(out);
  arb_ptr component = imaginary ? acb_imagref(out) : acb_realref(out);
  if (character) {
    arb_set(component, acb_imagref(root[0]));
    if (sign(index) < 0) arb_neg(component, component);
  } else if (periodic.size()) {
    arb_set(component, periodic[periodic_index(index / 2)]);
    if (index & 1) arb_neg(component, component);
  } else {
    const size_t pair = index / 2;
    if (pair < resident_first || pair - resident_first >= scalar.size())
      throw std::logic_error("analytic generator label is not resident");
    arb_set(component, scalar[pair - resident_first]);
    if (index & 1) arb_neg(component, component);
  }
}

arb_srcptr AnalyticGenerator::real_even(long pair) const {
  if (periodic.size()) return periodic[periodic_index(pair)];
  if (paired && !imaginary && !character) {
    if (pair < 0 || (size_t)pair < resident_first || (size_t)pair - resident_first >= scalar.size())
      throw std::logic_error("real generator label is not resident");
    return scalar[pair - resident_first];
  }
  if (!paired) return acb_realref(full[2 * pair]);
  throw std::logic_error("real analytic generator component unavailable");
}

void AnalyticGenerator::imaginary_even(arb_ptr out, long pair) const {
  if (!paired) {
    arb_set(out, acb_imagref(full[2 * pair]));
    return;
  }
  if (!imaginary) {
    arb_zero(out);
    return;
  }
  if (character) {
    arb_set(out, acb_imagref(root[0]));
    if (sign(2 * pair) < 0) arb_neg(out, out);
  } else
    arb_set(out, scalar[pair]);
}

void AnalyticGenerator::assign_character(acb_srcptr root_value, std::function<int(long)> signs) {
  periodic = sic::support::RealBallVector();
  periodic_index = {};
  backing.reset();
  pieces.clear();
  resident_first = 0;
  sic::support::ComplexBallVector isolated(1);
  acb_set(isolated[0], root_value);
  full = sic::support::ComplexBallVector();
  scalar = sic::support::RealBallVector();
  root = std::move(isolated);
  paired = imaginary = character = true;
  sign = std::move(signs);
}

void AnalyticGenerator::prepare() const {
  if (!backing || (resident_first == 0 && scalar.size() == (size_t)count / 2)) return;
  park();
  if (pieces.size() <= 1)
    scalar = backing->real();
  else {
    scalar = sic::support::RealBallVector(count / 2);
    for (size_t part = 0; part < pieces.size(); ++part) {
      sic::support::RealBallVector values = pieces[part]->real();
      for (size_t q = 0; q < values.size(); ++q) arb_swap(scalar[part * piece_size + q], values[q]);
    }
  }
}

void AnalyticGenerator::prepare_piece(size_t first, size_t size) const {
  if (!backing || !piece_size || first % piece_size || first >= (size_t)count / 2 ||
      size != std::min(piece_size, (size_t)count / 2 - first))
    throw std::logic_error("invalid analytic generator piece request");
  park();
  scalar = pieces.at(first / piece_size)->real();
  if (scalar.size() != size) throw std::logic_error("analytic generator piece size mismatch");
  resident_first = first;
}

void AnalyticGenerator::park() const {
  if (!backing) return;
  const bool large = scalar.size() >= 1048576;
  scalar = sic::support::RealBallVector();
  resident_first = 0;
#if defined(__GLIBC__)
  if (large) sic::support::resources::release_free_heap();
#else
  (void)large;
#endif
}

void AnalyticGenerator::spill(size_t block_size) {
  if (periodic.size()) return;
  if (!paired || imaginary || character || scalar.size() != (size_t)count / 2)
    throw std::logic_error("only resident real quadratic generators may be spilled");
  if (!block_size) throw std::invalid_argument("zero analytic generator piece size");
  if (!backing) {
    piece_size = block_size;
    for (size_t first = 0; first < scalar.size(); first += piece_size) {
      sic::support::RealBallVector part(std::min(piece_size, scalar.size() - first));
      for (size_t q = 0; q < part.size(); ++q) arb_swap(part[q], scalar[first + q]);
      pieces.push_back(BallFile::save(part));
    }
    backing = pieces.at(0);
  }
  park();
}

}  // namespace sic::exact
