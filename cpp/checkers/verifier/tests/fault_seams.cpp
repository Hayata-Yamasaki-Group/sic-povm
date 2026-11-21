// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fault_seams.hpp"

#include "../src/runtime/fault_seams.hpp"

namespace sic_check::verifier::test {
int stat_fault = 0, seek_fault = 0, tell_fault = 0;

int root_fault = 0, finite_fault = 0;

bool pad_real_roots = false;

int padded_roots = 0;

slong test_find_roots(acb_ptr roots, const acb_poly_t poly, acb_srcptr initial, slong maxiter,
                      slong prec) {
  slong found = ::acb_poly_find_roots(roots, poly, initial, maxiter, prec);
  if (pad_real_roots && found == acb_poly_degree(poly)) {
    // Widen certified enclosures without losing their roots. The small fixture
    // stays disjoint, but its imaginary singletons now need an equality proof.
    for (slong j = 0; j < found; ++j) {
      arb_add_error_2exp_si(acb_imagref(roots + j), -prec / 2);
      ++padded_roots;
    }
  }
  return found;
}

int test_roots(qqbar_ptr roots, qqbar_srcptr coefficients, slong length, slong degree, slong bits) {
  int result = ::_qqbar_roots_poly_squarefree(roots, coefficients, length, degree, bits);
  if (root_fault) {
    root_fault = 0;
    return 0;
  }
  return result;
}

int test_finite(const acb_t value) {
  if (finite_fault && acb_is_one(value)) {
    --finite_fault;
    return 0;
  }
  return ::acb_is_finite(value);
}

int test_fstat(int fd, struct stat* out) {
  if (stat_fault) {
    stat_fault = 0;
    return -1;
  }
  return ::fstat(fd, out);
}

int test_fseeko(FILE* f, off_t off, int whence) {
  if (seek_fault) {
    seek_fault = 0;
    return -1;
  }
  return ::fseeko(f, off, whence);
}

off_t test_ftello(FILE* f) {
  int fault = tell_fault;
  tell_fault = 0;
  return fault == 1 ? -1 : fault == 2 ? off_t(1ull << 61) : ::ftello(f);
}
}  // namespace sic_check::verifier::test
