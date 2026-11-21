// SPDX-License-Identifier: AGPL-3.0-or-later
#include "container.hpp"

#include "../runtime/diagnostics.hpp"
#include "ball_codec.hpp"
#include "canonical_json.hpp"
#include "integer_codec.hpp"
#include "metadata.hpp"
#include "sha256.hpp"
#ifdef SIC_CHECKER_FAULT_SEAMS
#include "../runtime/fault_seams.hpp"
#define fstat sic_check::verifier::test::test_fstat
#define fseeko sic_check::verifier::test::test_fseeko
#define ftello sic_check::verifier::test::test_ftello
#endif
namespace sic_check::verifier {
namespace {
constexpr const char* kDecoderIdentity = "sic-decode/1.0";
}
void admit_physical_payload(Uint64 size) {
  if (size > (256ull << 20)) throw PrecErr("physical payload exceeds 256 MiB resource limit");
}

void admit_rescaling_key(size_t count) {
  if (count >= (1u << 24)) throw PrecErr("rescaling weight-table resource limit");
}

auto File::stable() -> void {
  struct stat now {};
  if (fstat(fileno(f), &now) || now.st_dev != snapshot.st_dev || now.st_ino != snapshot.st_ino ||
      now.st_size != snapshot.st_size || now.st_mtim.tv_sec != snapshot.st_mtim.tv_sec ||
      now.st_mtim.tv_nsec != snapshot.st_mtim.tv_nsec ||
      now.st_ctim.tv_sec != snapshot.st_ctim.tv_sec ||
      now.st_ctim.tv_nsec != snapshot.st_ctim.tv_nsec)
    reject("source snapshot changed during verification");
}

auto File::open(const string& p) -> void {
  path = p;
  f = fopen(p.c_str(), "rb");
  if (!f) reject("cannot open " + p);
  if (fstat(fileno(f), &snapshot)) reject("cannot stat source snapshot");
  if (fseeko(f, 0, SEEK_END) != 0) reject("file seek failed");
  off_t end = ftello(f);
  if (end < 0) reject("file size unavailable");
  size = Uint64(end);
  if (size > UINT64_MAX / 8) reject("source exceeds SHA-256 message length domain");
}

auto File::read(Uint64 off, void* b, size_t n) -> void {
  if (off > size || n > size - off) reject("read past end of file");
  if (fseeko(f, (off_t)off, SEEK_SET) != 0 || fread(b, 1, n, f) != n) reject("read error");
}

auto File::read(Uint64 off, size_t n) -> vector<Uint8> {
  if (off > size || n > size - off) reject("read range out of bounds");
  vector<Uint8> v(n);
  if (n) read(off, v.data(), n);
  return v;
}

File::~File() {
  if (f) fclose(f);
}

Hash merkle(const vector<Hash>& leaves, size_t lo, size_t hi) {
  size_t n = hi - lo;
  if (!n) return sha("", 0);
  if (n == 1) return leaves[lo];
  size_t k = 1;
  while (k * 2 < n) k *= 2;  // largest power of two strictly below n
  Hash l = merkle(leaves, lo, lo + k), r = merkle(leaves, lo + k, hi);
  Uint8 b[65];
  b[0] = 1;
  memcpy(b + 1, l.data(), 32);
  memcpy(b + 33, r.data(), 32);
  return sha(b, 65);
}

int type_rank(const string& t) {
  const vector<string> types{"EXMT", "TOWR", "BODY", "PROV", "RAYS", "BALL", "PREC"};
  for (size_t i = 0; i < types.size(); ++i)
    if (t == types[i]) return i;
  return -1;
}

void hash_check(const Hash& got, const Hash& want, const string& what) {
  if (got != want) reject("SHA-256 mismatch: " + what);
}

void put64(vector<Uint8>& b, Uint64 x) {
  for (int i = 0; i < 8; ++i) b.push_back(x >> (8 * i));
}

Hash logical_hash(const Logical& l, const Uint8* b, size_t n) {
  Sha256 h;
  h.update("SICX1-LEAF\0", 11);
  h.update(l.kind.data(), 4);
  vector<Uint8> fields;
  put64(fields, l.element);
  put64(fields, l.first);
  put64(fields, l.count);
  put64(fields, n);
  h.update(fields.data(), fields.size());
  h.update(b, n);
  Hash r;
  h.final(r.data());
  return r;
}

Hash tree_entries(const vector<Uint8>& bytes, size_t width) {
  vector<Hash> h;
  for (size_t i = 8; i < bytes.size(); i += width) {
    Sha256 s;
    Uint8 z = 0;
    s.update(&z, 1);
    s.update(bytes.data() + i, width);
    Hash v;
    s.final(v.data());
    h.push_back(v);
  }
  return merkle(h, 0, h.size());
}

void expected_hash(const string& expected, const Hash& actual, const char* what) {
  if (expected.empty()) return;
  if (expected.size() != 64 || expected.find_first_not_of("0123456789abcdef") != string::npos)
    throw UsageErr(string(what) + ": expected 64 lowercase hex digits");
  if (expected != hex(actual)) reject(string("trusted ") + what + " mismatch");
}

void check_header(File& f, const Chunk& c) {
  auto h = f.read(c.off, 16);
  if (memcmp(h.data(), c.type, 4) || le32(h.data() + 4) != c.flags || le64(h.data() + 8) != c.len)
    reject("physical header differs from PDIR");
}

void read_payload(File& f, const Chunk& c, vector<Uint8>& buf) {
  buf.resize(c.len);
  if (c.len) f.read(c.off + 16, buf.data(), c.len);
}

ElemAcc::ElemAcc() { fmpz_init(g); }

ElemAcc::~ElemAcc() { fmpz_clear(g); }

void rescaling_check(Container& C, const Opts&) {
  auto residual = rescaling_rule(C);
  if (std::any_of(residual.begin(), residual.end(), [](Uint64 x) { return x != 0; }))
    reject("stored leaves not in canonical 2-adic form");
  // For reconstructed source valuation v+a.J, the maximum at greedy step s
  // is J_s + min floor((v + sum_future a_i J_i)/a_s), assuming earlier steps
  // agreed. Thus J_s is reproduced iff some constraint has future slack<a_s.
  // Every weight is <=511 (own exponent <=511; radix digit <=30). Reverse
  // accumulation can stop at512, before multiplying potentially huge shifts.
  // Equal-weight constraints retain only their minimum v; gains still include
  // every nonzero original coordinate. No reconstructed valuation is stored.
  vector<int> order;
  vector<bool> tight(C.M.t(), false);
  for (int s = 0; s < C.M.t(); ++s) {
    if (C.rs_gain[s])
      order.push_back(s);
    else if (C.M.shifts[s])
      reject("recorded rescaling shifts not reproduced by inverse transform");
  }
  std::stable_sort(order.begin(), order.end(),
                   [&](int a, int b) { return C.rs_gain[a] > C.rs_gain[b]; });
  for (const auto& kv : C.rs_minv) {
    Uint64 slack = kv.second;
    if (slack >= 512) continue;
    const Uint64 e = kv.first & ((1ull << 40) - 1), ow = kv.first >> 46;
    const int ol = int((kv.first >> 40) & 63) - 1;
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      const int s = *it;
      const Uint64 a = s == ol ? ow : (e / C.rs_N[s]) % C.M.deg[s];
      if (a && slack < a) tight[s] = true;
      if (a && C.M.shifts[s]) {
        if (C.M.shifts[s] > (511 - slack) / a) break;
        slack += a * C.M.shifts[s];  // Proven <=511 by the preceding comparison.
      }
    }
  }
  if (std::any_of(order.begin(), order.end(), [&](int s) { return !tight[s]; }))
    reject("recorded rescaling shifts not reproduced by inverse transform");
  C.rescaling_line = "canonical rescaling and recorded shifts verified";
}

