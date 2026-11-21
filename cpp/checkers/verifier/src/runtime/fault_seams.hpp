// SPDX-License-Identifier: AGPL-3.0-or-later
// Test-variant backend call declarations; the checker modules depend only on this contract.
#pragma once
#include <flint/acb_poly.h>
#include <flint/qqbar.h>
#include <sys/stat.h>

#include <cstdio>
namespace sic_check::verifier::test {

slong test_find_roots(acb_ptr roots, const acb_poly_t poly, acb_srcptr initial, slong maxiter,
                      slong prec);

int test_roots(qqbar_ptr roots, qqbar_srcptr coefficients, slong length, slong degree, slong bits);

int test_finite(const acb_t value);

int test_fstat(int fd, struct stat* out);

int test_fseeko(FILE* f, off_t off, int whence);

off_t test_ftello(FILE* f);

}  // namespace sic_check::verifier::test
