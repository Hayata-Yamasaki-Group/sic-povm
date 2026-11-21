// SPDX-License-Identifier: AGPL-3.0-or-later
#include <sched.h>

#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "sic/decode/decoder.hpp"
#include "sic/error.hpp"

void check(bool ok) {
  if (!ok) throw std::runtime_error("public decode contract");
}
int main(int argc, char** argv) {
  using sic::decode::Placement;
  check(argc == 2);
  check(Placement::parse_list("3,1-3,1") == std::vector<int>({3, 1, 2, 3, 1}));
  check(Placement::parse_list(",,,").empty());
  try {
    (void)Placement::parse_list("2-1");
    throw std::runtime_error("reversed range accepted");
  } catch (const sic::Error& err) {
    check(err.kind() == sic::ErrorKind::invalid_format);
  }
  check(Placement::parse_list(std::to_string(CPU_SETSIZE - 1) + "-" +
                              std::to_string(CPU_SETSIZE + 1)) ==
        std::vector<int>({CPU_SETSIZE - 1}));
  for (const char* bad : {"-1", "1-x", "1-2-3", "1x", "2147483648"}) {
    bool rejected = false;
    try {
      (void)Placement::parse_list(bad);
    } catch (const sic::Error& err) {
      rejected = err.kind() == sic::ErrorKind::invalid_format;
    }
    check(rejected);
  }
  sic::sicx::Reader reader(argv[1]);
  using Decoder = sic::decode::Decoder;
  Decoder::Options opts;
  opts.pin = false;
  opts.threads = 4;
  opts.target = 64;
  opts.delta = 64;
  opts.base_bits = 4096;
  Decoder decoder(reader, opts);
  const auto& placement = decoder.placement();
  // Detected placement is ordered by node; each represented node forms one group.
  std::set<int> nodes;
  for (unsigned worker = 0; worker < opts.threads; ++worker)
    nodes.insert(placement.node(worker, opts.threads));
  const auto groups = nodes.size();
  check(decoder.pool_groups() == groups);
  const auto* stats = &decoder.stats();
  auto req = Decoder::Request{reader.meta().representative.begin()->first,
                              std::vector<uint64_t>(reader.meta().smith.size(), 0)};
  check(decoder.decode({req}).size() == 1);
  check(&decoder.stats() == stats && stats->passes >= 1);
  check(decoder.base_precision() >= 4096 && decoder.pool_groups() == groups);
}
