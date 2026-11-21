// SPDX-License-Identifier: AGPL-3.0-or-later
// Direct library coverage: preplanning is not consumption, and both constructors
// admit the actual dependencies while sharing Reader's byte-source boundary.
#include <iostream>

#include "sic/access/remote.hpp"
#include "sic/decode/decoder.hpp"

int main(int argc, char** argv) {
  try {
    if (argc != 5 && argc != 6) return 2;
    if (argc == 6) {
      sic::access::RemoteOptions remote;
      remote.source = argv[3];
      remote.cache = argv[4];
      remote.max_fraction = 1;
      auto source = sic::access::remote_byte_source(argv[2], remote);
      const sic::support::ByteRange range{0, std::min<uint64_t>(source->size(), 512)};
      source->plan({&range, 1});
      source->schedule({&range, 1});
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      const auto start = std::chrono::steady_clock::now();
      source.reset();
      if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(2500))
        throw std::runtime_error("speculative HTTP cancellation exceeded bound");
      std::cout << "PASS cancellation\n";
      return 0;
    }
    sic::sicx::Reader local(argv[1]);
    const auto& meta = local.meta();
    std::vector<sic::decode::Decoder::Request> all, subset;
    for (uint64_t m = 0; m < meta.d; ++m) {
      const auto e = meta.entry(m);
      all.push_back(
          {e.representative, sic::sicx::words_add(meta.rays[0].word, e.word, meta.smith)});
      if (m == 0) subset.push_back(all.back());
    }
    sic::decode::Decoder::Options options;
    options.threads = 2;
    options.pin = false;
    options.memory_budget = 256ull << 20;
    sic::decode::Decoder reference(local, options);
    const auto expected = reference.decode(subset);
    for (unsigned mode = 0; mode < 2; ++mode) {
      sic::access::RemoteOptions remote;
      remote.source = argv[3];
      remote.cache = std::filesystem::path(argv[4]) / std::to_string(mode);
      remote.max_fraction = 1;
      remote.max_ranges = 1024;
      remote.prefetch_bytes = 0;
      remote.memory_budget = options.memory_budget;
      auto source = sic::access::remote_byte_source(argv[2], remote);
      sic::sicx::Reader reader(source);
      // First overplan all representatives, then decode only one. The second
      // iteration exercises the two-argument library constructor's lazy plan.
      auto decoder = mode ? std::make_unique<sic::decode::Decoder>(reader, options)
                          : std::make_unique<sic::decode::Decoder>(reader, options, all);
      const auto actual = decoder->decode(subset);
      if (!acb_equal(actual[0], expected[0])) throw std::runtime_error("library subset differs");
      if (source->stats().transferred >= reader.size())
        throw std::runtime_error("preflight caused unread representatives to be verified/fetched");
      std::cout << "PASS library mode=" << mode << " bytes=" << source->stats().transferred << '\n';
    }
    // Each source has independent cancellation; a conflicting decoder context
    // cannot replace the token of an already bound source.
    sic::access::RemoteOptions remote;
    remote.source = argv[3];
    remote.cache = std::filesystem::path(argv[4]) / "isolation-a";
    remote.max_fraction = 1;
    auto a = sic::access::remote_byte_source(argv[2], remote);
    remote.cache = std::filesystem::path(argv[4]) / "isolation-b";
    auto b = sic::access::remote_byte_source(argv[2], remote);
    auto token_a = std::make_shared<sic::support::StopToken>();
    auto token_b = std::make_shared<sic::support::StopToken>();
    a->bind_cancellation(token_a);
    b->bind_cancellation(token_b);
    bool conflict = false, cancelled = false;
    try {
      a->bind_cancellation(token_b);
    } catch (const sic::Error& e) {
      conflict = e.kind() == sic::ErrorKind::invalid_argument;
    }
    token_a->request_stop();
    std::array<uint8_t, 16> header{};
    try {
      a->read(0, header);
    } catch (const sic::Error& e) {
      cancelled = e.kind() == sic::ErrorKind::cancelled;
    }
    b->read(0, header);
    if (!conflict || !cancelled) throw std::runtime_error("source context isolation failed");
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