void report_trust(const Container& C, const Opts& O) {
  const char* exact = !O.expected_exact.empty() ? "direct trusted match"
                      : !O.expected_file.empty()
                          ? "transitively trusted via file root and full LDIR binding"
                      : !O.expected_sha.empty() ? "transitively trusted via whole-file SHA-256"
                                                : "recomputed, untrusted";
  const char* file = !O.expected_file.empty()  ? "direct trusted match"
                     : !O.expected_sha.empty() ? "transitively trusted via whole-file SHA-256"
                                               : "recomputed, untrusted";
  say("  exact root %s (%s); file root %s (%s)\n", hex(C.exact_root).c_str(), exact,
      hex(C.root).c_str(), file);
}

void open_container(Container& C, const Opts& O) {
  File& f = C.f;
  f.open(O.path);
  C.fsize = f.size;
  C.footer_size = 112;
  if (f.size < 16 + 112 + 2 * 24) reject("file too short for SICX 1.0");
  auto hdr = f.read(0, 16), ft = f.read(f.size - 112, 112);
  if (memcmp(hdr.data(), "SICX\r\n\x1a\n", 8)) reject("bad header magic");
  if (le16(hdr.data() + 8) != 1 || le16(hdr.data() + 10) != 0) reject("unsupported version");
  if (le32(hdr.data() + 12)) reject("unsupported header required_features");
  if (memcmp(ft.data() + 104, "SICXEND\x1a", 8)) reject("bad footer magic");
  C.ldir_off = le64(ft.data());
  C.ldir_len = le64(ft.data() + 8);
  C.pdir_off = le64(ft.data() + 16);
  C.pdir_len = le64(ft.data() + 24);
  memcpy(C.exact_root.data(), ft.data() + 32, 32);
  memcpy(C.root.data(), ft.data() + 64, 32);
  if (le64(ft.data() + 96) != f.size || C.ldir_off < 16 ||
      checked_add(checked_add(C.ldir_off, 16), C.ldir_len) != C.pdir_off ||
      checked_add(checked_add(checked_add(C.pdir_off, 16), C.pdir_len), 112) != f.size)
    reject("footer/directory geometry mismatch");
  auto directory = [&](Uint64 off, Uint64 len, const char* type, Uint64 width) {
    if (len < 8) reject(string(type) + " payload too short");
    auto h = f.read(off, 24);
    if (memcmp(h.data(), type, 4) || le32(h.data() + 4) != 1 || le64(h.data() + 8) != len)
      reject(string(type) + " header mismatch");
    Uint64 count = le64(h.data() + 16);
    if (!count) reject(string(type) + " count is zero");
    if (checked_add(8, checked_mul(count, width)) != len)
      reject(string(type) + " length/count mismatch");
    if (count > (1u << 24))
      throw PrecErr(string(type) + " exceeds directory-entry resource limit 2^24");
    return f.read(off + 16, len);
  };
  auto ld = directory(C.ldir_off, C.ldir_len, "LDIR", 64);
  auto pd = directory(C.pdir_off, C.pdir_len, "PDIR", 112);
  hash_check(tree_entries(ld, 64), C.exact_root, "exact root");
  auto lbytes = f.read(C.ldir_off, 16 + C.ldir_len);
  Hash lh = sha(lbytes.data(), lbytes.size());
  vector<Hash> file_leaves;
  for (size_t i = 8; i < pd.size(); i += 112) {
    Sha256 s;
    Uint8 zero = 0;
    s.update(&zero, 1);
    s.update(pd.data() + i, 112);
    Hash h;
    s.final(h.data());
    file_leaves.push_back(h);
  }
  {
    Sha256 s;
    Uint8 zero = 0;
    s.update(&zero, 1);
    s.update("SICX1-LDIR\0", 11);
    s.update(lh.data(), 32);
    Hash h;
    s.final(h.data());
    file_leaves.push_back(h);
  }
  hash_check(merkle(file_leaves, 0, file_leaves.size()), C.root, "file root");
  expected_hash(O.expected_exact, C.exact_root, "exact root");
  expected_hash(O.expected_file, C.root, "file root");
  for (size_t i = 8; i < ld.size(); i += 64) {
    const Uint8* p = ld.data() + i;
    Logical l;
    l.kind = string((const char*)p, 4);
    if (le32(p + 4)) reject("LDIR reserved field nonzero");
    l.element = le64(p + 8);
    l.first = le64(p + 16);
    l.count = le64(p + 24);
    memcpy(l.sha.data(), p + 32, 32);
    if (l.kind != "EXMT" && l.kind != "TOWR" && l.kind != "BODY") reject("undefined LDIR kind");
    C.logical.push_back(l);
  }
  const auto& first = C.logical[0];
  if (first.kind != "EXMT" || first.element || first.first || first.count != 1)
    reject("LDIR EXMT singleton invalid");
  Uint64 off = 16, next_leaf = 0;
  int last = -1;
  size_t balls = 0;
  int counts[7]{};
  for (size_t i = 8; i < pd.size(); i += 112) {
    const Uint8* p = pd.data() + i;
    Chunk c;
    memcpy(c.type, p, 4);
    for (int j = 0; j < 4; ++j)
      if (p[j] < 0x21 || p[j] > 0x7e) reject("non-ASCII chunk type");
    c.flags = le32(p + 4);
    c.off = le64(p + 8);
    c.len = le64(p + 16);
    if (le32(p + 24) != 0) reject("unsupported codec");
    if (le32(p + 28)) reject("PDIR reserved field nonzero");
    if (le64(p + 32) != c.len) reject("raw decoded length mismatch");
    c.first_leaf = le64(p + 40);
    c.leaf_count = le64(p + 48);
    c.object = le64(p + 56);
    c.first = le64(p + 64);
    c.count = le64(p + 72);
    memcpy(c.sha.data(), p + 80, 32);
    string ty = c.type;
    int rank = type_rank(ty);
    if (rank < 0) reject("undefined chunk type " + ty);
    if (rank < last) reject("chunk order violation");
    last = rank;
    ++counts[rank];
    if ((rank == 0 || rank == 3 || rank == 4 || rank == 6) && counts[rank] > 1)
      reject("duplicate singleton chunk " + ty);
    if (c.flags != ((ty == "PROV" || ty == "PREC") ? 0u : 1u)) reject("invalid chunk flags");
    if (c.off != off) reject("physical chunks not contiguous");
    off = checked_add(checked_add(off, 16), c.len);
    if (off > C.ldir_off) reject("physical chunk overlaps directories");
    if (ty == "EXMT" || ty == "TOWR" || ty == "BODY") {
      if (!c.leaf_count || c.first_leaf != next_leaf || c.leaf_count > C.logical.size() - next_leaf)
        reject("logical coverage missing/overlapping/out of range");
      if (ty == "EXMT") {
        if (C.ch.size() != 0 || c.first_leaf || c.leaf_count != 1 || c.object || c.first || c.count)
          reject("EXMT physical mapping invalid");
        C.meta_idx = 0;
      } else {
        Uint64 coord = c.first;
        for (Uint64 j = 0; j < c.leaf_count; ++j) {
          const auto& l = C.logical[next_leaf + j];
          if (l.kind != ty || l.element != c.object || l.first != coord || !l.count)
            reject("physical/logical mapping differs");
          coord = checked_add(coord, l.count);
        }
        if (coord != checked_add(c.first, c.count)) reject("physical coordinate span mismatch");
      }
      next_leaf += c.leaf_count;
    } else {
      if (c.first_leaf || c.leaf_count) reject("non-leaf chunk claims logical coverage");
      if (ty == "BALL") {
        if (c.object != balls++ || c.first) reject("BALL mapping/order invalid");
      } else if (c.object || c.first || c.count)
        reject("non-leaf chunk object/span nonzero");
      if (ty == "RAYS") C.rays_idx = C.ch.size();
    }
    admit_physical_payload(c.len);
    if (ty != "TOWR" && ty != "BODY" && ty != "BALL" && c.len > (64ull << 20))
      throw PrecErr("JSON exceeds 64 MiB resource limit");
    C.ch.push_back(c);
  }
  if (off != C.ldir_off || next_leaf != C.logical.size()) reject("directory coverage incomplete");
  if (counts[0] != 1 || C.meta_idx != 0) reject("EXMT must be first and unique");
  if (counts[4] && !counts[5]) reject("RAYS without required BALL chunks");
  if ((counts[5] || counts[6]) && !counts[4]) reject("BALL/PREC without RAYS");
  vector<Uint8> mb;
  check_header(f, C.ch[0]);
  read_payload(f, C.ch[0], mb);
  hash_check(sha(mb.data(), mb.size()), C.ch[0].sha, "EXMT physical payload");
  hash_check(logical_hash(first, mb.data(), mb.size()), first.sha, "EXMT logical payload");
  parse_exmt(C.M, mb.data(), mb.size());
  for (const auto& p : C.ch)
    if (string(p.type) == "BALL" && p.count != C.M.d) reject("BALL coordinate count != d");
  Uint64 li = 1;
  for (size_t e = 0; e < C.M.el.size(); ++e) {
    const Elem& el = *C.M.el[e];
    if (el.nblocks > C.logical.size() - li) reject("missing logical leaves");
    for (Uint64 b = 0; b < el.nblocks; ++b) {
      const Logical& l = C.logical[li++];
      if (l.kind != (el.kind == "rep" ? "BODY" : "TOWR") || l.element != e || l.first != b * el.W ||
          l.count != el.W)
        reject("LDIR differs from EXMT logical boundary rule");
    }
  }
  if (li != C.logical.size()) reject("extra logical leaves");
  // RAYS is intentionally deferred for partial reads. Its untrusted payload is never used there.
  if (O.partial_elements.empty() && C.rays_idx >= 0) {
    auto& c = C.ch[C.rays_idx];
    vector<Uint8> rb;
    check_header(f, c);
    read_payload(f, c, rb);
    hash_check(sha(rb.data(), rb.size()), c.sha, "RAYS payload");
    Jv r = parse_canonical(rb.data(), rb.size(), "RAYS");
    if (r.get("decoder"))
      j_keys(r, {"balls", "decoder", "rays"}, "RAYS");
    else
      j_keys(r, {"balls", "rays"}, "RAYS");
    parse_rays(C.M, j_get(r, "balls", 'o'), j_get(r, "rays", 'a'));
    if (const Jv* dc = r.get("decoder")) {
      j_keys(*dc, {"name", "source_commit"}, "RAYS.decoder");
      string name = j_get(*dc, "name", 's').s, commit = j_get(*dc, "source_commit", 's').s;
      if (name != kDecoderIdentity) reject("unsupported decoder identifier");
      if ((commit.size() != 40 && commit.size() != 64) ||
          commit.find_first_not_of("0123456789abcdef") != string::npos)
        reject("decoder source_commit invalid");
      C.M.has_decoder = true;
      C.M.decoder_name = name;
      C.M.decoder_source_commit = commit;
    }
    admit_ball_memory(C.M.rays.size(), C.M.d, C.M.limbs);
    if (balls != C.M.rays.size()) reject("BALL count differs from RAYS");
    for (const auto& p : C.ch)
      if (string(p.type) == "BALL" &&
          (p.count != C.M.d || p.len != checked_mul(C.M.d, 2 * (32 + 8 * C.M.limbs))))
        reject("BALL size/span mismatch");
  }
  if (O.release && (C.rays_idx < 0 || !C.M.has_decoder))
    reject("--release requires finalized RAYS with decoder record");
}

