// SPDX-License-Identifier: AGPL-3.0-or-later
// sic-test-exact-group-metadata CASE: exact class-group metadata of the selected order of K =
// Q(sqrt(Delta)) for a dimension d = 2^s, checked by exact assertions on sic::exact (arith.hpp,
// qk.hpp), printed in the committed canonical format.
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <vector>

#include "exact/arithmetic/arith.hpp"

namespace {
using namespace sic::exact;
std::uint64_t checks = 0;
void require(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

// Every value below is either an exact-header expectation or an independently
// recorded ring-class formula expectation that the exact
// computation must reproduce. Derived quantities are recomputed, not trusted.
struct Case {
  const char* name;
  int s;
  bool minimal;                    // producer SIC_ORDER=minimal
  long order_index;                // [O_K : Z[eps]]
  long original_Delta, Delta;      // t^2-4 and the fundamental discriminant
  std::vector<long> Delta_primes;  // squarefree factorization of Delta
  long levels;  // unused here (export-header check of the selector metadata contract)
  std::vector<std::pair<long, long>> odd_levels;  // unused here (same)
  long principal_B, principal_C;                  // reduced principal form (1, B, C)
  long narrow, ordinary, group;                   // h+, h, |G'| = h+ d^2 / 2
  long conductor;  // conductor of the selected order in O_K (1: maximal)
};

const std::vector<Case>& cases() {
  static const std::vector<Case> table{
      {"maximal128", 7, true, 5, 16125, 645, {3, 5, 43}, 15, {}, -27, 21, 4, 2, 32768, 1},
      {"standard256",
       8,
       false,
       1,
       65021,
       65021,
       {11, 23, 257},
       19,
       {{17, 3}},
       -255,
       1,
       24,
       12,
       786432,
       1},
      {"standard64", 6, false, 1, 3965, 3965, {5, 13, 61}, 14, {}, -63, 1, 8, 4, 16384, 1},
      {"maximal512",
       9,
       true,
       3,
       261117,
       29013,
       {3, 19, 509},
       20,
       {{18, 3}},
       -171,
       57,
       12,
       6,
       1572864,
       1},
      // Metadata-only forward case: no export header or Group is pinned yet.
      {"maximal1024",
       10,
       true,
       5,
       1046525,
       41861,
       {41, 1021},
       22,
       {{20, 5}},
       -205,
       41,
       20,
       10,
       10485760,
       1},
      // Nonmaximal selected orders: the producer default Z[eps] with conductor > 1.
      // Every order quantity uses the arithmetic discriminant conductor^2 * Delta; Delta
      // stays the field discriminant (export header D0).
      {"standard128",
       7,
       false,
       1,
       16125,
       645,
       {3, 5, 43},
       16,
       {{14, 5}},
       -127,
       1,
       20,
       10,
       163840,
       5},
      {"standard512",
       9,
       false,
       1,
       261117,
       29013,
       {3, 19, 509},
       21,
       {{18, 3}, {19, 3}},
       -511,
       1,
       36,
       18,
       4718592,
       3},
      // Fixed-field-tower validation cases and the d2048 target (all-quadratic towers).
      {"standard16", 4, false, 1, 221, 221, {13, 17}, 9, {}, -15, 1, 4, 2, 512, 1},
      {"standard32", 5, false, 1, 957, 957, {3, 11, 29}, 11, {}, -31, 1, 4, 2, 2048, 1},
      // standard2048: no golden (about 30 min); kept so the table equals the selector contract
      // tool's.
      {"standard2048",
       11,
       false,
       1,
       4190205,
       4190205,
       {3, 5, 409, 683},
       28,
       {},
       -2047,
       1,
       128,
       64,
       268435456,
       1},
  };
  return table;
}

const Case* active = nullptr;
const Case& C() { return *active; }

const Case& select_case(std::string_view name) {
  for (const auto& item : cases())
    if (name == item.name) return item;
  throw std::runtime_error("unknown case");
}

void check() {
  Arithmetic ar(C().s, C().minimal);
  const long d = 1L << C().s, d2 = d * d, t = d - 1, f = C().order_index;
  const long group_size = ar.h() * d2 / 2;
  const long arith = C().conductor * C().conductor * C().Delta;  // selected-order discriminant
  require(ar.d == d && ar.order_index == f && ar.original_Delta == C().original_Delta &&
              ar.Delta == arith && ar.kc.Delta == arith,
          "selected arithmetic order mismatch");
  long prime_product = 1;
  for (long prime : C().Delta_primes) prime_product *= prime;
  require(C().original_Delta == t * t - 4 && C().original_Delta == f * f * arith &&
              C().conductor >= 1 && prime_product == C().Delta && C().Delta % 4 == 1,
          "conductor/fundamental discriminant mismatch");
  for (long prime = 2; prime * prime <= C().Delta; ++prime)
    require(C().Delta % (prime * prime) != 0, "fundamental discriminant not squarefree");
  require(ar.eps.a * Rational(2) == Rational(t) && norm(ar.kc, ar.eps) == Rational(1),
          "dimension unit trace/norm");
  require(equal(mul(ar.kc, ar.eps, ar.eps), sub(scal(Rational(t), ar.eps), QuadraticNumber(1))),
          "order multiplication relation");
  const QuadraticNumber omega(Rational(1, 2), Rational(1, 2));
  require(lattice_equal(ar.kc, ar.class_involution(0), Lattice{omega, QuadraticNumber(1)}),
          "actual maximal principal order basis");
  // eps = (t + f sqrt(Delta))/2 = f*omega + (t-f)/2: Z[eps] has index f in O_K.
  require(equal(ar.eps, add(scal(Rational(f), omega), QuadraticNumber((t - f) / 2))),
          "dimension-unit suborder index");
  const QuadraticNumber alpha = sub(QuadraticNumber(1), ar.eps);
  require(totally_positive(ar.kc, ar.eps) && sign1(ar.kc, sub(ar.eps, QuadraticNumber(1))) > 0,
          "positive dimension unit branch");
  require(norm(ar.kc, alpha) == Rational(2 - t) &&
              equal(mul(ar.kc, alpha, alpha), scal(Rational(t - 2), ar.eps)),
          "exact ordinary sign element and positive square witness");
  const auto principal = ar.cls[0].cycle[0];
  long root = 0;
  while ((root + 1) * (root + 1) <= arith) ++root;
  long pb = root + 1;
  if (pb % 2 == 0) ++pb;
  require(principal.A == 1 && principal.B == C().principal_B && principal.C == C().principal_C &&
              pb == -C().principal_B && pb * pb - arith == 4 * C().principal_C &&
              equal(ar.cls[0].tau[0], add(omega, QuadraticNumber((pb - 1) / 2))),
          "actual principal form and basis");
  // Overflow guard: reduced forms have A, C < b <= (Delta + 1) / 2.
  const long form_bound = std::max<long>(1000000, (ar.Delta + 1) / 2 + 1);
  std::set<std::tuple<long, long, long>> reduced_forms;
  for (const auto& cls : ar.cls) {
    QuadraticNumber period(1);
    require(!cls.cycle.empty() && cls.cycle.size() == cls.tau.size(), "cycle coordinate mismatch");
    for (size_t i = 0; i < cls.cycle.size(); ++i) {
      const auto form = cls.cycle[i];
      require(form.A > 0 && form.A < form_bound && std::abs(form.B) < form_bound &&
                  std::abs(form.C) < form_bound,
              "bounded primitive form coordinates");
      require(form.C > 0 && form.A + form.B + form.C < 0 &&
                  form.B * form.B - 4 * form.A * form.C == ar.Delta &&
                  std::gcd(std::gcd(form.A, form.B), form.C) == 1,
              "primitive order form");
      require(reduced_forms.emplace(form.A, form.B, form.C).second,
              "reduced form occurs only once");
      require(equal(cls.tau[i], ar.tau_of(form)), "exact form coordinate");
      const Lattice ideal{cls.tau[i], QuadraticNumber(1)};
      const Lattice conjugate{conj(ideal.w1), conj(ideal.w2)};
      require(lattice_equal(ar.kc, lattice_mul(ar.kc, ar.class_involution(0), ideal), ideal),
              "every primitive form ideal is stable under the actual order");
      require(lattice_equal(ar.kc, lattice_mul(ar.kc, ideal, conjugate),
                            lattice_scale(ar.kc, QuadraticNumber(Rational(1, form.A), Rational(0)),
                                          ar.class_involution(0))),
              "exact invertibility witness for the maximal order ideal");
      const long digit = ceil1(ar.kc, cls.tau[i]);
      const auto next = cls.cycle[(i + 1) % cls.cycle.size()];
      require(next.A == form.A * digit * digit + form.B * digit + form.C &&
                  next.B == -2 * form.A * digit - form.B && next.C == form.A,
              "exact closed reduced-cycle transition");
      const QuadraticNumber transition = sub(QuadraticNumber(digit), cls.tau[i]);
      require(
          totally_positive(ar.kc, transition) &&
              lattice_equal(ar.kc, Lattice{cls.tau[(i + 1) % cls.tau.size()], QuadraticNumber(1)},
                            lattice_scale(ar.kc, inv(ar.kc, transition), ideal)),
          "positive exact cycle lattice witness");
      period = mul(ar.kc, period, cls.tau[i]);
    }
    require(equal(period, ar.eps), "every reduced cycle has exact epsilon period");
  }
  // Independent rectangle/factor enumeration, rather than the producer's
  // triangular (u,k) scan. If b=-B=A+C+k and k>=1, then
  // Delta=(A-C)^2+2k(A+C)+k^2, giving b<=(Delta+1)/2.
  // For the enumeration window, for the smaller divisor a <= c of ac, a + c < b
  // forces (c - a)^2 = (a + c)^2 - 4ac < b^2 - (b^2 - Delta) = Delta, hence
  // c < a + root_ceil with root_ceil = ceil(sqrt(Delta)) and ac < a (a + root_ceil).
  // The scan below runs a down from isqrt(ac) and stops at the first a with
  // a (a + root_ceil) <= ac; every smaller a also fails, so nothing is skipped.
  // Each divisor pair is emitted as (a, c) and, when distinct, (c, a); the
  // original conditions (a + 1 < b, a + c < b, primitivity) are then checked
  // for each orientation.
  std::set<std::tuple<long, long, long>> independent_forms;
  long root_ceil = 0;
  while (root_ceil * root_ceil < ar.Delta) ++root_ceil;
  long small = 0;  // isqrt(ac); ac grows with b, so it is advanced monotonically
  for (long b = 1; b <= (ar.Delta + 1) / 2; ++b) {
    const long numerator = b * b - ar.Delta;
    if (numerator <= 0 || numerator % 4) continue;
    const long ac = numerator / 4;
    while ((small + 1) * (small + 1) <= ac) ++small;
    for (long a = small; a >= 1 && a * (a + root_ceil) > ac; --a) {
      if (ac % a) continue;
      const long c = ac / a;
      for (const auto& [x, y] : {std::pair{a, c}, std::pair{c, a}})
        if (x + 1 < b && x + y < b && std::gcd(std::gcd(x, b), y) == 1)
          independent_forms.emplace(x, -b, y);
    }
  }
  require(reduced_forms == independent_forms, "complete independently enumerated reduced-form set");

  const int h = ar.h();
  std::vector<int> product(h * h), partner(h), ordinary(h);
  for (int a = 0; a < h; ++a)
    for (int b = 0; b < h; ++b) {
      const auto ideal = lattice_mul(ar.kc, ar.class_involution(a), ar.class_involution(b));
      const auto [c, mu] = ar.identify(ideal);
      require(totally_positive(ar.kc, mu) &&
                  lattice_equal(ar.kc, ideal, lattice_scale(ar.kc, mu, ar.class_involution(c))),
              "exact positive class product witness");
      product[a * h + b] = c;
    }
  for (int a = 0; a < h; ++a) {
    const auto ideal =
        lattice_scale(ar.kc, sub(QuadraticNumber(1), ar.eps), ar.class_involution(a));
    const auto [c, mu] = ar.identify(ideal);
    require(totally_positive(ar.kc, mu) &&
                lattice_equal(ar.kc, ideal, lattice_scale(ar.kc, mu, ar.class_involution(c))),
            "exact positive sign-partner witness");
    partner[a] = c;
    const auto la = ar.cls[a].cycle.size(), lc = ar.cls[c].cycle.size();
    ordinary[a] = (la < lc || (la == lc && a < c)) ? a : c;
  }
  for (int a = 0; a < h; ++a) {
    require(product[a] == a && product[a * h] == a, "class identity");
    require(partner[a] != a && partner[partner[a]] == a && ordinary[partner[a]] == ordinary[a],
            "nontrivial ordinary sign quotient");
    for (int b = 0; b < h; ++b) {
      require(product[a * h + b] == product[b * h + a] &&
                  ordinary[product[a * h + b]] == ordinary[product[ordinary[a] * h + ordinary[b]]],
              "commutative ordinary class product");
      for (int c = 0; c < h; ++c)
        require(product[product[a * h + b] * h + c] == product[a * h + product[b * h + c]],
                "class associativity");
    }
  }
  const std::set<int> ordinary_labels(ordinary.begin(), ordinary.end());
  std::cout << "dimension " << d << "\norder_index " << f << "\nconductor " << C().conductor
            << "\narithmetic_discriminant " << ar.Delta << "\nfield_discriminant " << C().Delta
            << '\n';
  std::cout << "derived_narrow_classes " << h << "\nderived_ordinary_classes "
            << ordinary_labels.size() << "\nderived_group_degree_model " << group_size << '\n';
  for (int a = 0; a < h; ++a) {
    const auto& cls = ar.cls[a];
    std::cout << "narrow " << a << " partner " << partner[a] << " ordinary " << ordinary[a]
              << " cycle_length " << cls.cycle.size() << " tau " << cls.tau[0].a.str() << ' '
              << cls.tau[0].b.str() << '\n';
  }
  require(h == C().narrow && static_cast<long>(ordinary_labels.size()) == C().ordinary &&
              group_size == C().group,
          "derived fixed case counts");
  std::cout << "METADATA_PASS exact_checks=" << checks
            << "; no Group, Table, constructor or selector word computed\n";
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("usage: sic-test-exact-group-metadata CASE");
    active = &select_case(argv[1]);
    flint_set_num_threads(1);
    check();
    flint_cleanup();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
