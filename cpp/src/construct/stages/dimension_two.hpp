// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/components/production_policy.hpp"
#include "sicx/codec/ball.hpp"
#include "sicx/codec/block.hpp"
#include "support/sha256.hpp"
// Native d = 2 closed form with independently supplied radical input.
#pragma once
#include "sic/data/text_formats.hpp"
#include "sic/support/flint_raii.hpp"
//
//
//   SIC_OUT=OUT.sicx SIC_D2_RADICALS=data/d2/d2.sicrad sic-construct 1 0 1 7
//
// d = 2 has no transported export. K = Q(sqrt(-3)) (D0 = -3), omega = (1 + sqrt(-3))/2, and the
// multiplet is the single fiducial class of the unnormalized vector v = (1, z) with
//     z = psi_1/psi_0 = (1 + i)(sqrt(3) - 1)/2 = -omega + (omega - 1) g_0,   g_0^2 = -1,
// in the tower K = F_0 < F_1 = K(g_0) (one quadratic level, L = Q(i, sqrt(3)), Smith [2],
// tau_0: g_0 -> -g_0). The identity z = (1 + i) beta/alpha holds for the radical record of
// d2.sicrad (alpha^2 = 1/2 + sqrt(3)/6, beta^2 = 1/4 - sqrt(3)/12, psi = (alpha, (1 + i) beta));
// the tool checks it numerically against that record (256-bit balls) before writing.
//
// Exact data (leaf coordinates in the trace-dual basis, x = lambda_0/2 + lambda_1 g/(2c), c = -1):
//   wsq 0 : c_0 = g_0^2 = -1                       levels [], scale 1, coordinates (-1, 0)
//   act 0 0: A = tau(g) g = 1                      levels [], scale 1, coordinates (1, 0)
//   rep 0 : eta_0 = 1 (lambda = 2, 0)              levels [], scale 2, coordinates (1, 0)
//   rep 1 : eta_1 = z (lambda = -2 omega, 2 - 2 omega)   levels [0], scale 2, coordinates (-omega,
//   1 - omega)
// BALL: the normalized gauged vector w = v/|v| (anchor 0), 256-bit Arb balls computed here from
// the exact leaves. The decoder supports the same dimension-two representation.
#include <flint/acb.h>
#include <flint/fmpq.h>

#include <fstream>
#include <iostream>
#include <sstream>

#include "sic/sicx/finalize.hpp"
#include "sicx/codec/rescale.hpp"

namespace sic::construct::dimension_two {

namespace detail {

inline const std::string kPinnedRadicals =
    std::string(sic::data::kExactD2RadicalsIdentity) +
    "\ndimension 2\nradicand 3\nalpha_squared 1/2 1/6\nbeta_squared 1/4 "
    "-1/12\nend\n";

struct Leaf {
  uint64_t s;
  long x, y;
};

std::string ball_text(const arb_t x);

// alpha^2, beta^2 as (x + y*sqrt(3)) from the radical file; returns false if the file has another
// shape.
void read_radicals(const std::string& path, fmpq_t a2x, fmpq_t a2y, fmpq_t b2x, fmpq_t b2y);

}  // namespace detail

using namespace detail;

// Writes OUT; radicals: the d2.sicrad record (checked numerically, cited in PROV); keep_balls:
// optional.
int make(const std::filesystem::path& out, const std::string& radicals,
         const std::string& keep_balls,
         const std::function<void(acb_srcptr, acb_srcptr)>& check_sic = {},
         const sic::construct::ProductionPolicy& policy = {});

}  // namespace sic::construct::dimension_two
