// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fixtures/fixture.hpp"
#include "sicx/codec/block.hpp"
#include "support/sha256.hpp"
// SICX primitive and fault-control tests:
//   sic_test_sicx_sicx FIXTURE.sicx WORKDIR
// Primitives: canonical varints, integer code, canonical JSON, block runs.
// Faults on FIXTURE: a flipped byte in every chunk, truncation, a broken
// footer, and a mutated text-form line must each be refused (fail-closed).
// Streaming writer: kill/resume, corruption, parts, finalization.
#include <sys/wait.h>

#include <fstream>
#include <iostream>
#include <random>
#include <thread>

#include "sic/sicx/finalize.hpp"
#include "sic/sicx/text.hpp"

namespace sicx_tests {

void faults(const std::filesystem::path& fixture, const std::filesystem::path& work) {
  std::filesystem::create_directories(work);
  const sic::sicx::ByteBuffer good = read_file(fixture);
  {
    sic::sicx::Reader r(fixture);
    (void)sic::sicx::verify_all(r, 2);
  }
  std::vector<sic::sicx::IndexEntry> index;
  sic::sicx::Digest root;
  {
    sic::sicx::Reader r(fixture);
    index = r.index();
    root = r.root();
  }
  // One flipped byte inside every chunk payload: refused at open or by the full verification.
  for (const auto& e : index) {
    if (!e.length) continue;
    sic::sicx::ByteBuffer bad = good;
    bad[e.offset + sic::sicx::kChunkHeaderBytes + e.length / 2] ^= 0x01;
    write_file(work / "flip.sicx", bad);
    check(throws([&] {
            sic::sicx::Reader r(work / "flip.sicx");
            (void)sic::sicx::verify_all(r, 1);
          }),
          "flipped byte in " + e.type_name() + " refused");
  }
  // Header, INDX, footer and truncation.
  for (size_t position : {size_t(0), size_t(9), good.size() - 1, good.size() - 30,
                          good.size() - sic::sicx::kFooterBytes - 40}) {
    sic::sicx::ByteBuffer bad = good;
    bad[position] ^= 0x01;
    write_file(work / "flip.sicx", bad);
    check(throws([&] { sic::sicx::Reader r(work / "flip.sicx"); }),
          "flipped structural byte refused at " + std::to_string(position));
  }
  sic::sicx::ByteBuffer truncated(good.begin(), good.end() - 1);
  write_file(work / "short.sicx", truncated);
  check(throws([&] { sic::sicx::Reader r(work / "short.sicx"); }), "truncated file refused");
  sic::sicx::ByteBuffer extended = good;
  extended.push_back(0);
  write_file(work / "long.sicx", extended);
  check(throws([&] { sic::sicx::Reader r(work / "long.sicx"); }), "trailing byte refused");
  // Text form: a changed digit in any element line, or a dropped line, is refused and leaves no
  // output.
  const auto text = work / "good.jsonl";
  std::filesystem::remove(text);
  {
    sic::sicx::Reader r(fixture);
    std::ofstream out(text, std::ios::binary);
    sic::sicx::StreamSink sink(out);
    sic::sicx::dump_text_json(r, sic::support::sha256::file_hex(fixture), sink, 1);
  }
  std::vector<std::string> lines;
  {
    std::ifstream in(text);
    for (std::string l; std::getline(in, l);) lines.push_back(l);
  }
  for (size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].find("\"chunk\":\"RAYS\"") != std::string::npos) {
      const auto input = work / "rays-before-exmt.jsonl";
      const auto output = work / "rays-before-exmt.sicx";
      std::ofstream out(input, std::ios::binary);
      out << lines.front() << '\n' << lines[i] << '\n';
      for (size_t j = 1; j < lines.size(); ++j)
        if (j != i) out << lines[j] << '\n';
      out.close();
      std::filesystem::remove(output);
      std::string message;
      try {
        sic::sicx::text_json_to_binary(input, output);
      } catch (const std::exception& error) {
        message = error.what();
      }
      check(message.find("EXMT must be the first chunk line") != std::string::npos,
            "RAYS before EXMT rejected before metadata access");
      check(!std::filesystem::exists(output) &&
                !std::filesystem::exists(output.string() + ".rebuild") &&
                !std::filesystem::exists(output.string() + ".rebuild.partial"),
            "out-of-order text leaves no published or partial output");
      break;
    }
  }
  check(std::any_of(
            lines.begin(), lines.end(),
            [](const auto& line) { return line.find("\"chunk\":\"RAYS\"") != std::string::npos; }),
        "out-of-order control includes RAYS");
  for (size_t i = 0; i < lines.size(); ++i) {
    std::string l = lines[i];
    const auto at = l.find("[\"");
    if (at == std::string::npos || l.find("\"pairs\"") == std::string::npos) continue;
    const auto digit = l.find_first_of("123456789", l.find("\"pairs\""));
    l[digit] = l[digit] == '9' ? '8' : static_cast<char>(l[digit] + 1);
    std::ofstream out(work / "mutated.jsonl", std::ios::binary);
    for (size_t j = 0; j < lines.size(); ++j) out << (j == i ? l : lines[j]) << '\n';
    out.close();
    std::filesystem::remove(work / "mutated.sicx");
    check(throws([&] {
            sic::sicx::text_json_to_binary(work / "mutated.jsonl", work / "mutated.sicx");
          }),
          "mutated text line refused");
    check(!std::filesystem::exists(work / "mutated.sicx"),
          "refused text conversion leaves no output");
  }
  {
    std::ofstream out(work / "dropped.jsonl", std::ios::binary);
    for (size_t j = 0; j < lines.size(); ++j)
      if (j != 2) out << lines[j] << '\n';
  }
  std::filesystem::remove(work / "dropped.sicx");
  check(throws(
            [&] { sic::sicx::text_json_to_binary(work / "dropped.jsonl", work / "dropped.sicx"); }),
        "dropped text line refused");
  std::filesystem::remove(work / "again.sicx");
  check(sic::sicx::text_json_to_binary(text, work / "again.sicx") ==
            sic::support::sha256::file_hex(fixture),
        "text round trip");
}

