// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests of sic/core/fft.hpp, sic/core/sic_frame.hpp and sic/core/clifford.hpp against direct sums,
// plus Clifford identification and stabilizers of released FP64 fiducials.
//   usage: sic-test-search-clifford [fiducial.csv ...]   (files of equal dimension form one group;
//   files
//          of different records of one dimension must be Clifford-inequivalent)
#include "sic/search/clifford.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "assertions.hpp"
#include "sic/core/fft.hpp"
#include "sic/core/fiducial_io.hpp"
#include "sic/core/sic_frame.hpp"
#include "sic/core/weyl_heisenberg.hpp"

using sic::core::Complex;
using sic::core::Vector;

static Vector random_vector(int d, std::mt19937_64& rng) {
  std::normal_distribution<double> normal;
  Vector v(static_cast<std::size_t>(d));
  for (auto& z : v) z = Complex(normal(rng), normal(rng));
  return v;
}
static double distance(const Vector& x, const Vector& y) {
  double s = 0;
  for (std::size_t k = 0; k < x.size(); ++k) s += std::norm(x[k] - y[k]);
  return std::sqrt(s);
}
static Complex inner(const Vector& x, const Vector& y) {
  Complex s = 0;
  for (std::size_t k = 0; k < x.size(); ++k) s += std::conj(x[k]) * y[k];
  return s;
}
static sic::core::clifford::Mat2 random_esl(sic::core::clifford::Int d,
                                            sic::core::clifford::Int sign, std::mt19937_64& rng) {
  const sic::core::clifford::Int db = sic::core::clifford::dbar_of(d);
  std::uniform_int_distribution<sic::core::clifford::Int> u(0, db - 1);
  for (;;) {
    const sic::core::clifford::Mat2 f{u(rng), u(rng), u(rng), u(rng)};
    if (sic::core::clifford::det(f, db) == sic::core::clifford::mod(sign, db)) return f;
  }
}

static void test_dft() {
  std::mt19937_64 rng(1);
  for (std::size_t n : {1u, 2u, 3u, 4u, 5u, 8u, 12u, 16u, 64u}) {
    const sic::core::Dft dft(n);
    for (int sign : {-1, 1}) {
      const Vector x = random_vector(static_cast<int>(n), rng);
      Vector y = x;
      dft.transform(y, sign);
      double err = 0;
      for (std::size_t k = 0; k < n; ++k) {
        Complex s = 0;
        for (std::size_t j = 0; j < n; ++j)
          s +=
              x[j] * std::polar(1.0, sign * 2 * std::numbers::pi * double((j * k) % n) / double(n));
        err = std::max(err, std::abs(s - y[k]));
      }
      SIC_CHECK(err < 1e-12 * double(n));
    }
  }
}

static void test_frame_against_direct() {
  std::mt19937_64 rng(2);
  for (int d : {1, 2, 3, 4, 6, 8, 16}) {
    const sic::core::WeylHeisenberg wh(d);
    const Vector psi = sic::core::normalized_copy(random_vector(d, rng));
    const Vector phi = random_vector(d, rng);
    const sic::core::SicFrame frame(psi);
    Vector v, row, cross;
    double err_vec = 0, err_ov = 0, err_cross = 0;
    for (int p1 = -d; p1 < 2 * d; p1 += (d > 4 ? 3 : 1)) {
      frame.overlap_row(p1, row);
      frame.cross_overlap_row(p1, phi, cross);
      for (int p2 = 0; p2 < d; ++p2) {
        frame.vector(p1, p2, v);
        const Vector direct = wh.apply(p1, p2, psi);
        err_vec = std::max(err_vec, distance(v, direct));
        err_ov = std::max(err_ov, std::abs(row[p2] - inner(psi, direct)));
        err_cross = std::max(err_cross, std::abs(cross[p2] - inner(direct, phi)));
      }
    }
    SIC_CHECK(err_vec < 1e-13);
    SIC_CHECK(err_ov < 1e-13);
    SIC_CHECK(err_cross < 1e-12);
    // Streaming: all d^2 vectors, in order, equal to vector().
    std::size_t count = 0;
    double err_stream = 0;
    frame.for_each_vector([&](std::int64_t p1, std::int64_t p2, const Complex* w) {
      SIC_CHECK(p1 == static_cast<std::int64_t>(count) / d &&
                p2 == static_cast<std::int64_t>(count) % d);
      frame.vector(p1, p2, v);
      for (int k = 0; k < d; ++k) err_stream = std::max(err_stream, std::abs(w[k] - v[k]));
      ++count;
    });
    SIC_CHECK(count == static_cast<std::size_t>(d) * d);
    SIC_CHECK(err_stream == 0);
    sic::core::SicVectorStream stream(frame);
    std::size_t pulled = 0;
    while (stream.next(v)) {
      Vector block;
      if (stream.p2() == 0) {
        frame.row_block(stream.p1(), block);
        SIC_CHECK(distance(Vector(block.begin(), block.begin() + d), v) == 0);
      }
      ++pulled;
    }
    SIC_CHECK(pulled == static_cast<std::size_t>(d) * d);
    const sic::core::SicCheck chk = sic::core::check_sic(frame);
    SIC_CHECK(chk.parseval_defect < 1e-12 * d);
  }
}

