// SPDX-License-Identifier: AGPL-3.0-or-later
#include <fstream>
#include <iostream>

#include "sicx/container/producer_writer.hpp"
#include "sicx/container/rescaling_input.hpp"
#include "support/sha256.hpp"

#ifdef SIC_TEST_WRAP_IO
#include "sicx/encoding/encoding_faults.hpp"
#include "support/io_faults.hpp"
#endif

namespace fs = std::filesystem;
using namespace sic::sicx;
static void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

static uint64_t copy(const Reader& input, const fs::path& out, unsigned workers, uint64_t buffers,
                     const std::string& action = {}) {
  StreamOptions options;
  options.durable = !action.empty();
  options.resume = action == "resume";
  ProducerWriter writer(out, input.meta(), options, kPhysicalChunkTargetBytes, {}, workers,
                        buffers);
  for (size_t i = 0; i < input.meta().elements.size(); ++i) {
    const auto& e = input.meta().elements[i];
    const ElementId id{e.kind, e.level, e.axis, e.k, e.position};
    if (options.resume && writer.done(id)) continue;
    writer.add(id, stored_element_source(input, i), [&](const ElementInfo& info) {
#ifdef SIC_TEST_WRAP_IO
      if (action == "kill" && info.blocks() >= 8) encoding_faults::mode = 4;
#endif
    });
  }
  writer.write_meta();
  for (size_t i = 0; i < input.index().size(); ++i) {
    const auto& e = input.index()[i];
    const auto type = e.type_name();
    if (type != "EXMT" && type != "TOWR" && type != "BODY")
      writer.writer().chunk(type, e.flags, input.chunk(i), e.object, e.first, e.count);
  }
  writer.writer().finish();
  return writer.stats().peak_buffer_count;
}

static void parallel_identity(const fs::path& work) {
  Meta meta;
  meta.d = 3;
  meta.D0 = -3;
  meta.degrees.assign(18, 2);
  meta.degrees.back() = 3;
  for (auto degree : meta.degrees) meta.width.push_back(meta.width.back() * degree);
  meta.D = meta.width.back();
  meta.smith = {meta.D};
  std::string expected;
  // 24 blocks: a full 16-slot batch and an eight-slot tail. Signed GMP limbs,
  // sparse runs and empty blocks exercise independent encoder/decoder scratch.
  for (unsigned workers : {1, 2, 16})
    for (uint64_t buffers : {uint64_t{1}, uint64_t{7}, uint64_t{16}}) {
      const auto out =
          work / ("synthetic-" + std::to_string(workers) + "-" + std::to_string(buffers));
      StreamOptions options;
      options.durable = false;
      ProducerWriter writer(out, meta, options, kPhysicalChunkTargetBytes, {}, workers, buffers);
      writer.add({"rep", 18, -1, -1, 0}, [&](const LeafVisitor& visit) {
        FmpzPair value;
        auto emit = [&](uint64_t pos) {
          fmpz_one(value.x);
          fmpz_mul_2exp(value.x, value.x, 1023);
          fmpz_add_ui(value.x, value.x, 2 * pos + 1);
          fmpz_one(value.y);
          fmpz_mul_2exp(value.y, value.y, 767);
          fmpz_add_ui(value.y, value.y, pos);
          if (pos & 1) fmpz_neg(value.x, value.x);
          if (pos & 2) fmpz_neg(value.y, value.y);
          visit(pos, value.x, value.y);
        };
        for (uint64_t pos = 0; pos + 1 < meta.D; pos += 67)
          if (pos / (uint64_t{1} << kLogicalLeafLog2Limit) != 9) emit(pos);
        emit(meta.D - 1);
      });
      require(writer.stats().blocks == 24, "synthetic parallel test did not cover 24 blocks");
      require(writer.stats().peak_buffer_count == std::min<uint64_t>(workers, buffers),
              "synthetic test did not obtain the intended parallel buffer allowance");
      require(flint_get_num_threads() == 4,
              "encoding did not restore the caller's FLINT allowance");
      const auto sha = sic::support::sha256::file_hex(out.string() + ".spool.partial");
      if (expected.empty()) expected = sha;
      require(sha == expected, "successful parallel encoding changed spool bytes");
    }
  std::cout << "PASS synthetic 24-block GMP success, empty block, partial batch SHA=" << expected
            << '\n';
}

