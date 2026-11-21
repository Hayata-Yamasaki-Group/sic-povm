// SPDX-License-Identifier: AGPL-3.0-or-later
#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "assertions.hpp"
#include "data/decoded_output.hpp"
#include "exact/arithmetic/qk.hpp"
#include "exact/transform/quadratic_transform.hpp"
#include "sic/error.hpp"
#include "sic/sicx/io.hpp"
#include "sic/support/flint_raii.hpp"
#include "support/sha256.hpp"
#include "temporary_directory.hpp"
#include "transport/parts.hpp"

namespace {
using Directory = sic_test::TemporaryDirectory;

void ceil_refusal() {
  sic::exact::QuadraticContext context{5};
  sic::exact::QuadraticNumber x(sic::exact::Rational(0), sic::exact::Rational(1));
  SIC_CHECK(sic::exact::ceil1(context, x) == 3);
  SIC_CHECK(::setenv("SIC_TEST_CEIL1_FORCE_PRECISION_REFUSAL", "1", 1) == 0);
  bool refused = false;
  try {
    (void)sic::exact::ceil1(context, x);
  } catch (const std::runtime_error& e) {
    refused = std::string(e.what()) == "ceil1: precision";
  }
  SIC_CHECK(::unsetenv("SIC_TEST_CEIL1_FORCE_PRECISION_REFUSAL") == 0);
  SIC_CHECK(refused);
  SIC_CHECK(sic::exact::ceil1(context, x) == 3);
}

void fp64_refusal() {
  Directory work("sic-fp64-refusal");
  sic::support::ComplexBallVector values(1);
  acb_one(values[0]);
  // Populate a heap-backed midpoint in b before either rounding refusal.
  sic::sicx::BallPart wide;
  wide.mantissa.assign(8, UINT64_MAX);
  wide.mid_exp = -511;
  wide.rad_man = 1;
  wide.rad_exp = 0;
  sic::sicx::BallPart zero;
  zero.mantissa.assign(8, 0);
  for (int decoded = 0; decoded < 2; ++decoded) {
    if (decoded) arb_add_error_2exp_si(acb_realref(values[0]), 0);
    bool refused = false;
    try {
      (void)sic::data::write_decoded_fp64(work.path / std::to_string(decoded), values, {wide, zero},
                                          1);
    } catch (const sic::Error& e) {
      const std::string coordinate = decoded ? "decoded" : "BALL";
      refused =
          e.kind() == sic::ErrorKind::precision &&
          std::string(e.what()) == "SICX: ball is too wide to certify the binary64 rounding of " +
                                       coordinate + " coordinate 0";
    }
    SIC_CHECK(refused);
  }
}

size_t descriptors() {
  size_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
    (void)entry;
    ++count;
  }
  return count;
}

void destination_refusal() {
  Directory work("sic-parts-write-refusal");
  const auto source = work.path / "source";
  const auto temp = work.path / "temp";
  std::filesystem::create_directory(source);
  std::filesystem::create_directory(temp);
  sic::transport::Parts parts;
  sic::transport::Part part;
  part.asset = "d4-m1.sicx.part-0000";
  part.size = 128;
  const auto input = source / part.asset;
  {
    std::ofstream out(input, std::ios::binary);
    out << std::string(part.size, 'x');
  }
  part.sha256 = sic::support::sha256::file_hex(input);
  parts.parts.push_back(part);
  const int fd = ::open(input.c_str(), O_RDONLY | O_CLOEXEC);
  SIC_CHECK(fd >= 0);
  const size_t before = descriptors();
  for (unsigned jobs : {1u, 3u}) {
    bool refused = false;
    try {
      (void)sic::transport::sync_parts(fd, parts, source.string(), jobs, false, temp);
    } catch (const sic::Error& e) {
      refused = e.kind() == sic::ErrorKind::io &&
                std::string(e.what()) == "write failed: Bad file descriptor";
    }
    SIC_CHECK(refused);
    SIC_CHECK(descriptors() == before);
    SIC_CHECK(std::filesystem::is_empty(temp));
    SIC_CHECK(::fcntl(fd, F_GETFD) >= 0);  // The destination remains caller-owned.
  }
  SIC_CHECK(::close(fd) == 0);
}

void stream_refusal() {
  Directory work("sic-stream-refusal");
  const auto output = work.path / "out.sicx";
  { sic::sicx::StreamWriter writer(output); }
  sic::sicx::StreamOptions options;
  options.resume = true;
  options.plan[0] = 1;
  const size_t before = descriptors();
  bool refused = false;
  try {
    sic::sicx::StreamWriter writer(output, options);
  } catch (const sic::Error& e) {
    refused = e.kind() == sic::ErrorKind::integrity &&
              std::string(e.what()).find("plan digest differs") != std::string::npos;
  }
  SIC_CHECK(refused);
  SIC_CHECK(descriptors() == before);

  options.resume = false;
  options.part = true;
  const auto part = work.path / "part";
  sic::sicx::StreamWriter writer(part, options);
  std::filesystem::create_symlink("/dev/full", part.string() + ".idx.partial");
  const size_t with_writer = descriptors();
  refused = false;
  try {
    (void)writer.finish();
  } catch (const sic::Error& e) {
    refused = std::string(e.what()).find("write") != std::string::npos;
  }
  SIC_CHECK(refused);
  SIC_CHECK(descriptors() == with_writer);
}

void transform_refusal() {
  for (int mode = 0; mode < 3; ++mode) {
    sic::support::RealBallVector values(4);
    sic::support::ComplexBallVector complex(4);
    sic::support::flint::Arb factor;
    arb_const_pi(factor, 512);
    for (int i = 0; i < 4; ++i) {
      arb_mul_ui(values[i], factor, i + 1, 512);
      acb_set_arb(complex[i], values[i]);
    }
    int calls = 0;
    auto generator = [&](int, long) -> arb_srcptr {
      if (++calls == 2) throw std::runtime_error("generator refusal");
      return factor.get();
    };
    bool refused = false;
    try {
      if (mode == 0)
        sic::exact::quadratic_real_forward(values.v, 2, 512, 1, false, generator);
      else if (mode == 1)
        sic::exact::quadratic_real_forward_windowed(values.v, 2, 512, 1, 2, generator,
                                                    [](int, long, long, bool) {});
      else
        sic::exact::quadratic_complex_inverse(
            complex.v, 2, 512, 1,
            [&](acb_ptr out, int i, long c) { acb_set_arb(out, generator(i, c)); });
    } catch (const std::runtime_error& e) {
      refused = std::string(e.what()) == "generator refusal";
    }
    SIC_CHECK(refused && calls == 2);
  }
}
}  // namespace

int main(int argc, char** argv) {
  sic::support::flint::WorkerCleanup cleanup;
  SIC_CHECK(argc == 2);
  if (argc == 2) {
    const std::string test = argv[1];
    if (test == "ceil")
      ceil_refusal();
    else if (test == "fp64")
      fp64_refusal();
    else if (test == "parts")
      destination_refusal();
    else if (test == "stream")
      stream_refusal();
    else if (test == "transform")
      transform_refusal();
    else
      SIC_CHECK(false);
  }
  return sic_test::report();
}
