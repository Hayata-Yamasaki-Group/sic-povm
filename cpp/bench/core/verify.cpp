// Numerical verifier timing and memory driver; not a SIC certification run.
#include "sic/core/verify.hpp"

#include <charconv>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string_view>

static int number(const char* text) {
  const std::string_view value(text);
  int result = 0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
  if (error != std::errc() || end != value.data() + value.size())
    throw std::invalid_argument("invalid integer");
  return result;
}

int main(int argc, char** argv) {
  try {
    if (argc > 3) throw std::invalid_argument("usage: bench-verify [d=2048] [repeats=3]");
    const int d = argc > 1 ? number(argv[1]) : 2048;
    const int repeats = argc > 2 ? number(argv[2]) : 3;
    if (d < 2 || d > 2048 || repeats < 1 || repeats > 20)
      throw std::invalid_argument("supported d=2..2048, repeats=1..20");
    std::mt19937_64 rng(1);
    std::normal_distribution<double> normal;
    sic::core::Vector input(static_cast<std::size_t>(d));
    for (auto& z : input) z = sic::core::Complex(normal(rng), normal(rng));
    std::cout << "d,repeat,seconds,input_norm,max_overlap_error,worst_p1,worst_p2\n";
    std::cout << std::setprecision(17);
    for (int repeat = 0; repeat < repeats; ++repeat) {
      const auto begin = std::chrono::steady_clock::now();
      const auto result = sic::core::verify_fiducial(input);
      const double seconds =
          std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
      std::cout << d << ',' << repeat << ',' << seconds << ',' << result.input_norm << ','
                << result.max_overlap_error << ',' << result.worst_p1 << ',' << result.worst_p2
                << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "benchmark refused: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