void verify_chunks(Container& C, const Opts& O) {
  File& f = C.f;
  Meta& M = C.M;
  vector<bool> selected(M.el.size(), O.partial_elements.empty());
  for (Uint64 e : O.partial_elements) {
    if (e >= M.el.size()) throw UsageErr("partial element id out of range");
    selected[e] = true;
  }
  C.all_exact = std::all_of(selected.begin(), selected.end(), [](bool v) { return v; });
  size_t nray = 0;
  vector<ElemAcc> acc(M.el.size());
  for (size_t e = 0; e < M.el.size(); e++) acc[e].found.assign(M.el[e]->levels.size(), 0);
  vector<Uint8> buf;
  Encoder enc;
  Uint64 nzero_total = 0, ncoord_total = 0, nreenc = 0, ndeep_el = 0;
  Uint64 payload_tower = 0, payload_body = 0;
  vector<bool> deep(M.el.size());
  for (size_t e = 0; e < M.el.size(); e++) {
    deep[e] = true;
    if (deep[e]) ndeep_el++;
  }
  const int nch = (int)C.ch.size();
  Integer u;
  // spec 3.7 rule inputs per element: own level (the element's field level; -1 for rep), own
  // weight, v_2(scale)
  const int t = M.t();
  vector<int> own_lv(M.el.size(), -1);
  vector<Uint64> own_w(M.el.size(), 0), v2u(M.el.size(), 0);
  for (size_t e = 0; e < M.el.size(); e++) {
    const Elem& el = *M.el[e];
    v2u[e] = fmpz_val2(el.scale);
    if (el.kind == "rep") continue;
    Uint64 p = M.deg[el.level], Cc = p * (p - 1) / 2;
    own_lv[e] = (int)el.level;
    if (el.kind == "wsq" || el.kind == "act")
      own_w[e] = 2;
    else if (el.kind == "fcoef")
      own_w[e] = p - (Uint64)el.k;
    else if (el.kind == "actdelta")
      own_w[e] = Cc;
    else if (el.kind == "actodd")
      own_w[e] = Cc + 1 - (Uint64)el.k;
    else
      reject("internal: unknown element kind " + el.kind);
    if (own_w[e] > 511) reject("internal: own weight exceeds 9 bits");
  }
  C.rs_gain.assign(t, 0);
  C.rs_minv.clear();
  C.rs_leaves = 0;
  C.rs_N.assign(t + 1, 1);
  for (int i = 0; i < t; i++) C.rs_N[i + 1] = C.rs_N[i] * M.deg[i];  // N_i (spec 3.1)

  for (int i = 0; i < nch; ++i) {
    Chunk& c = C.ch[i];
    string ty = c.type;
    bool isblock = ty == "TOWR" || ty == "BODY";
    if (!O.partial_elements.empty() && (!isblock || !selected[c.object])) continue;
    check_header(f, c);
    read_payload(f, c, buf);
    hash_check(sha(buf.data(), buf.size()), c.sha, ty + " payload");
    if (isblock) {
      Elem& el = *M.el[c.object];
      bool rep = el.kind == "rep";
      (rep ? payload_body : payload_tower) += c.len;
      size_t cursor = 0;
      for (Uint64 j = 0; j < c.leaf_count; ++j) {
        const Logical& l = C.logical[c.first_leaf + j];
        ElemAcc& a = acc[c.object];
        bool dp = deep[c.object];
        Uint64 first = l.first;
        vector<int>& lv = el.levels;
        size_t nl = lv.size();
        if (dp) {
          enc.start(c.object, first, l.count);
          fmpz_set(u, el.scale);
        }
        Uint64 cnt_nz = 0;
        const bool rsc = true;
        const int olv = own_lv[c.object];
        const Uint64 ow = own_w[c.object], vu = v2u[c.object];
        auto sink = [&](Uint64 s, const fmpz_t x, const fmpz_t y) {
          cnt_nz++;
          a.zero = false;
          if (a.nfound < (int)nl) {
            Uint64 ss = s;
            for (size_t j = 0; j < nl; j++) {
              Uint64 p = M.deg[lv[j]];
              if (ss % p && !a.found[j]) {
                a.found[j] = 1;
                a.nfound++;
              }
              ss /= p;
            }
          }
          if (rsc) {
            // leaf lambda = scale * (x + y omega), x in the original basis; v = min v_2 over its
            // nonzero coordinates
            Uint64 vc = UINT64_MAX;
            if (!fmpz_is_zero(x)) vc = fmpz_val2(x);
            if (!fmpz_is_zero(y)) vc = std::min<Uint64>(vc, fmpz_val2(y));
            Uint64 v = checked_add(vc, vu);
            // weight vector: digits e_i of the full leaf index at the levels of the level set
            // (others are 0), plus own weight
            Uint64 ss = s, e = 0;
            for (size_t j = 0; j < nl; j++) {
              Uint64 p = M.deg[lv[j]];
              Uint64 dgt = ss % p;
              ss /= p;
              e += dgt * C.rs_N[lv[j]];
              C.rs_gain[lv[j]] = checked_add(C.rs_gain[lv[j]], dgt);
            }
            if (olv >= 0) C.rs_gain[olv] = checked_add(C.rs_gain[olv], ow);
            Uint64 key = e | (Uint64)(olv + 1) << 40 | ow << 46;
            auto it = C.rs_minv.find(key);
            if (it == C.rs_minv.end()) {
              admit_rescaling_key(C.rs_minv.size());
              C.rs_minv.emplace(key, v);
            } else if (v < it->second)
              it->second = v;
            C.rs_leaves++;
          }
          if (dp) {
            fmpz_gcd(a.g, a.g, x);
            fmpz_gcd(a.g, a.g, y);
            fmpz_t tt;
            fmpz_init(tt);
            fmpz_mul(tt, x, u);
            a.maxbits = std::max<Uint64>(a.maxbits, fmpz_bits(tt));
            fmpz_mul(tt, y, u);
            a.maxbits = std::max<Uint64>(a.maxbits, fmpz_bits(tt));
            fmpz_clear(tt);
            if (a.maxbits > el.max_bits) reject("leaf exceeds max_bits");
            enc.nonzero(s - first, x, y, M.D0);
          }
        };
        size_t consumed = 0;
        parse_block(buf.data() + cursor, buf.size() - cursor, c.object, first, l.count, M.D0, true,
                    sink, nullptr, &consumed);
        hash_check(logical_hash(l, buf.data() + cursor, consumed), l.sha, "logical leaf");
        Chunk view = c;
        view.off = c.off + cursor;
        view.len = consumed;
        view.first = l.first;
        view.count = l.count;
        view.sha = sha(buf.data() + cursor, consumed);
        el.chunks.push_back(C.leaf_chunks.size());
        C.leaf_chunks.push_back(view);
        ncoord_total += l.count;
        nzero_total += cnt_nz;
        if (dp) {
          enc.finish(l.count);
          nreenc++;
          if (enc.out.size() != consumed ||
              memcmp(enc.out.data(), buf.data() + cursor, consumed) != 0)
            reject("re-encoding block " + std::to_string(i) + " does not reproduce its bytes");
        }

        cursor += consumed;
      }
      if (cursor != buf.size()) reject("trailing/unused physical bytes");
      continue;
    }
    if (ty == "BALL") {
      Uint64 ray = nray++;
      if (ray >= M.rays.size()) reject("BALL chunk count != number of rays");
      if (c.object != ray || c.first != 0 || c.count != M.d)
        reject("BALL PDIR object/first/count wrong");
      Uint64 rec = 2 * (32 + 8 * M.limbs);
      if (c.len != M.d * rec) reject("BALL chunk has wrong length");
      read_payload(f, c, buf);
      hash_check(sha(buf.data(), buf.size()), c.sha, "BALL chunk " + std::to_string(i));
      C.ball_chunks.push_back(i);
      BallRay br;
      br.w.resize(M.d);
      Int64 worst = INT64_MAX;
      BallPart bp[2];
      for (Uint64 j = 0; j < M.d; j++) {
        for (int part = 0; part < 2; part++) {
          parse_ball_part(&buf[j * rec + part * (32 + 8 * M.limbs)], M.limbs, bp[part]);
          if (bp[part].m) {
            Uint64 m = bp[part].m;
            int bl = 64 - __builtin_clzll(m);
            Int64 a = -(bp[part].e + bl);
            worst = std::min(worst, a);
          }
          ball_to_arb(part ? acb_imagref(br.w[j].v) : acb_realref(br.w[j].v), bp[part]);
        }
      }
      br.worst_acc = worst;
      const RayRec& R = M.rays[ray];
      if (worst == INT64_MAX)
        reject("ray " + R.label + ": all radii zero (tight accuracy_bits impossible)");
      if (worst != R.accuracy_bits)
        reject("ray " + R.label + ": accuracy_bits " + std::to_string(R.accuracy_bits) +
               " is not the tight value " + std::to_string(worst) + " (spec 5.1a)");
      const ACB& wa = br.w[R.anchor];
      if (!arb_is_zero(acb_imagref(wa.v)) || !arb_is_exact(acb_imagref(wa.v)))
        reject("ray " + R.label + ": anchor imaginary part is not exactly 0 0 0 0");
      if (!arb_is_positive(acb_realref(wa.v)))
        reject("ray " + R.label + ": anchor real part not positive");
      Real sum, tmp;
      for (Uint64 j = 0; j < M.d; j++) {
        acb_abs(tmp.v, br.w[j].v, M.working_bits + 64);
        arb_mul(tmp.v, tmp.v, tmp.v, M.working_bits + 64);
        arb_add(sum.v, sum.v, tmp.v, M.working_bits + 64);
      }
      if (!arb_contains_si(sum.v, 1)) reject("ray " + R.label + ": sum |w_j|^2 does not contain 1");
      char* ss = arb_get_str(sum.v, 22, ARB_STR_NO_RADIUS);
      say("  ray %-4s anchor %-4llu accuracy_bits stored %lld, tight %lld; sum|w|^2 = %s\n",
          R.label.c_str(), (unsigned long long)R.anchor, (long long)R.accuracy_bits,
          (long long)worst, ss);
      flint_free(ss);
      C.balls.push_back(std::move(br));
      continue;
    }

    if (ty == "PREC" || ty == "PROV") {
      Jv pj = parse_canonical(buf.data(), buf.size(), ty.c_str());
      if (pj.t != 'o') reject(ty + " must be an object");
      if (ty == "PROV") {
        if (pj.get("generator_rescaling")) reject("PROV duplicates EXMT rescaling");
        C.has_prov = true;
        C.prov = std::move(pj);
      } else
        for (const auto& kv : pj.o) {
          if (kv.first != "branch_bits" && kv.first != "base_path_bits" &&
              kv.first != "delta_bits" && kv.first != "max_element_bits")
            reject("unknown PREC key");
          if (kv.second.t != 's') reject("PREC value must be integer string");
          to_u64(kv.second.s, "PREC hint");
        }
    }
  }
  if (O.partial_elements.empty() && nray != M.rays.size()) reject("missing BALL chunks");
  Uint64 nprim = 0, nmb = 0;
  for (size_t e = 0; e < M.el.size(); e++) {
    if (!selected[e]) continue;
    Elem& el = *M.el[e];
    ElemAcc& a = acc[e];
    for (size_t j = 0; j < el.levels.size(); j++)
      if (!a.found[j])
        reject("element " + std::to_string(e) + ": level set not minimal (level " +
               std::to_string(el.levels[j]) + " never a nonzero digit)");
    if (deep[e]) {
      if (a.zero) {
        if (!fmpz_is_one(el.scale)) reject("zero element with scale != 1");
      } else if (!fmpz_is_one(a.g))
        reject("element " + std::to_string(e) + ": stored coordinates not primitive (gcd != 1)");
      else
        nprim++;
      if (a.maxbits != el.max_bits)
        reject("element " + std::to_string(e) + ": max_bits not attained / exceeded (declared " +
               std::to_string(el.max_bits) + ", observed " + std::to_string(a.maxbits) + ")");
      nmb++;
    }
  }

  say("  leaves re-encoded %llu; element canonicality checked for %llu elements; coordinates "
      "%llu\n",
      (unsigned long long)nreenc, (unsigned long long)nmb, (unsigned long long)ncoord_total);
  if (!O.partial_elements.empty()) return;
  Sha256 whole;
  vector<Uint8> window(1u << 20);
  for (Uint64 off = 0; off < f.size;) {
    size_t n = std::min<Uint64>(window.size(), f.size - off);
    f.read(off, window.data(), n);
    whole.update(window.data(), n);
    off += n;
  }
  whole.final(C.filesha.data());
  expected_hash(O.expected_sha, C.filesha, "whole-file SHA-256");
  say("  whole-file SHA-256 %s (%s)\n", hex(C.filesha).c_str(),
      O.expected_sha.empty() ? "recomputed, untrusted" : "trusted match");
}

