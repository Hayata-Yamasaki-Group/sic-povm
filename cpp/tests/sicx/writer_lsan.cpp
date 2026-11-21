// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/block.hpp"
#include "support/resources/checked_bytes.hpp"
#include "support/sha256.hpp"
// Exercise GMP-sized block allocations on success and after a source failure.
// Run with ASAN_OPTIONS=detect_leaks=1; no allocator or FLINT suppression is used.
#include <iostream>
#include <numeric>

#include "sicx/container/producer_writer.hpp"

namespace {
std::string run(const std::filesystem::path& work, unsigned mode) {
  sic::sicx::Meta meta;
  meta.d = 2;
  meta.D0 = 5;
  meta.degrees.assign(16, 2);
  for (auto p : meta.degrees) meta.width.push_back(meta.width.back() * p);
  meta.D = meta.width.back();
  meta.smith = {meta.D};
  meta.unit_generator_words = {};
  sic::sicx::FmpzPair pair;
  fmpz_one(pair.x);
  fmpz_mul_2exp(pair.x, pair.x, 511);
  fmpz_add_ui(pair.x, pair.x, 1);
  fmpz_set_ui(pair.y, 7);
  unsigned pass = 0;
  sic::sicx::StreamOptions options;
  options.durable = false;
  sic::sicx::ProducerWriter producer(work / ("case-" + std::to_string(mode)), meta, options,
                                     sic::sicx::kPhysicalChunkTargetBytes, {}, 4);
  sic::support::ScopedStopFile stop({});
  bool refused = false;
  try {
    producer.add({"rep", 16, -1, -1, 0}, [&](const sic::sicx::LeafVisitor& visit) {
      ++pass;
      for (uint64_t pos = 0; pos < (mode == 3 ? 8 : meta.D); ++pos) {
        if (mode == 1 && pass == 2 && pos == 3 * (uint64_t(1) << sic::sicx::kLogicalLeafLog2Limit))
          throw std::runtime_error("injected source failure");
        if (mode == 2 && pass == 2 && pos == 3 * (uint64_t(1) << sic::sicx::kLogicalLeafLog2Limit))
          sic::support::stop_token.request_stop();
        if (mode == 2 && pass == 2 &&
            pos == 3 * (uint64_t(1) << sic::sicx::kLogicalLeafLog2Limit) + 1)
          throw std::runtime_error("writer ignored block stop boundary");
        visit(pos, pair.x, pair.y);
      }
    });
  } catch (const sic::support::resources::AdmissionError&) {
    if (pass != 1) throw std::runtime_error("writer admission ran after the second source pass");
    throw;
  } catch (const std::runtime_error& e) {
    if (mode == 0 || mode == 3 ||
        (mode == 1 && std::string(e.what()) != "injected source failure") ||
        (mode == 2 && std::string(e.what()).find("stop token") == std::string::npos))
      throw;
    refused = true;
  }
  if (refused != (mode == 1 || mode == 2) ||
      ((mode == 0 || mode == 3) && producer.stats().blocks != (mode == 3 ? 1 : 4)))
    throw std::runtime_error("writer block path was not exercised");
  return mode == 0 ? sic::support::sha256::file_hex(work / "case-0.spool.partial") : std::string{};
}
}  // namespace
int main(int argc, char** argv) {
  int status = 0;
  try {
    if (argc != 2) return 2;
    const std::filesystem::path work = argv[1];
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);
    run(work, 0);
    run(work, 1);
    run(work, 2);
    run(work, 3);
    // The retained-buffer grant must refuse before a source's second pass.
    setenv("SIC_MEMORY_MB", "1", 1);
    bool refused = false;
    try {
      std::filesystem::create_directories(work / "refusal");
      run(work / "refusal", 0);
    } catch (const sic::support::resources::AdmissionError&) {
      refused = true;
    }
    unsetenv("SIC_MEMORY_MB");
    if (!refused) throw std::runtime_error("writer memory admission did not refuse");
    std::filesystem::remove_all(work);
    std::cout << "PASS parallel writer success, source failure and per-block cancellation; "
                 "buffer admission refusal precedes the second source pass\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    status = 1;
  }
  flint_cleanup_master();
  return status;
}
