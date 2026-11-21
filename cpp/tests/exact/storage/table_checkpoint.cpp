// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/storage/table_checkpoint.hpp"

#include "assertions.hpp"
#include "support/resources.hpp"
#include "temporary_directory.hpp"

int main() {
  sic_test::TemporaryDirectory scratch("sic-table-cache");
  const auto root = scratch.path;
  sic::exact::Arithmetic ar(3);
  sic::exact::Group group(ar);
  group.generate_standard();
  group.close(
      sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")),
      (long)ar.h() * ar.d * ar.d / 2);
  auto layout = std::make_shared<sic::exact::OrbitTableLayout>(ar.d, ar.h(), group.orbit);
  sic::exact::Table before(ar, 80, 2);
  before.enable_compact(layout);
  before.build_batched();
  before.release_states();
  const auto path = sic::exact::TableCheckpoint::directory(root, ar, 80);
  const std::vector<char> mask(ar.h(), 0);
  sic::exact::TableCheckpoint::save(before, group, -1, mask, path);
  long cc = -2;
  std::vector<char> restored_mask;
  bool admitted = false;
  auto after = sic::exact::TableCheckpoint::load(
      ar, group, layout, 80, 2, path, cc, restored_mask,
      [&](size_t physical) { admitted = physical == (size_t)ar.h(); });
  SIC_CHECK(admitted);
  SIC_CHECK(cc == -1);
  SIC_CHECK(restored_mask == mask);
  SIC_CHECK(after->cached_enclosures);
  for (int c = 0; c < ar.h(); ++c)
    for (size_t q = 0; q < before.packed[c].size(); ++q)
      SIC_CHECK(acb_equal(before.packed[c][q], after->packed[c][q]));
  admitted = false;
  bool rejected = false;
  try {
    sic::exact::TableCheckpoint::load(ar, group, layout, 81, 2, path, cc, restored_mask,
                                      [&](size_t) { admitted = true; });
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  SIC_CHECK(rejected && !admitted);
  rejected = false;
  try {
    sic::exact::TableCheckpoint::save(before, group, -1, mask, path);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  SIC_CHECK(rejected);
  {
    const auto manifest = path / "manifest", checksum = path / "manifest.sha256";
    std::ifstream in(manifest);
    const std::string valid((std::istreambuf_iterator<char>(in)), {});
    for (const auto* header :
         {"SIC_ANALYTIC_CACHE 1", "sic-analytic-cache/2.0"}) {  // identity-negative
      {
        std::ofstream out(manifest);
        out << header << valid.substr(valid.find('\n'));
      }
      {
        std::ofstream out(checksum);
        out << sic::exact::checkpoint_digest(manifest) << '\n';
      }
      rejected = false;
      try {
        sic::exact::TableCheckpoint::load(ar, group, layout, 80, 2, path, cc, restored_mask,
                                          [](size_t) {});
      } catch (const sic::Error& error) {
        rejected = error.kind() == sic::ErrorKind::invalid_format;
      }
      SIC_CHECK(rejected);
    }
    {
      std::ofstream out(manifest);
      out << valid;
    }
    {
      std::ofstream out(checksum);
      out << sic::exact::checkpoint_digest(manifest) << '\n';
    }
  }
  {
    std::fstream corrupt(path / "class-0.balls", std::ios::in | std::ios::out | std::ios::binary);
    corrupt.seekp(0);
    corrupt.put('X');
  }
  rejected = false;
  try {
    sic::exact::TableCheckpoint::load(ar, group, layout, 80, 2, path, cc, restored_mask,
                                      [](size_t) {});
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  SIC_CHECK(rejected);
  return sic_test::report();
}