// Synthetic multi-block controls are format tests, not SIC computations.
static void failures(const fs::path& work) {
  Meta meta;
  meta.d = 3;
  meta.D0 = 1;
  meta.degrees.assign(16, 2);
  for (auto degree : meta.degrees) meta.width.push_back(meta.width.back() * degree);
  meta.D = meta.width.back();
  meta.smith = {meta.D};
  const auto path = work / "worker-failure";
  StreamOptions options;
  ProducerWriter writer(path, meta, options, kPhysicalChunkTargetBytes, {}, 4);
  FmpzPair pair;
  fmpz_one(pair.x);
  writer.add({"rep", 0, -1, -1, 0}, [&](const LeafVisitor& visit) { visit(0, pair.x, pair.y); });
  const auto spool = fs::path(path.string() + ".spool.partial");
  const auto journal = fs::path(spool.string() + ".journal");
  const auto before = sic::support::sha256::file_hex(spool);
  const auto note = sic::support::sha256::file_hex(journal);
  bool refused = false;
  try {
    writer.add({"rep", 16, -1, -1, 1}, [&](const LeafVisitor& visit) {
      for (uint64_t pos :
           {uint64_t{0}, uint64_t{1} << 14, uint64_t{1} << 15, (uint64_t{1} << 16) - 1}) {
        fmpz_set_ui(pair.y, pos == (uint64_t{1} << 15));
        visit(pos, pair.x, pair.y);
      }
    });
  } catch (const sic::Error& error) {
    refused = error.kind() == sic::ErrorKind::invalid_format &&
              std::string(error.what()).find("rational-field") != std::string::npos;
  }
  require(refused, "middle worker failure lost its error category");
  require(before == sic::support::sha256::file_hex(spool) &&
              note == sic::support::sha256::file_hex(journal) && !fs::exists(path),
          "failed batch changed durable state or published output");
#ifdef SIC_TEST_WRAP_IO
  const auto io_path = work / "io-failure";
  size_t committed = 0;
  {
    ProducerWriter io(io_path, meta, options, kPhysicalChunkTargetBytes, {}, 4);
    faults::reset();
    // Each chunk synchronizes data then journal; fail the middle block's data sync.
    faults::fdatasync.at = 5;
    refused = false;
    try {
      io.add({"rep", 16, -1, -1, 0}, [&](const LeafVisitor& visit) {
        fmpz_zero(pair.y);
        for (uint64_t pos :
             {uint64_t{0}, uint64_t{1} << 14, uint64_t{1} << 15, (uint64_t{1} << 16) - 1})
          visit(pos, pair.x, pair.y);
      });
    } catch (const sic::Error& error) {
      refused = error.kind() == sic::ErrorKind::io;
    }
    faults::reset();
    committed = io.writer().index().size();
    require(refused && committed == 2 && !fs::exists(io_path), "middle I/O fault was published");
  }
  options.resume = true;
  ProducerWriter recovered(io_path, meta, options, kPhysicalChunkTargetBytes, {}, 2);
  require(recovered.writer().pending_replay() == committed, "I/O recovery lost durable prefix");
  recovered.add({"rep", 16, -1, -1, 0}, [&](const LeafVisitor& visit) {
    for (uint64_t pos :
         {uint64_t{0}, uint64_t{1} << 14, uint64_t{1} << 15, (uint64_t{1} << 16) - 1})
      visit(pos, pair.x, pair.y);
  });
  require(recovered.writer().index().size() == 4, "middle I/O fault did not replay in order");
  ProducerWriter uninterrupted(work / "io-reference", meta, {}, kPhysicalChunkTargetBytes, {}, 4);
  uninterrupted.add({"rep", 16, -1, -1, 0}, [&](const LeafVisitor& visit) {
    for (uint64_t pos :
         {uint64_t{0}, uint64_t{1} << 14, uint64_t{1} << 15, (uint64_t{1} << 16) - 1})
      visit(pos, pair.x, pair.y);
  });
  require(sic::support::sha256::file_hex(io_path.string() + ".spool.partial") ==
              sic::support::sha256::file_hex(work / "io-reference.spool.partial"),
          "I/O fault replay changed bytes against uninterrupted encoding");
#endif
}

