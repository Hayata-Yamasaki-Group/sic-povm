// SPDX-License-Identifier: AGPL-3.0-or-later
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "assertions.hpp"
#include "data/decoded_output.hpp"
#include "sic/core/interchange.hpp"
#include "sic/error.hpp"
#include "sic/sicx/text.hpp"
#include "support/resources/platform_snapshot.hpp"

namespace fs = std::filesystem;
std::string contents(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}
template <class F>
void collision(const fs::path& path, const std::string& message, F write) {
  const auto before = contents(path);
  bool refused = false;
  try {
    write();
  } catch (const sic::Error& error) {
    refused = error.kind() == sic::ErrorKind::io && error.what() == message;
  }
  SIC_CHECK(refused);
  SIC_CHECK(contents(path) == before);
}
int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("work directory required");
    const fs::path work = argv[1];
    fs::remove_all(work);
    fs::create_directories(work);
    sic::support::ComplexBallVector zero(1);
    const auto balls = work / "zero.balls";
    sic::data::write_balls(balls, zero);
    SIC_CHECK(contents(balls) == "sic-vector-balls/1.0\nd 1\n0 0 0 0 | 0 0 0 0\nend\n");
    collision(balls, "output exists: " + balls.string(),
              [&] { sic::data::write_balls(balls, zero); });
    const auto fp64 = work / "zero.fp64";
    const auto result =
        sic::data::write_decoded_fp64(fp64, zero, std::vector<sic::sicx::BallPart>(2), 1);
    SIC_CHECK(result.overlap == 1 && result.same64 == 1 && result.worst == INT64_MIN);
    SIC_CHECK(contents(fp64) == "0000000000000000 0000000000000000\n");
    collision(fp64, "output exists: " + fp64.string(), [&] {
      sic::data::write_decoded_fp64(fp64, zero, std::vector<sic::sicx::BallPart>(2), 1);
    });
    const auto provenance = work / "decode.provenance.json";
    const std::string provenance_text = "{\"decoder\":\"sic-decode/1.0\",\"d\":4}\n";
    sic::data::write_decode_provenance(provenance, provenance_text);
    SIC_CHECK(contents(provenance) == provenance_text);
    collision(provenance, "output exists: " + provenance.string(),
              [&] { sic::data::write_decode_provenance(provenance, "replacement"); });
    const auto index = work / "empty.parts.json";
    {
      sic::sicx::PartSink sink(work / "empty", 1024);
      sink.finish();
    }
    SIC_CHECK(contents(index) == "{\"parts\":[],\"schema\":\"sicx-text-json-parts/1.0\"}\n");
    collision(index, "SICX: parts index exists", [&] {
      sic::sicx::PartSink sink(work / "empty", 1024);
      sink.finish();
    });
    // General interchange deliberately replaces regular files with the complete new content.
    const auto interchange = (work / "interchange.bin").string();
    sic::core::interchange::write_file(interchange, "first");
    SIC_CHECK(contents(interchange) == "first");
    const std::string binary("new\0payload", 11);
    sic::core::interchange::write_file(interchange, binary);
    SIC_CHECK(contents(interchange) == binary);
    const auto symlink = work / "link";
    fs::create_symlink("missing", symlink);
    bool refused = false;
    try {
      sic::core::interchange::write_file(symlink.string(), "data");
    } catch (const sic::Error& error) {
      refused = error.kind() == sic::ErrorKind::io;
    }
    SIC_CHECK(refused && fs::is_symlink(fs::symlink_status(symlink)) &&
              !fs::exists(work / "missing"));
    const auto loop = work / "loop";
    fs::create_symlink("loop", loop);
    refused = false;
    try {
      sic::core::interchange::write_file((loop / "output").string(), "data");
    } catch (const sic::Error& error) {
      refused = error.kind() == sic::ErrorKind::io;
    }
    SIC_CHECK(refused);
    // Injected topology bypasses the process cache and keeps missing CPUs unassigned.
    const auto topology = work / "topology";
    fs::create_directories(topology / "node2");
    std::ofstream(topology / "node2/cpulist") << "1,4-5\n";
    SIC_CHECK(sic::support::resources::numa_node_count(topology) == 0);
    fs::create_directory(topology / "node0");
    fs::create_directory(topology / "node1");
    SIC_CHECK(sic::support::resources::numa_node_count(topology) == 3);
    const auto nodes = sic::support::resources::numa_cpu_nodes(topology);
    SIC_CHECK(nodes.size() == 3 && nodes.at(1) == 2 && nodes.at(4) == 2 && nodes.at(5) == 2);
    std::ofstream(topology / "node2/cpulist") << "6\n";
    SIC_CHECK(sic::support::resources::numa_cpu_nodes(topology).size() == 1);
    fs::remove(topology / "node1");
    SIC_CHECK(sic::support::resources::numa_node_count(topology) == 1);
    for (const auto& file : fs::directory_iterator(work))
      SIC_CHECK(file.path().filename().string().find(".tmp-") == std::string::npos);
    std::cout << "publication PASS\n";
    return sic_test::report();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