vector<Uint64> rescaling_rule(const Container& C) {
  const int t = C.M.t();
  vector<Uint64> J(t, 0);
  vector<int> order;
  for (int s = 0; s < t; s++)
    if (C.rs_gain[s] > 0) order.push_back(s);
  std::stable_sort(order.begin(), order.end(),
                   [&](int a, int b) { return C.rs_gain[a] > C.rs_gain[b]; });
  for (int s : order) {
    Uint64 best = UINT64_MAX;
    for (auto& kv : C.rs_minv) {
      const Uint64 k = kv.first, e = k & ((1ull << 40) - 1);
      const int ol = (int)((k >> 40) & 63) - 1;
      const Uint64 ow = k >> 46;
      auto w = [&](int i) -> Uint64 { return i == ol ? ow : (e / C.rs_N[i]) % C.M.deg[i]; };
      Uint64 as = w(s);
      if (!as) continue;
      Uint64 dot = 0;
      for (int i = 0; i < t; i++) dot = checked_add(dot, checked_mul(w(i), J[i]));
      if (dot > kv.second) reject("internal: rescaling rule lost admissibility");
      best = std::min(best, (kv.second - dot) / as);
      if (best == 0) break;
    }
    if (best == UINT64_MAX)
      reject("internal: level with positive gain but no leaf of positive weight");
    J[s] = best;
  }
  return J;
}
}  // namespace sic_check::verifier
