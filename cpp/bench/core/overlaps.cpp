// Benchmark: time to compute all d^2 overlaps of a random vector.
// Output: CSV "d,seconds" on stdout. Usage: bench-overlaps [d_max=256]

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>

#include "sic/core/weyl_heisenberg.hpp"

int main(int argc, char** argv) {
  const int d_max = (argc > 1) ? std::atoi(argv[1]) : 256;
  std::mt19937_64 rng(1);
  std::normal_distribution<double> g;
  std::cout << "d,seconds\n";
  for (int d = 8; d <= d_max; d *= 2) {
    sic::core::Vector v(d);
    for (auto& z : v) z = sic::core::Complex(g(rng), g(rng));
    const sic::core::WeylHeisenberg wh(d);
    const auto t0 = std::chrono::steady_clock::now();
    const auto ov = wh.overlaps(v);
    const auto t1 = std::chrono::steady_clock::now();
    volatile double sink = std::abs(ov[1]);
    (void)sink;
    std::cout << d << "," << std::chrono::duration<double>(t1 - t0).count() << "\n";
  }
  return 0;
}