// ------------------------------------------------------------- streaming writer
// The fixture's chunk sequence is rewritten through the StreamWriter under every
// fault: kill mid-chunk / mid-header / after k durable chunks (child process, SIGKILL),
// a corrupted finished chunk, out-of-order parallel commits, part files and the
// two-stage finalization (also killed and resumed). Every path must reproduce the
// fixture byte for byte; a changed input (plan) or a non-deterministic replay must be refused.
struct Seq {
  std::vector<sic::sicx::IndexEntry> index;
  std::vector<sic::sicx::ByteBuffer> payloads;
  size_t exact = 0;
  uint64_t indx_offset = 0;
};

void write_range(sic::sicx::StreamWriter& w, const Seq& s, size_t begin, size_t end) {
  for (size_t i = begin; i < end; ++i) {
    const auto& e = s.index[i];
    w.chunk(e.type_name(), e.flags, s.payloads[i], e.object, e.first, e.count);
  }
}
sic::sicx::StreamOptions durable_options(bool resume, uint8_t plan = 7) {
  sic::sicx::StreamOptions o;
  o.durable = true;
  o.resume = resume;
  o.plan.fill(plan);
  return o;
}
// Runs body in a child process with an environment variable set; returns true if the child died by
// SIGKILL.
template <class F>
bool killed_child(const char* var, uint64_t value, F body) {
  std::cout.flush();
  const pid_t pid = ::fork();
  if (pid == 0) {
    ::setenv(var, std::to_string(value).c_str(), 1);
    try {
      body();
    } catch (...) {
      ::_exit(3);
    }
    ::_exit(0);
  }
  int status = 0;
  ::waitpid(pid, &status, 0);
  return WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL;
}

