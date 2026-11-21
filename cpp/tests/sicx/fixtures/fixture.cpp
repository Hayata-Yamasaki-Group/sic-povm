// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fixture.hpp"

#include "sicx/container/directory.hpp"
#include "sicx/container/leaf_index.hpp"
namespace sicx_tests {
RecoveringSource::RecoveringSource(sic::sicx::ByteBuffer bytes)
    : original(std::move(bytes)), wire(original) {}

uint64_t RecoveringSource::size() const { return wire.size(); }

void RecoveringSource::read(uint64_t offset, std::span<uint8_t> out) const {
  check({offset, out.size()});
  std::copy_n(wire.data() + offset, out.size(), out.data());
}

void RecoveringSource::invalidate(sic::support::ByteRange range) const {
  invalidated.push_back(range);
  wire = original;  // model discarding the bad fragments before a clean refetch
}
void write(const filesystem::path& path, const std::vector<Row>& rows, DirectoryEdit edit) {
  sic::sicx::ByteBuffer file(sic::sicx::kMagic.begin(), sic::sicx::kMagic.end());
  sic::sicx::put_le(file, 1, 2);
  sic::sicx::put_le(file, 0, 2);
  sic::sicx::put_le(file, 0, 4);
  std::vector<sic::sicx::IndexEntry> physical;
  std::vector<sic::sicx::LogicalEntry> logical;
  for (const auto& row : rows) {
    auto e = row.entry;
    e.offset = file.size();
    e.length = e.decoded_length = row.bytes.size();
    e.sha256 = sic::sicx::sha256(row.bytes.data(), row.bytes.size());
    if (e.type_name() == "EXMT" || e.type_name() == "TOWR" || e.type_name() == "BODY") {
      std::vector<sic::sicx::LogicalEntry> ls;
      try {
        ls = sic::sicx::chunk_leaves(e, row.bytes);
      } catch (const std::exception&) {
        sic::sicx::LogicalEntry leaf;
        leaf.type = e.type;
        leaf.element = e.object;
        leaf.first = e.first;
        leaf.count = e.count;
        leaf.sha256 = sic::sicx::logical_hash(leaf, row.bytes);
        ls.push_back(leaf);
      }
      e.first_leaf = logical.size();
      e.leaf_count = ls.size();
      logical.insert(logical.end(), ls.begin(), ls.end());
    } else {
      e.first_leaf = e.leaf_count = 0;
    }
    const auto chunk = sic::sicx::directory_chunk(e.type_name(), row.bytes);
    const size_t start = file.size();
    file.insert(file.end(), chunk.begin(), chunk.end());
    for (int j = 0; j < 4; ++j) file[start + 4 + j] = static_cast<uint8_t>(e.flags >> (8 * j));
    physical.push_back(e);
  }
  if (edit) edit(logical, physical);
  sic::sicx::ByteBuffer lt;
  sic::sicx::put_le(lt, logical.size(), 8);
  for (auto e : logical) e.put(lt);
  const auto ldir = sic::sicx::directory_chunk("LDIR", lt);
  const uint64_t lo = file.size();
  file.insert(file.end(), ldir.begin(), ldir.end());
  sic::sicx::ByteBuffer pt;
  sic::sicx::put_le(pt, physical.size(), 8);
  for (auto e : physical) e.put(pt);
  const auto pdir = sic::sicx::directory_chunk("PDIR", pt);
  const uint64_t po = file.size();
  file.insert(file.end(), pdir.begin(), pdir.end());
  sic::sicx::put_le(file, lo, 8);
  sic::sicx::put_le(file, lt.size(), 8);
  sic::sicx::put_le(file, po, 8);
  sic::sicx::put_le(file, pt.size(), 8);
  const auto er = sic::sicx::merkle_root(logical), fr = sic::sicx::file_tree_root(physical, ldir);
  file.insert(file.end(), er.begin(), er.end());
  file.insert(file.end(), fr.begin(), fr.end());
  sic::sicx::put_le(file, file.size() + 16, 8);
  file.insert(file.end(), sic::sicx::kEndMagic.begin(), sic::sicx::kEndMagic.end());
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(file.data()), file.size());
  if (!out) throw std::runtime_error("test write");
}

sic::support::Json json(const Row& row) {
  return sic::support::JsonParser::parse(
      std::string_view(reinterpret_cast<const char*>(row.bytes.data()), row.bytes.size()));
}

void set_json(Row& row, const sic::support::Json& j) {
  const auto s = sic::support::dump_json(j);
  row.bytes.assign(s.begin(), s.end());
}
Fixture::Fixture(const filesystem::path& file, const filesystem::path& directory)
    : work((filesystem::create_directories(directory), directory)), source(file) {
  (void)sic::sicx::verify_all(source, 2);
  for (size_t i = 0; i < source.index().size(); ++i)
    good.push_back({source.index()[i], source.chunk(i)});
}
int run_fixture_case(int argc, char** argv, void (*cases)(Fixture&)) {
  try {
    if (argc != 3) return 2;
    Fixture fixture(argv[1], argv[2]);
    cases(fixture);
    std::cout << "PASS final format checks=" << fixture.tests << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
}

void check(bool ok, const std::string& what) {
  if (!ok) {
    std::cerr << "FAIL " << what << '\n';
    ++failures;
  }
}

sic::sicx::ByteBuffer read_file(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  return sic::sicx::ByteBuffer(std::istreambuf_iterator<char>(in), {});
}

void write_file(const std::filesystem::path& p, const sic::sicx::ByteBuffer& b) {
  std::filesystem::remove(p);
  std::ofstream out(p, std::ios::binary);
  out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

int failures = 0;
}  // namespace sicx_tests