// U_F D_p U_F^dagger = D_(F p) for random F in ESL(2, Z_dbar), checked on a random vector.
static void test_clifford_action() {
  std::mt19937_64 rng(3);
  for (int d : {2, 3, 4, 5, 6, 8, 16, 32}) {
    const sic::core::WeylHeisenberg wh(d);
    const sic::core::clifford::CliffordAction action(d);
    const sic::core::clifford::Int db = sic::core::clifford::dbar_of(d);
    for (int trial = 0; trial < 12; ++trial) {
      const sic::core::clifford::Int sign = trial % 3 == 2 ? -1 : 1;
      sic::core::clifford::Mat2 f = random_esl(d, sign, rng);
      if (trial == 0)
        f = sic::core::clifford::Mat2{1, 2 % db, 0, 1};  // b = 2 is not a unit for even d
      const Vector v = random_vector(d, rng);
      Vector uv, lhs, rhs;
      action.apply(f, v, uv);
      SIC_CHECK(std::abs(inner(uv, uv).real() - inner(v, v).real()) < 1e-12 * inner(v, v).real());
      double err = 0;
      for (int p1 = 0; p1 < db; p1 += (d > 8 ? 5 : 1))
        for (int p2 = 0; p2 < db; p2 += (d > 8 ? 3 : 1)) {
          action.apply(f, wh.apply(p1, p2, v), lhs);
          const auto q = sic::core::clifford::apply(f, p1, p2, db);
          rhs = wh.apply(q[0], q[1], uv);
          err = std::max(err, distance(lhs, rhs));
        }
      SIC_CHECK(err < 1e-11);
    }
  }
}

struct Record {
  std::string path;
  Vector psi;
};