int main(int argc, char** argv) {
  int status = 0;
  try {
#ifdef SIC_TEST_WRAP_IO
    if (argc == 5 && std::string(argv[1]) == "--copy-resume") {
      Reader input(argv[2]);
      require(copy(input, argv[3], 8, 8, argv[4]) == 8,
              "d128 kill/resume did not obtain eight-slot batches");
      flint_cleanup_master();
      return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--synthetic-part") {
      encoding_faults::part(argv[2], argv[3]);
      flint_cleanup_master();
      return 0;
    }
#endif
    if (argc == 4 && std::string(argv[1]) == "--charge") {
      Reader input(argv[2]);
      const auto workers = std::stoull(argv[3]);
      require(workers > 0, "charge inspection requires positive workers");
      uint64_t retained = 0, extra = 0;
      std::vector<uint64_t> workspaces;
      for (const auto& e : input.meta().elements) {
        const auto workspace = ProducerWriter::writer_workspace(e);
        workspaces.push_back(workspace);
        const auto buffers = std::min<uint64_t>(workers, e.blocks());
        retained = std::max(retained, sic::support::admission::mul(buffers, workspace));
        extra = std::max(extra, sic::support::admission::mul(buffers - 1, workspace));
      }
      std::cout << "{\"retainedBytes\":" << retained << ",\"extraBytes\":" << extra
                << ",\"workspaces\":[";
      for (size_t i = 0; i < workspaces.size(); ++i) std::cout << (i ? "," : "") << workspaces[i];
      std::cout << "]}\n";
      flint_cleanup_master();
      return 0;
    }
    if (argc != 3) return 2;
    flint_set_num_threads(4);
    const fs::path work(argv[2]);
    fs::remove_all(work);
    fs::create_directories(work);
    Reader input(argv[1]);
    const auto expected = sic::support::sha256::file_hex(argv[1]);
    for (unsigned workers : {1, 2, 16})
      for (uint64_t buffers : {uint64_t{1}, uint64_t{16}}) {
        const auto out = work / (std::to_string(workers) + "-" + std::to_string(buffers) + ".sicx");
        const auto peak_buffers = copy(input, out, workers, buffers);
        std::cout << "workers=" << workers << " ceiling=" << buffers
                  << " peak_buffers=" << peak_buffers << '\n';
        require(sic::support::sha256::file_hex(out) == expected, "whole-file bytes changed");
        Reader output(out);
        require(output.logical().size() == input.logical().size(), "block count changed");
        for (size_t i = 0; i < input.logical().size(); ++i)
          require(output.leaf(i) == input.leaf(i), "logical block bytes changed");
        fs::remove(out);
      }
    parallel_identity(work);
    failures(work);
#ifdef SIC_TEST_WRAP_IO
    encoding_faults::controls(work);
#endif
    require(flint_get_num_threads() == 4, "worker failure did not restore the FLINT allowance");
    std::cout << "PASS workers=1,2,16 buffers=1,16 whole-file/PROV/every-block SHA=" << expected
              << "; worker failure and durable I/O fault recovery\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    status = 1;
  }
  flint_cleanup_master();
  return status;
}
