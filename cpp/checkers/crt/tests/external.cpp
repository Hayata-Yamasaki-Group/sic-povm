// SPDX-License-Identifier: AGPL-3.0-or-later
// Optional executable-only interoperability corpus. Never includes a producer implementation.
#include <filesystem>
#include <fstream>
#include <iostream>

#include "../src/format/container.hpp"
#include "../src/format/integer_codec.hpp"
#include "../src/format/sicx_input.hpp"
#include "test_writer.hpp"
using namespace sic_check::crt::sicx;
namespace filesystem = std::filesystem;
fixture::ByteBuffer bytes(const string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot read external fixture");
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
fixture::File copy_file(const Input& in) {
  fixture::File f(in.d);
  f.physical.clear();
  f.leaves.clear();
  auto meta = in.payload(0);
  f.meta.assign(meta.begin(), meta.end());
  for (size_t ci = 0; ci < in.ch.size(); ++ci) {
    const auto& c = in.ch[ci];
    fixture::P p;
    p.type = c.type;
    p.flags = c.flags;
    p.codec = c.codec;
    p.decoded = c.decoded;
    p.first_leaf = c.first_leaf;
    p.leaf_count = c.leaf_count;
    p.object = c.object;
    p.first = c.first;
    p.count = c.count;
    p.bytes = in.payload(ci);
    if (c.type == "TOWR" || c.type == "BODY") {
      Cur cursor{p.bytes.data(), p.bytes.data() + p.bytes.size()};
      for (Uint64 li = c.first_leaf; li < c.first_leaf + c.leaf_count; ++li) {
        const Uint8* start = cursor.p;
        fixture::L l;
        l.kind = c.type;
        l.element = rd_varint(cursor);
        l.first = rd_varint(cursor);
        l.count = rd_varint(cursor);
        for (Uint64 covered = 0; covered < l.count;) {
          Uint64 tag = rd_varint(cursor), n = tag >> 1;
          if (tag & 1)
            for (Uint64 j = 0; j < n; ++j) {
              Z x, y;
              rd_int(cursor, x.v);
              rd_int(cursor, y.v);
            }
          covered += n;
        }
        l.bytes.assign(start, cursor.p);
        f.leaves.push_back(l);
      }
    }
    f.physical.push_back(p);
  }
  return f;
}
int main(int argc, char** argv) {
  try {
    if (argc < 3) throw std::runtime_error("usage: sic-test-crt-external OUTDIR FILE...");
    filesystem::create_directories(argv[1]);
    const auto temp = filesystem::path(argv[1]) / "mutated.sicx";
    std::ofstream table(filesystem::path(argv[1]) / "crosscheck.tsv");
    table << "file\tbytes\texact_root\tfile_root\tsha256\tchecks\n";
    size_t total = 0, rejected = 0;
    for (int arg = 2; arg < argc; ++arg) {
      Input in;
      in.open(argv[arg]);
      auto original = bytes(argv[arg]);
      auto f = copy_file(in);
      if (f.encode() != original)
        throw std::runtime_error("independent OpenSSL writer differs: " + string(argv[arg]));
      size_t checks = 1;
      // All chunks have one complete leaf in this small-d corpus. Consequently both
      // requested physical targets have exactly this same greedy grouping.
      for (const auto& c : in.ch)
        if (c.type == "TOWR" || c.type == "BODY")
          if (c.leaf_count != 1 || c.len > (16ull << 20))
            throw std::runtime_error("small-d profile assumption failed");
      ++checks;
      auto reject_copy = [&](const fixture::ByteBuffer& b, bool trusted = false) {
        {
          std::ofstream out(temp, std::ios::binary);
          out.write((const char*)b.data(), b.size());
        }
        try {
          Input candidate;
          candidate.open(temp.string(),
                         trusted ? Trust{hex(in.exact_root), hex(in.merkle), ""} : Trust{});
        } catch (const Reject&) {
          ++checks;
          ++rejected;
          return;
        }
        throw std::runtime_error("external mutated copy accepted: " + string(argv[arg]));
      };
      for (const auto& c : in.ch) {
        auto b = original;
        b.at(c.off) ^= 1;
        reject_copy(b);
        if (c.len) {
          b = original;
          b.at(c.off + 16 + c.len / 2) ^= 1;
          reject_copy(b);
        }
      }
      for (Uint64 off : {Uint64(0), Uint64(8), Uint64(12), in.f.size - 112 + 32,
                         in.f.size - 112 + 64, in.f.size - 1}) {
        auto b = original;
        b.at(off) ^= 1;
        reject_copy(b);
      }
      // Rehash semantic corruptions independently, so outer checksums cannot mask them.
      auto metadata_mutant = [&](const string& from, const string& to) {
        auto m = f;
        fixture::replace(m.meta, from, to);
        reject_copy(m.encode());
      };
      metadata_mutant("sicx-divisors/1.0", "sicx-2power/1");  // identity-negative
      metadata_mutant("\"unit_generator_words\":", "\"word_5\":");
      vector<string> original_words;
      for (const auto& word : in.unit_words) original_words.push_back(fixture::nums(word));
      const string words_key = "\"unit_generator_words\":";
      const string encoded_words = words_key + fixture::arr(original_words);
      if (!in.unit_words.empty()) {
        metadata_mutant(encoded_words, words_key + "[]");
        auto extra_words = original_words;
        extra_words.push_back(fixture::nums(vector<Uint64>(in.smith.size(), 0)));
        metadata_mutant(encoded_words, words_key + fixture::arr(extra_words));
        auto bad_word = in.unit_words.front();
        bad_word[0] = in.smith[0];
        auto bad_words = original_words;
        bad_words[0] = fixture::nums(bad_word);
        metadata_mutant(encoded_words, words_key + fixture::arr(bad_words));
        bool order_mutant = false;
        for (size_t j = 0; j < in.unit_words.size() && !order_mutant; ++j)
          for (size_t l = 0; l < in.smith.size(); ++l)
            if (in.unit_generators[j].order % in.smith[l]) {
              auto word = in.unit_words[j];
              word[l] = 1;
              bad_words = original_words;
              bad_words[j] = fixture::nums(word);
              metadata_mutant(encoded_words, words_key + fixture::arr(bad_words));
              order_mutant = true;
              break;
            }
      }
      auto missing_position = in.positions;
      missing_position.pop_back();
      metadata_mutant("\"representatives\":" + fixture::nums(in.positions),
                      "\"representatives\":" + fixture::nums(missing_position));
      {
        auto m = f;
        auto pos = m.meta.find("\"max_bits\":\"") + 12;
        auto end = m.meta.find('"', pos);
        m.meta.replace(pos, end - pos, "8388609");
        reject_copy(m.encode());
      }
      {
        auto m = f;
        fixture::P prov;
        prov.type = "PROV";
        prov.flags = 0;
        prov.bytes = fixture::text("{\"generator_rescaling\":\"forbidden\"}");
        bool changed = false;
        for (auto& c : m.physical)
          if (c.type == "PROV") {
            c.bytes = prov.bytes;
            changed = true;
            break;
          }
        if (!changed) {
          auto at = std::find_if(m.physical.begin(), m.physical.end(),
                                 [](const auto& c) { return c.type == "RAYS"; });
          m.physical.insert(at, prov);
        }
        reject_copy(m.encode());
      }
      for (size_t ci = 0; ci < f.physical.size(); ++ci) {
        if (f.physical[ci].type == "BALL") {
          auto m = f;
          fixture::set(m.physical[ci].bytes, 0, 2);
          reject_copy(m.encode());
        }
        if (f.physical[ci].type == "PROV") {
          auto m = f;
          m.physical[ci].bytes = fixture::text("{\"source\":\"another valid provenance\"}");
          reject_copy(m.encode(), true);
        }
      }
      // Partial checks also use the actual production directory and its transitive trust binding.
      Input partial;
      partial.open(argv[arg], {"", hex(in.merkle), ""}, false);
      if (!partial.scope.exact_trusted || !partial.scope.file_trusted ||
          partial.scope.chunks.size() != 1)
        throw std::runtime_error("external partial transitive trust");
      for (size_t ci = 1; ci < partial.ch.size(); ++ci) partial.verify_chunk(ci);
      if (!partial.scope.exact_checked || partial.scope.full)
        throw std::runtime_error("external partial exact completion");
      ++checks;
      table << filesystem::path(argv[arg]).filename().string() << '\t' << in.f.size << '\t'
            << hex(in.exact_root) << '\t' << hex(in.merkle) << '\t'
            << hex(fixture::digest(original)) << '\t' << checks << '\n';
      total += checks;
    }
    filesystem::remove(temp);
    std::cout << "PASS external: " << argc - 2 << " files, " << total << " checks, " << rejected
              << " mutated copies rejected\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
