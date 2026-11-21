// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/group/group_checkpoint.hpp"

#include <fstream>

#include "assertions.hpp"
#include "support/resources.hpp"
#include "temporary_directory.hpp"

int main() {
  sic_test::TemporaryDirectory scratch("sic-group-cache");
  const auto directory = scratch.path;
  const auto path = directory / "group.bin";
  sic::exact::Arithmetic ar(3);
  sic::exact::Group before(ar);
  before.generate_standard();
  before.close(
      sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")),
      (long)ar.h() * ar.d * ar.d / 2);
  sic::exact::GroupCheckpoint::save(before, path);
  sic::exact::Group after(ar);
  bool admitted = false;
  sic::exact::GroupCheckpoint::load(after, path, before.N,
                                    [&](size_t bases) { admitted = bases == before.nb(); });
  SIC_CHECK(admitted);
  SIC_CHECK(after.N == before.N);
  SIC_CHECK(after.invf == before.invf);
  SIC_CHECK(after.stride == before.stride);
  SIC_CHECK(after.base == before.base);
  SIC_CHECK(after.nn == before.nn);
  SIC_CHECK(after.chi == before.chi);
  SIC_CHECK(after.img == before.img);
  SIC_CHECK(after.rep_elem == before.rep_elem);
  SIC_CHECK(after.rep_base == before.rep_base);
  bool rejected = false;
  try {
    sic::exact::GroupCheckpoint::save(before, path);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  SIC_CHECK(rejected);
  sic::exact::Arithmetic other_ar(4);
  sic::exact::Group other(other_ar);
  rejected = false;
  try {
    sic::exact::GroupCheckpoint::load(other, path, before.N, [](size_t) {});
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  SIC_CHECK(rejected);
  {
    std::fstream corrupt(path, std::ios::in | std::ios::out | std::ios::binary);
    corrupt.seekp(0);
    corrupt.put('X');
  }
  rejected = false;
  try {
    sic::exact::GroupCheckpoint::load(after, path, before.N, [](size_t) {});
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  SIC_CHECK(rejected);
  // A recomputed digest must not hide a malformed schema or dimensions.
  {
    std::ofstream digest(path.string() + ".sha256");
    digest << sic::exact::checkpoint_digest(path) << '\n';
  }
  rejected = false;
  try {
    sic::exact::GroupCheckpoint::load(after, path, before.N, [](size_t) {});
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  SIC_CHECK(rejected);
  {
    std::fstream corrupt(path, std::ios::in | std::ios::out | std::ios::binary);
    const std::uint64_t magic = 0x53494347524f5550ULL;
    corrupt.seekp(0);
    corrupt.write(reinterpret_cast<const char*>(&magic), sizeof magic);
    corrupt.seekp(15 * sizeof(std::uint64_t));
    corrupt.put('Z');
  }
  {
    std::ofstream digest(path.string() + ".sha256");
    digest << sic::exact::checkpoint_digest(path) << '\n';
  }
  rejected = false;
  try {
    sic::exact::GroupCheckpoint::load(after, path, before.N, [](size_t) {});
  } catch (const std::runtime_error& error) {
    rejected = std::string(error.what()).find("source mismatch") != std::string::npos;
  }
  SIC_CHECK(rejected);
  {
    std::fstream restore(path, std::ios::in | std::ios::out | std::ios::binary);
    restore.seekp(15 * sizeof(std::uint64_t));
    restore.put(sic::exact::GroupCheckpoint::source_id()[0]);
  }
  std::filesystem::resize_file(path, std::filesystem::file_size(path) - 4);
  {
    std::ofstream digest(path.string() + ".sha256");
    digest << sic::exact::checkpoint_digest(path) << '\n';
  }
  admitted = false;
  rejected = false;
  try {
    sic::exact::GroupCheckpoint::load(after, path, before.N, [&](size_t) { admitted = true; });
  } catch (const std::runtime_error& error) {
    rejected = std::string(error.what()).find("length mismatch") != std::string::npos;
  }
  SIC_CHECK(rejected);
  SIC_CHECK(!admitted);
  return sic_test::report();
}
