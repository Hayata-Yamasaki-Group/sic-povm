// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <fstream>
#include <iostream>

#include "sic/access/remote.hpp"
#include "sic/sicx/finalize.hpp"
#include "sic/transport/parts.hpp"
#include "sicx/codec/block.hpp"
namespace sicx_tests {

namespace filesystem = std::filesystem;
struct Row {
  sic::sicx::IndexEntry entry;
  sic::sicx::ByteBuffer bytes;
};
struct RecoveringSource final : sic::support::ByteSource {
  sic::sicx::ByteBuffer original;
  mutable sic::sicx::ByteBuffer wire;
  mutable std::vector<sic::support::ByteRange> invalidated;
  explicit RecoveringSource(sic::sicx::ByteBuffer bytes);
  uint64_t size() const override;
  void read(uint64_t offset, std::span<uint8_t> out) const override;
  void invalidate(sic::support::ByteRange range) const override;
};
using DirectoryEdit =
    std::function<void(std::vector<sic::sicx::LogicalEntry>&, std::vector<sic::sicx::IndexEntry>&)>;

void write(const filesystem::path& path, const std::vector<Row>& rows, DirectoryEdit edit = {});
sic::support::Json json(const Row& row);
void set_json(Row& row, const sic::support::Json& j);
struct Fixture {
  filesystem::path work;
  sic::sicx::Reader source;
  std::vector<Row> good;
  unsigned tests = 0;
  Fixture(const filesystem::path& file, const filesystem::path& directory);
  template <class Mutate>
  void rejected(const std::string& name, Mutate mutate);
};
int run_fixture_case(int argc, char** argv, void (*cases)(Fixture&));
extern int failures;
void check(bool ok, const std::string& what);
sic::sicx::ByteBuffer read_file(const filesystem::path& p);
void write_file(const filesystem::path& p, const sic::sicx::ByteBuffer& b);
template <class F>
bool throws(F f);
}  // namespace sicx_tests
#include "fixture.tpp"
