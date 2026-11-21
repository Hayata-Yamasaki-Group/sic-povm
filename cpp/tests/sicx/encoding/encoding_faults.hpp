// SPDX-License-Identifier: AGPL-3.0-or-later
// Linker-only controls at the actual encoder boundary; production has no test seam.
#pragma once
#include <atomic>
#include <csignal>
#include <thread>

namespace encoding_faults {
inline unsigned mode = 0;
inline std::atomic<bool> high_finished{false}, low_entered{false}, stop_hit{false};
inline std::atomic<bool> stop_interrupted_encode{false};
inline thread_local bool high_failed = false;
inline thread_local bool in_encoding = false;
inline thread_local unsigned stop_exports = 0;
inline std::atomic<unsigned> encoding_entries{0};
struct LowFailure : sic::Error {
  LowFailure() : sic::Error(sic::ErrorKind::integrity, "lowest injected block") {}
};
}  // namespace encoding_faults

extern "C" {
int __real_flint_set_num_workers(int);
int __wrap_flint_set_num_workers(int count) {
  const auto result = __real_flint_set_num_workers(count);
  encoding_faults::in_encoding = true;
  if (encoding_faults::mode == 4) encoding_faults::encoding_entries.fetch_add(1);
  return result;
}
void __real_flint_reset_num_workers(int);
void __wrap_flint_reset_num_workers(int count) {
  __real_flint_reset_num_workers(count);
  if (encoding_faults::stop_exports && encoding_faults::stop_exports < 2049)
    encoding_faults::stop_interrupted_encode.store(true);
  encoding_faults::in_encoding = false;
  if (encoding_faults::high_failed) encoding_faults::high_finished.store(true);
}
void __real_fmpz_get_ui_array(ulong*, slong, const fmpz*);
void __wrap_fmpz_get_ui_array(ulong* out, slong count, const fmpz* x) {
  using namespace encoding_faults;
  if (in_encoding && mode == 1 && fmpz_equal_ui(x, 101)) {
    low_entered.store(true);
    while (!high_finished.load()) std::this_thread::yield();
    throw LowFailure{};
  }
  if (in_encoding && mode == 1 && fmpz_equal_ui(x, 103)) {
    while (!low_entered.load()) std::this_thread::yield();
    high_failed = true;
    throw std::logic_error("higher injected block");
  }
  if (in_encoding && mode == 2 && fmpz_equal_ui(x, 102)) {
    ++stop_exports;
    if (!stop_hit.exchange(true)) sic::support::stop_token.request_stop();
  }
  if (in_encoding && mode == 3 && fmpz_equal_ui(x, 106)) ::kill(::getpid(), SIGKILL);
  if (in_encoding && mode == 4) {
    // Seven distinct workers plus the owner must enter the admitted eight-slot batch.
    while (encoding_entries.load() < 8) std::this_thread::yield();
    ::kill(::getpid(), SIGKILL);
  }
  __real_fmpz_get_ui_array(out, count, x);
}
}

namespace encoding_faults {
inline sic::sicx::Meta meta() {
  sic::sicx::Meta result;
  result.d = 3;
  result.D0 = 1;
  result.degrees.assign(17, 2);
  for (auto degree : result.degrees) result.width.push_back(result.width.back() * degree);
  result.D = result.width.back();
  result.smith = {result.D};
  return result;
}
inline void source(const sic::sicx::LeafVisitor& visit) {
  sic::sicx::FmpzPair value;
  const uint64_t block = uint64_t{1} << sic::sicx::kLogicalLeafLog2Limit;
  for (uint64_t b = 0; b < 8; ++b) {
    fmpz_set_ui(value.x, 101 + b);
    // Enough get() calls for stop polling inside an active encoder.
    for (uint64_t i = 0; i < 2048; ++i) visit(b * block + i, value.x, value.y);
    visit((b + 1) * block - 1, value.x, value.y);
  }
}
inline void controls(const std::filesystem::path& work) {
  for (unsigned control : {1, 2}) {
    sic::support::ScopedStopFile stop({});
    const auto out = work / ("encode-failure-" + std::to_string(control));
    sic::sicx::ProducerWriter writer(out, meta(), {}, sic::sicx::kPhysicalChunkTargetBytes, {}, 4);
    const auto spool = out.string() + ".spool.partial";
    const auto before = sic::support::sha256::file_hex(spool);
    const auto journal = sic::support::sha256::file_hex(spool + ".journal");
    mode = control;
    bool caught = false;
    try {
      writer.add({"rep", 17, -1, -1, 0}, source);
    } catch (const LowFailure& error) {
      caught = control == 1 && high_finished.load() && error.kind() == sic::ErrorKind::integrity &&
               std::string(error.what()) == "lowest injected block";
    } catch (const sic::Error& error) {
      caught = control == 2 && stop_hit.load() && stop_interrupted_encode.load() &&
               error.kind() == sic::ErrorKind::cancelled;
    }
    mode = 0;
    if (!caught || writer.stats().blocks || std::filesystem::exists(out) ||
        before != sic::support::sha256::file_hex(spool) ||
        journal != sic::support::sha256::file_hex(spool + ".journal"))
      throw std::runtime_error("parallel failure lost its original error or changed durable state");
  }
  std::cout << "PASS lowest failed block retains type/kind after higher failure; "
               "cancellation inside parallel encode commits nothing\n";
}
inline void part(const std::filesystem::path& out, const std::string& action) {
  sic::sicx::StreamOptions options;
  options.part = true;
  options.resume = action == "resume";
  sic::sicx::ProducerWriter writer(out, meta(), options, sic::sicx::kPhysicalChunkTargetBytes, {},
                                   4);
  if (options.resume && writer.writer().pending_replay() != 4)
    throw std::runtime_error("kill did not retain exactly the first four-block batch");
  mode = action == "kill" ? 3 : 0;
  writer.add({"rep", 17, -1, -1, 0}, source);
  if (writer.stats().peak_buffer_count != 4 || writer.stats().blocks != 8)
    throw std::runtime_error("kill/resume fixture did not use four-slot batches");
  writer.finish_part(out, "synthetic eight-block format fixture; not a SIC");
}
}  // namespace encoding_faults