static void test_fiducials(const std::vector<Record>& records) {
  std::mt19937_64 rng(4);
  std::map<int, std::vector<const Record*>> by_d;
  for (const auto& r : records) by_d[static_cast<int>(r.psi.size())].push_back(&r);
  for (const auto& [d, group] : by_d) {
    const sic::core::WeylHeisenberg wh(d);
    for (const Record* r : group) {
      const sic::core::SicFrame frame(r->psi);
      const sic::core::SicCheck chk = sic::core::check_sic(frame);
      SIC_CHECK(chk.passes(1e-12));
      SIC_CHECK(chk.norm_defect < 1e-14);
      // Brute force overlaps for d <= 64.
      if (d <= 64) {
        const auto all = frame.overlaps();
        double err = 0;
        for (int p1 = 0; p1 < d; ++p1)
          for (int p2 = 0; p2 < d; ++p2)
            err = std::max(err,
                           std::abs(all[p1 * d + p2] -
                                    inner(frame.fiducial(), wh.apply(p1, p2, frame.fiducial()))));
        SIC_CHECK(err < 1e-13);
      }
      sic::search::MatchOptions all;
      all.find_all = true;
      const auto stab = sic::search::clifford_maps(r->psi, r->psi, all);
      bool stab_has_anti = false;
      for (const auto& s : stab) stab_has_anti = stab_has_anti || s.antiunitary();
      // Random Clifford images are identified, with an explicit element reproducing them.
      const int trials = d <= 64 ? 4 : 2;
      for (int t = 0; t < trials; ++t) {
        sic::core::clifford::CliffordElement g;
        g.sign = t % 2 == 1 ? -1 : 1;
        g.F = random_esl(d, g.sign, rng);
        g.chi1 = std::uniform_int_distribution<int>(0, d - 1)(rng);
        g.chi2 = std::uniform_int_distribution<int>(0, d - 1)(rng);
        Vector image = sic::core::clifford::apply_element(g, frame.fiducial());
        const Complex phase = std::polar(1.7, 0.3 + t);
        for (auto& z : image) z *= phase;
        const auto found = sic::search::clifford_maps(r->psi, image);
        SIC_CHECK(found.size() == 1);
        if (!found.empty()) {
          const Vector back = sic::core::clifford::apply_element(found[0], frame.fiducial());
          SIC_CHECK(std::abs(std::abs(inner(back, sic::core::normalized_copy(image))) - 1) < 1e-10);
          // The first element found is unitary when one exists (unitary pass first).
          SIC_CHECK(found[0].antiunitary() == (g.antiunitary() && !stab_has_anti));
        }
      }
      // Stabilizer: contains the identity, closes under multiplication mod d.
      bool has_identity = false;
      for (const auto& s : stab)
        if (sic::core::clifford::reduce(s.F, d) == sic::core::clifford::Mat2{1, 0, 0, 1} &&
            s.sign == 1 && s.chi1 == 0 && s.chi2 == 0)
          has_identity = true;
      SIC_CHECK(has_identity);
      SIC_CHECK(stab.size() >= 3 || d == 2);  // a Zauner-type order-3 symmetry at least
      for (const auto& x : stab)
        for (const auto& y : stab) {
          const sic::core::clifford::Mat2 xy = sic::core::clifford::mul(
              sic::core::clifford::reduce(x.F, d), sic::core::clifford::reduce(y.F, d), d);
          bool closed = false;
          for (const auto& z : stab)
            if (sic::core::clifford::reduce(z.F, d) == xy && z.sign == x.sign * y.sign)
              closed = true;
          SIC_CHECK(closed);
        }
      std::cout << "d=" << d << " " << r->path << ": max dev " << chk.max_overlap_error
                << ", |stab| " << stab.size() << "\n";
    }
    // Distinct records of one dimension are Clifford-inequivalent.
    for (std::size_t i = 0; i < group.size(); ++i)
      for (std::size_t j = i + 1; j < group.size(); ++j)
        SIC_CHECK(sic::search::clifford_maps(group[i]->psi, group[j]->psi).empty());
  }
}

int main(int argc, char** argv) {
  test_dft();
  test_frame_against_direct();
  test_clifford_action();
  std::vector<Record> records;
  std::vector<Record> oracles;
  bool compare = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--oracles") {
      compare = true;
      continue;
    }
    auto& destination = compare ? oracles : records;
    destination.push_back({argv[i], sic::core::files::read_fiducial(argv[i])});
  }
  test_fiducials(records);
  if (compare) {
    SIC_CHECK(records.size() == oracles.size());
    std::vector<bool> matched(oracles.size());
    for (const auto& record : records) {
      // The archival release used other coordinate gauges and ray labels.
      // Find a one-to-one correspondence in the extended Clifford group.
      size_t i = 0;
      for (; i < oracles.size(); ++i) {
        if (!matched[i] && record.psi.size() == oracles[i].psi.size() &&
            !sic::search::clifford_maps(record.psi, oracles[i].psi).empty()) {
          matched[i] = true;
          break;
        }
      }
      SIC_CHECK(i != oracles.size());
    }
  }
  return sic_test::report();
}
