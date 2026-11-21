// SPDX-License-Identifier: AGPL-3.0-or-later
#include "text_check.hpp"

#include "../format/ball_codec.hpp"
#include "../format/canonical_json.hpp"
#include "../format/container.hpp"
#include "../format/integer_codec.hpp"
#include "../format/metadata.hpp"
#include "../format/sha256.hpp"
#include "../runtime/diagnostics.hpp"
namespace sic_check::verifier {
string dec(const fmpz_t x) {
  std::unique_ptr<char, decltype(&flint_free)> s(fmpz_get_str(NULL, 10, x), flint_free);
  string r = s.get();
  return r;
}

string qs(const string& s) {
  string o;
  ser_str(o, s);
  return o;
}

string render_block_line(Container& C, const Chunk& c, vector<Uint8>& buf) {
  string leaves = "[";
  size_t cursor = 0;
  for (Uint64 j = 0; j < c.leaf_count; ++j) {
    const auto& l = C.logical[c.first_leaf + j];
    string runs = "[";
    bool in_pairs = false, first_run = true;
    auto close = [&]() {
      if (in_pairs) {
        runs += "]]";
        in_pairs = false;
      }
    };
    auto sink = [&](Uint64, const fmpz_t x, const fmpz_t y) {
      if (!in_pairs) {
        if (!first_run) runs += ",";
        runs += "[\"pairs\",[[";
        in_pairs = true;
        first_run = false;
      } else
        runs += ",[";
      runs += qs(dec(x)) + "," + qs(dec(y)) + "]";
    };
    std::function<void(Uint64, Uint64)> zs = [&](Uint64, Uint64 n) {
      close();
      if (!first_run) runs += ",";
      runs += "[\"zeros\"," + qs(std::to_string(n)) + "]";
      first_run = false;
    };
    size_t consumed = 0;
    parse_block(buf.data() + cursor, buf.size() - cursor, l.element, l.first, l.count, C.M.D0, true,
                sink, &zs, &consumed);
    cursor += consumed;
    close();
    runs += "]";
    if (j) leaves += ",";
    leaves += "{\"count\":" + qs(std::to_string(l.count)) +
              ",\"element\":" + qs(std::to_string(l.element)) +
              ",\"first\":" + qs(std::to_string(l.first)) + ",\"runs\":" + runs + "}";
  }
  return "{\"chunk\":" + qs(c.type) + ",\"codec\":\"0\",\"flags\":\"1\",\"leaves\":" + leaves +
         "]}";
}

string render_ball_line(Container& C, size_t ci, vector<Uint8>& buf) {
  Meta& M = C.M;
  const Chunk& c = C.ch[ci];
  Uint64 ray = c.object;
  Uint64 rec = 2 * (32 + 8 * M.limbs);
  BallPart bp;
  string o = "{\"chunk\":\"BALL\",\"flags\":\"1\",\"label\":" + qs(M.rays[ray].label) +
             ",\"ray\":" + qs(std::to_string(ray)) + ",\"values\":[";
  for (Uint64 j = 0; j < M.d; j++) {
    if (j) o += ",";
    o += "[";
    for (int part = 0; part < 2; part++) {
      parse_ball_part(&buf[j * rec + part * (32 + 8 * M.limbs)], M.limbs, bp);
      if (part) o += ",";
      o += "[" + qs(dec(bp.M)) + "," + qs(std::to_string(bp.E)) + "," + qs(std::to_string(bp.m)) +
           "," + qs(std::to_string(bp.e)) + "]";
    }
    o += "]";
  }
  return o + "]}";
}

bool bounded_text_line(FILE* fp, string& out, bool& had_nl, size_t limit) {
  out.clear();
  had_nl = false;
  for (;;) {
    int ch = fgetc(fp);
    if (ch == EOF) {
      if (ferror(fp)) reject("text read failed");
      return !out.empty();
    }
    if (ch == '\n') {
      had_nl = true;
      return true;
    }
    if (out.size() >= limit) throw PrecErr("text line exceeds reader resource limit");
    out.push_back(char(ch));
  }
}

void text_check(Container& C, const string& tpath) {
  say("text form %s\n", tpath.c_str());
  std::unique_ptr<FILE, decltype(&fclose)> input(fopen(tpath.c_str(), "rb"), &fclose);
  FILE* fp = input.get();
  if (!fp) reject("cannot open text file");
  Sha256 th;
  Uint64 nlines = 0, ncontent = 0, nbytes = 0;
  vector<Uint8> buf;
  auto next_line = [&](string& out, bool& had_nl) { return bounded_text_line(fp, out, had_nl); };
  auto expect = [&](const string& want, const char* what) {
    string got;
    bool nl;
    if (!next_line(got, nl)) reject(string("text: file ends early before ") + what);
    if (!nl) reject("text: line not newline-terminated");
    if (got != want) {
      size_t i = 0;
      while (i < got.size() && i < want.size() && got[i] == want[i]) i++;
      reject(string("text: line differs from the rendering of the binary at ") + what +
             " (first difference at byte " + std::to_string(i) + ", line " +
             std::to_string(nlines + 1) + ")");
    }
    // Independent syntactic check of small lines: parse as canonical JSON
    if (got.size() < (8u << 20)) parse_canonical((const Uint8*)got.data(), got.size(), "text line");
    th.update(got.data(), got.size());
    th.update("\n", 1);
    nlines++;
    nbytes += got.size() + 1;
  };
  expect(
      "{\"content_hash\":\"sha256\",\"required_features\":[],\"schema\":\"sicx-text-json/"
      "1.0\",\"sicx_version\":[\"1\",\"" +
          std::to_string(C.M.minor) + "\"]}",
      "header");
  {
    read_payload(C.f, C.ch[C.meta_idx], buf);
    expect("{\"chunk\":\"EXMT\",\"flags\":\"1\",\"meta\":" + string((char*)buf.data(), buf.size()) +
               "}",
           "EXMT");
  }
  for (size_t i = 0; i < C.ch.size(); i++) {
    if ((int)i == C.meta_idx) continue;
    const Chunk& c = C.ch[i];
    string ty = c.type;
    read_payload(C.f, c, buf);
    if (ty == "TOWR" || ty == "BODY")
      expect(render_block_line(C, c, buf), "block"), ncontent++;
    else if (ty == "BALL")
      expect(render_ball_line(C, i, buf), "BALL");
    else if (ty == "RAYS" || ty == "PREC" || ty == "PROV")
      expect("{\"chunk\":" + qs(ty) + ",\"flags\":\"" + (ty == "RAYS" ? "1" : "0") +
                 "\",\"json\":" + string((char*)buf.data(), buf.size()) + "}",
             ty.c_str());
  }
  Hash ch;
  {
    Sha256 t2 = th;
    t2.final(ch.data());
  }
  string last;
  bool nl;
  if (!next_line(last, nl)) reject("text: missing final line");
  if (!nl) reject("text: final line not newline-terminated");
  string want = "{\"content_sha256\":\"" + hex(ch) + "\",\"exact_root\":\"" + hex(C.exact_root) +
                "\",\"lines\":\"" + std::to_string(nlines) + "\",\"merkle_root\":\"" + hex(C.root) +
                "\",\"sicx_bytes\":\"" + std::to_string(C.fsize) + "\",\"sicx_sha256\":\"" +
                hex(C.filesha) + "\"}";
  if (last != want) {
    // diagnose the "lines" convention
    string alt = want;
    size_t p = alt.find("\"lines\":\"");
    alt.replace(p + 9, std::to_string(nlines).size(), std::to_string(nlines + 1));
    if (last == alt)
      reject(
          "text: final line matches only if `lines` counts the final line itself (spec: 'their "
          "line count' = preceding lines)");
    reject("text: final line differs from the recomputed one: " + last.substr(0, 400));
  }
  string extra;
  bool nl2;
  if (next_line(extra, nl2)) reject("text: data after the final line");
  say("  text form matches the binary exactly: %llu lines before the final line (%llu block "
      "lines), %llu bytes, content SHA-256 %s\n",
      (unsigned long long)nlines, (unsigned long long)ncontent, (unsigned long long)nbytes,
      hex(ch).c_str());
}
}  // namespace sic_check::verifier