void streaming(const std::filesystem::path& fixture, const std::filesystem::path& work, bool full) {
  const sic::sicx::ByteBuffer good = read_file(fixture);
  Seq s;
  {
    sic::sicx::Reader r(fixture);
    s.index = r.index();
    for (size_t i = 0; i < s.index.size(); ++i) s.payloads.push_back(r.chunk(i));
    s.exact = r.exact_count();
    check(r.finalized() && s.exact < s.index.size(), "fixture is finalized");
  }
  const auto out = work / "stream.sicx";
  auto clean = [&] {
    for (const char* suffix : {"", ".partial", ".partial.journal"})
      std::filesystem::remove(out.string() + suffix);
  };
  auto same_as_fixture = [&](const std::string& what) {
    check(std::filesystem::exists(out) && read_file(out) == good,
          what + ": byte-identical to the fixture");
    check(!std::filesystem::exists(out.string() + ".partial") &&
              !std::filesystem::exists(out.string() + ".partial.journal"),
          what + ": no partial or journal left");
  };
  // Plain and durable single runs.
  clean();
  {
    sic::sicx::StreamWriter w(out, sic::sicx::StreamOptions{false, false, {}, false});
    write_range(w, s, 0, s.index.size());
    w.finish();
  }
  same_as_fixture("non-durable writer");
  clean();
  {
    sic::sicx::StreamWriter w(out, durable_options(false));
    write_range(w, s, 0, s.index.size());
    w.finish();
  }
  same_as_fixture("durable writer");
  if (full) {
    // Kill points: inside a chunk header, inside payloads (first, middle, last chunk), inside
    // INDX/footer.
    std::vector<uint64_t> points{sic::sicx::kHeaderBytes + 5};
    for (size_t i : {size_t(0), s.index.size() / 2, s.exact - 1, s.index.size() - 1})
      points.push_back(s.index[i].offset + sic::sicx::kChunkHeaderBytes +
                       std::max<uint64_t>(1, s.index[i].length / 2));
    points.push_back(good.size() - 50);
    for (uint64_t point : points) {
      clean();
      const bool died = killed_child("SIC_FAULT_KILL_AT_BYTE", point, [&] {
        sic::sicx::StreamWriter w(out, durable_options(false));
        write_range(w, s, 0, s.index.size());
        w.finish();
      });
      check(died && std::filesystem::exists(out.string() + ".partial") &&
                !std::filesystem::exists(out),
            "killed at byte " + std::to_string(point) + " leaves only a partial file");
      check(throws([&] { sic::sicx::StreamWriter w(out, durable_options(false)); }),
            "existing partial refused without resume");
      check(throws([&] { sic::sicx::StreamWriter w(out, durable_options(true, 9)); }),
            "resume with a different plan refused");
      {
        sic::sicx::StreamWriter w(out, durable_options(true));
        write_range(w, s, 0, s.index.size());
        w.finish();
      }
      same_as_fixture("kill at byte " + std::to_string(point) + " + resume (replay)");
    }
    // Kill after k durable chunks; resume by adopting the recovered chunks without regenerating
    // them.
    for (uint64_t k : {uint64_t(1), uint64_t(s.index.size() / 2), uint64_t(s.index.size() - 1)}) {
      clean();
      const bool died = killed_child("SIC_FAULT_KILL_AFTER_CHUNKS", k, [&] {
        sic::sicx::StreamWriter w(out, durable_options(false));
        write_range(w, s, 0, s.index.size());
        w.finish();
      });
      {
        sic::sicx::StreamWriter w(out, durable_options(true));
        check(died && w.pending_replay() == k,
              "kill after " + std::to_string(k) + " chunks recovers exactly k chunks");
        const size_t n = w.pending_replay();
        w.adopt(n);
        write_range(w, s, n, s.index.size());
        w.finish();
      }
      same_as_fixture("kill after " + std::to_string(k) + " chunks + resume (adopt)");
    }
    // A corrupted finished chunk is detected on resume: the file is cut before it and rewritten.
    {
      clean();
      const size_t k = s.index.size() - 1;
      (void)killed_child("SIC_FAULT_KILL_AFTER_CHUNKS", k, [&] {
        sic::sicx::StreamWriter w(out, durable_options(false));
        write_range(w, s, 0, s.index.size());
        w.finish();
      });
      const size_t victim = 2;
      sic::sicx::ByteBuffer partial = read_file(out.string() + ".partial");
      partial[s.index[victim].offset + sic::sicx::kChunkHeaderBytes] ^= 0x40;
      write_file(out.string() + ".partial", partial);
      {
        sic::sicx::StreamWriter w(out, durable_options(true));
        check(w.pending_replay() == victim, "corrupted chunk cuts the recovered prefix");
        write_range(w, s, 0, s.index.size());
        w.finish();
      }
      same_as_fixture("corrupted chunk + resume");
    }
    // A replay that differs from the interrupted run is refused (non-deterministic producer).
    {
      clean();
      (void)killed_child("SIC_FAULT_KILL_AFTER_CHUNKS", 3, [&] {
        sic::sicx::StreamWriter w(out, durable_options(false));
        write_range(w, s, 0, s.index.size());
      });
      sic::sicx::StreamWriter w(out, durable_options(true));
      sic::sicx::ByteBuffer changed = s.payloads[1];
      changed.back() ^= 1;
      write_range(w, s, 0, 1);
      check(throws([&] {
              w.chunk(s.index[1].type_name(), s.index[1].flags, changed, s.index[1].object,
                      s.index[1].first, s.index[1].count);
            }),
            "non-deterministic replay refused");
      w.abandon();
    }
    check(!std::filesystem::exists(out.string() + ".partial"), "abandoned output removed");
  }
  // Part files (multi-node): three contiguous ranges written independently, then merged.
  {
    clean();
    const size_t cut1 = s.exact / 3, cut2 = 2 * s.exact / 3;
    const std::vector<std::pair<size_t, size_t>> ranges{
        {0, cut1}, {cut1, cut2}, {cut2, s.index.size()}};
    std::vector<std::filesystem::path> parts;
    for (size_t p = 0; p < ranges.size(); ++p) {
      const auto part = work / ("stream.part" + std::to_string(p));
      for (const char* suffix : {"", ".idx", ".partial", ".partial.journal"})
        std::filesystem::remove(part.string() + suffix);
      sic::sicx::StreamOptions o = durable_options(false);
      o.part = true;
      sic::sicx::StreamWriter w(part, o);
      write_range(w, s, ranges[p].first, ranges[p].second);
      w.finish();
      parts.push_back(part);
    }
    {
      sic::sicx::StreamWriter w(out, durable_options(false));
      for (const auto& part : parts) sic::sicx::append_part(w, part);
      w.finish();
    }
    same_as_fixture("three part files merged");
    sic::sicx::ByteBuffer bad = read_file(parts[1]);
    bad[bad.size() / 2] ^= 1;
    write_file(parts[1], bad);
    clean();
    check(throws([&] {
            sic::sicx::StreamWriter w(out, durable_options(false));
            for (const auto& part : parts) sic::sicx::append_part(w, part);
            w.abandon();
          }),
          "corrupted part refused at merge");
  }
  // Two-stage completion: exact part first (valid, not finalized), then finalization in place.
  auto write_exact = [&] {
    clean();
    sic::sicx::StreamWriter w(out, durable_options(false));
    write_range(w, s, 0, s.exact);
    w.finish();
  };
  write_exact();
  sic::sicx::Digest exact_root{};
  uint64_t indx_offset = 0;
  std::vector<sic::sicx::IndexEntry> exact_index;
  {
    sic::sicx::Reader r(out);
    check(!r.finalized() && r.exact_count() == s.exact,
          "exact-only file is valid and not finalized");
    (void)sic::sicx::verify_all(r, 2);
    exact_root = r.exact_root();
    indx_offset = r.ldir_offset();
    exact_index = r.index();
  }
  {
    auto w = sic::sicx::StreamWriter::reopen(out, durable_options(false), exact_index, indx_offset,
                                             exact_root);
    w->adopt(s.exact);
    write_range(*w, s, s.exact, s.index.size());
    w->finish();
  }
  same_as_fixture("exact part + in-place finalization");
  {
    sic::sicx::Reader r(out);
    check(r.exact_root() == exact_root && r.finalized(), "finalization keeps the exact root");
  }
  if (full) {
    // Finalization killed after the RAYS chunk, then resumed.
    write_exact();
    const bool died = killed_child("SIC_FAULT_KILL_AFTER_CHUNKS", 1, [&] {
      auto w = sic::sicx::StreamWriter::reopen(out, durable_options(false), exact_index,
                                               indx_offset, exact_root);
      w->adopt(s.exact);
      write_range(*w, s, s.exact, s.index.size());
      w->finish();
    });
    check(died && !std::filesystem::exists(out),
          "killed finalization leaves only the partial file");
    {
      auto w = sic::sicx::StreamWriter::resume_reopen(out, durable_options(true));
      check(w->pending_replay() == s.exact + 1,
            "resumed finalization recovers the exact part and RAYS");
      w->adopt(s.exact);
      write_range(*w, s, s.exact, s.index.size());
      w->finish();
    }
    same_as_fixture("killed finalization + resume");  // A private finalized snapshot can replace
                                                      // its tail while preserving every
    // exact-prefix byte and the complete LDIR chunk.
  }
  sic::sicx::Meta final_meta;
  sic::sicx::Finalization replacement;
  sic::sicx::ByteBuffer old_ldir;
  uint64_t prefix_end = 0;
  sic::sicx::Digest old_file_root;
  {
    sic::sicx::Reader r(out);
    final_meta = r.meta();
    old_file_root = r.root();
    prefix_end = r.index()[r.exact_count()].offset;
    const size_t bytes = 24 + r.logical().size() * sic::sicx::kLogicalEntryBytes;
    old_ldir.assign(good.begin() + r.ldir_offset(), good.begin() + r.ldir_offset() + bytes);
    final_meta.rays_decoder_source_commit = std::string(40, 'a');
    replacement.rays_text = sic::support::dump_json(sic::sicx::rays_to_json(final_meta));
    for (size_t i = 0; i < final_meta.rays.size(); ++i)
      replacement.balls.push_back(r.chunk(r.ball_chunk(i)));
    replacement.prec_text = sic::support::dump_json(r.json_chunk(*r.prec()));
  }
  (void)sic::sicx::finalize_in_place(out, final_meta, replacement, false, exact_root);
  {
    sic::sicx::Reader r(out);
    const auto changed = read_file(out);
    check(r.exact_root() == exact_root && r.root() != old_file_root,
          "replacement tail preserves exact root and changes file root");
    check(std::equal(good.begin(), good.begin() + prefix_end, changed.begin()),
          "replacement exact prefix is byte-identical");
    check(std::equal(old_ldir.begin(), old_ldir.end(), changed.begin() + r.ldir_offset()),
          "replacement LDIR is byte-identical");
  }
}
}  // namespace sicx_tests

namespace sicx_tests {
void codec_cases();
void metadata_cases();
}  // namespace sicx_tests
using namespace sicx_tests;
int main(int argc, char** argv) {
  try {
    flint_set_num_threads(1);
    sicx_tests::codec_cases();
    sicx_tests::metadata_cases();
    if (argc != 4 || (std::string(argv[3]) != "full" && std::string(argv[3]) != "positive"))
      throw std::runtime_error("select explicit full or positive primitive mode");
    const bool full = std::string(argv[3]) == "full";
#ifndef SIC_ENABLE_TEST_HOOKS
    if (full) throw std::runtime_error("full mode requires SIC_ENABLE_TEST_HOOKS=ON");
#endif
    faults(argv[1], argv[2]);
    streaming(argv[1], argv[2], full);
  } catch (const std::exception& error) {
    std::cerr << "FAIL exception: " << error.what() << '\n';
    return 1;
  }
  if (failures) {
    std::cerr << failures << " failures\n";
    return 1;
  }
  std::cout << "PASS sic-container primitives and fault controls\n";
  return 0;
}
