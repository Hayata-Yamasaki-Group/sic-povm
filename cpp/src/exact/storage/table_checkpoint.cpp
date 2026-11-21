// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/storage/table_checkpoint.hpp"

#include <sstream>

#include "sic/error.hpp"

namespace sic::exact {
namespace {
constexpr const char* kAnalyticCacheIdentity = "sic-analytic-cache/1.0";
}
std::string TableCheckpoint::source_id() {
#ifdef SIC_TABLE_SOURCE_ID
  return SIC_TABLE_SOURCE_ID;
#else
  throw std::runtime_error("analytic checkpoints require the CMake source identity");
#endif
}

std::filesystem::path TableCheckpoint::directory(const std::filesystem::path& root,
                                                 const Arithmetic& ar, slong precision) {
  return root / ("s" + std::to_string(ar.s) + "-o" + std::to_string(ar.order_index) + "-p" +
                 std::to_string(precision) + "-t" + source_id().substr(0, 16) + "-g" +
                 GroupCheckpoint::source_id().substr(0, 16) + "-paired");
}

void TableCheckpoint::sync_file(const std::filesystem::path& path) {
  sic::support::sync_file(path);
}

void TableCheckpoint::save(const Table& table, const Group& group, long cc,
                           const std::vector<char>& derived,
                           const std::filesystem::path& path) try {
  if (!table.compact() || derived.size() != (size_t)table.ar.h())
    throw std::runtime_error("analytic checkpoint requires a complete compact table");
  if (std::filesystem::exists(path))
    throw sic::Error(sic::ErrorKind::io, "analytic checkpoint destination exists");
  auto partial = path;
  partial += ".partial";
  std::filesystem::create_directories(path.parent_path());
  if (!std::filesystem::create_directory(partial))
    throw sic::Error(sic::ErrorKind::io, "analytic checkpoint partial already exists");
  std::ostringstream manifest;
  manifest << kAnalyticCacheIdentity << '\n'
           << GroupCheckpoint::source_id() << '\n'
           << source_id() << '\n'
           << FLINT_VERSION << '\n'
           << table.ar.s << ' ' << table.ar.Delta << ' ' << table.ar.order_index << ' ' << group.N
           << ' ' << table.prec << ' ' << table.ar.h() << ' ' << table.layout->slots << ' ' << cc
           << ' ' << table.paired_values << ' ' << table.n_nodes_total << '\n';
  for (char value : derived) manifest << int(value) << ' ';
  manifest << '\n';
  for (sic::support::Index c = 0; c < table.ar.h(); ++c) {
    if (derived[c]) {
      if (table.packed[c].size())
        throw std::runtime_error("derived analytic class has unexpected owned values");
      manifest << "empty " << c << '\n';
    } else {
      if (table.packed[c].size() != (size_t)table.layout->slots)
        throw std::runtime_error("incomplete analytic class cannot be checkpointed");
      auto backing = BallFile::save(table.packed[c]);
      backing->publish(partial / ("class-" + std::to_string(c) + ".balls"));
      manifest << "data " << c << ' ' << backing->bytes() << ' ' << backing->digest() << '\n';
    }
  }
  manifest << "end\n";
  sic::support::atomic_write(partial / "manifest", manifest.str(), false, 0666);
  // Checksum atomic_write directory fsync durably publishes BallFile payloads; no staging writes
  // may follow.
  sic::support::atomic_write(partial / "manifest.sha256",
                             checkpoint_digest(partial / "manifest") + '\n', false, 0666);
  sic::support::publish_replace(partial, path);
  sic::support::sync_parent(path);
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

std::unique_ptr<Table> TableCheckpoint::load(const Arithmetic& ar, const Group& group,
                                             const std::shared_ptr<OrbitTableLayout>& layout,
                                             slong precision, int threads,
                                             const std::filesystem::path& path, long& cc,
                                             std::vector<char>& derived,
                                             const std::function<void(size_t)>& admit) try {
  if (!layout || std::filesystem::file_size(path / "manifest") > sic::support::resources::kMib)
    throw sic::Error(sic::ErrorKind::invalid_format,
                     "invalid analytic checkpoint manifest size/layout");
  std::ifstream checksum(path / "manifest.sha256");
  if (!checksum) throw sic::Error(sic::ErrorKind::io, "cannot open analytic checkpoint checksum");
  checksum.exceptions(std::ios::badbit);
  std::string digest, extra;
  if (!(checksum >> digest) || (checksum >> extra) || digest.size() != 64 ||
      digest.find_first_not_of("0123456789abcdef") != std::string::npos)
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid analytic checkpoint checksum");
  if (checkpoint_digest(path / "manifest") != digest)
    throw sic::Error(sic::ErrorKind::integrity, "analytic checkpoint manifest checksum failed");
  std::ifstream manifest(path / "manifest");
  if (!manifest) throw sic::Error(sic::ErrorKind::io, "cannot open analytic checkpoint manifest");
  manifest.exceptions(std::ios::badbit);
  std::string tag, group_source, table_source, flint_version;
  if (!(manifest >> tag >> group_source >> table_source >> flint_version) ||
      tag != kAnalyticCacheIdentity)
    throw sic::Error(sic::ErrorKind::invalid_format, "analytic checkpoint schema mismatch");
  if (group_source != GroupCheckpoint::source_id() || table_source != source_id())
    throw sic::Error(sic::ErrorKind::integrity, "analytic checkpoint source mismatch");
  if (flint_version != FLINT_VERSION)
    throw sic::Error(sic::ErrorKind::unsupported,
                     "analytic checkpoint arithmetic version mismatch");
  long s, delta, order, count, stored_precision, classes, slots, saved_cc, paired, nodes;
  if (!(manifest >> s >> delta >> order >> count >> stored_precision >> classes >> slots >>
        saved_cc >> paired >> nodes) ||
      nodes < 0 || (paired != 0 && paired != 1) || saved_cc < -1)
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid analytic checkpoint metadata");
  if (s != ar.s || delta != ar.Delta || order != ar.order_index || count != group.N ||
      stored_precision != precision || classes != ar.h() || slots != layout->slots)
    throw sic::Error(sic::ErrorKind::integrity,
                     "analytic checkpoint arithmetic/precision mismatch");
  if (saved_cc >= group.N)
    throw sic::Error(sic::ErrorKind::invalid_format,
                     "invalid analytic checkpoint conjugation index");
  std::vector<char> mask(classes);
  for (char& value : mask) {
    int digit;
    if (!(manifest >> digit) || (digit != 0 && digit != 1))
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid analytic checkpoint derived mask");
    value = digit;
  }
  const size_t physical = std::count(mask.begin(), mask.end(), 0);
  if (!physical || (paired && saved_cc < 0) ||
      ((!paired || saved_cc < 0) && physical != (size_t)classes) ||
      (paired && (group.mul(saved_cc, saved_cc) != 0 || group.nn[saved_cc] != 2 * ar.d - 1 ||
                  group.chi[saved_cc] < 0)))
    throw sic::Error(sic::ErrorKind::invalid_format,
                     "invalid analytic checkpoint conjugation metadata");
  std::vector<std::shared_ptr<BallFile>> backing(classes);
  for (long c = 0; c < classes; ++c) {
    long index;
    if (!(manifest >> tag >> index) || index != c || tag != (mask[c] ? "empty" : "data"))
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid analytic checkpoint class records");
    if (!mask[c]) {
      sic::support::resources::Bytes bytes;
      if (!(manifest >> bytes >> digest) || digest.size() != 64 ||
          digest.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw sic::Error(sic::ErrorKind::invalid_format,
                         "invalid analytic checkpoint class checksum");
      backing[c] =
          BallFile::open(path / ("class-" + std::to_string(c) + ".balls"), slots, 2, bytes, digest);
    }
  }
  if (!(manifest >> tag) || tag != "end" || (manifest >> extra))
    throw sic::Error(sic::ErrorKind::invalid_format,
                     "truncated/trailing analytic checkpoint manifest");
  admit(physical);
  auto table = std::make_unique<Table>(ar, precision, threads);
  table->enable_compact(layout);
  for (long c = 0; c < classes; ++c)
    if (backing[c]) table->packed[c] = backing[c]->complex();
  if (paired)
    layout->pair_classes(mask, saved_cc,
                         [&](sic::support::Index point) { return group.act(saved_cc, point); });
  table->paired_values = paired;
  table->cached_enclosures = true;
  table->n_nodes_total = nodes;
  cc = saved_cc;
  derived = std::move(mask);
  return table;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
} catch (const std::ios_base::failure& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

}  // namespace sic::exact
