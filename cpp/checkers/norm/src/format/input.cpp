// SPDX-License-Identifier: AGPL-3.0-or-later
#include "input.hpp"

#include "../runtime/diagnostics.hpp"
#include "canonical.hpp"
#include "canonical_json.hpp"
#include "container.hpp"
#include "integer_codec.hpp"
#include "metadata.hpp"
namespace sic_check::norm {
auto Reader::finish_elements() -> void {
  for (size_t id = 0; id < m.elements.size(); ++id) {
    const auto& e = m.elements[id];
    uint64_t total = 1;
    for (unsigned l : e.levels) total *= m.degrees[l];
    if (loaded_coordinates[id] != total) continue;
    complete[id] = true;
    Integer gcd, scale;
    fmpz_set_str(scale.v, e.scale.c_str(), 10);
    uint64_t attained = 0;
    std::set<unsigned> used;
    for (size_t full = 0; full < leaves[id].size(); ++full) {
      const auto& v = leaves[id][full];
      if (fmpz_is_zero(v.x.v) && fmpz_is_zero(v.y.v)) continue;
      attained = std::max<uint64_t>(attained, std::max(fmpz_bits(v.x.v), fmpz_bits(v.y.v)));
      fmpz_gcd(gcd.v, gcd.v, v.x.v);
      fmpz_gcd(gcd.v, gcd.v, v.y.v);
      size_t digits = full;
      for (unsigned l = 0; l < e.level; ++l) {
        if (digits % m.degrees[l]) used.insert(l);
        digits /= m.degrees[l];
      }
    }
    input_require(
        attained == e.max_bits && std::vector<unsigned>(used.begin(), used.end()) == e.levels,
        "attained max_bits/minimal level set");
    input_require((attained == 0 && e.scale == "1") || (attained && fmpz_equal(gcd.v, scale.v)),
                  "scale/primitivity/zero convention");
    verification.checked_elements.push_back(id);
  }
}

auto Reader::rescaling() -> void {
  struct Constraint {
    std::vector<uint64_t> weight;
    uint64_t valuation;
  };
  std::vector<Constraint> constraints;
  std::vector<uint64_t> gain(m.degrees.size(), 0);
  for (size_t id = 0; id < m.elements.size(); ++id) {
    const auto& e = m.elements[id];
    for (size_t full = 0; full < leaves[id].size(); ++full) {
      const auto& v = leaves[id][full];
      if (fmpz_is_zero(v.x.v) && fmpz_is_zero(v.y.v)) continue;
      Constraint c{std::vector<uint64_t>(m.degrees.size(), 0), UINT64_MAX};
      if (!fmpz_is_zero(v.x.v)) c.valuation = fmpz_val2(v.x.v);
      if (!fmpz_is_zero(v.y.v)) c.valuation = std::min<uint64_t>(c.valuation, fmpz_val2(v.y.v));
      size_t digits = full;
      for (unsigned l = 0; l < e.level; ++l) {
        c.weight[l] = digits % m.degrees[l];
        digits /= m.degrees[l];
      }
      if (e.kind != "rep") {
        uint64_t p = m.degrees[e.level], C = p * (p - 1) / 2;
        c.weight[e.level] = e.kind == "wsq" || e.kind == "act" ? 2
                            : e.kind == "fcoef"                ? p - uint64_t(e.k)
                            : e.kind == "actdelta"             ? C
                                                               : C + 1 - uint64_t(e.k);
      }
      for (size_t l = 0; l < gain.size(); ++l) gain[l] = add_checked(gain[l], c.weight[l]);
      constraints.push_back(std::move(c));
    }
  }
  std::vector<size_t> order(gain.size());
  for (size_t l = 0; l < gain.size(); ++l) order[l] = l;
  std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return gain[a] > gain[b]; });
  auto greedy = [&](bool undo) {
    std::vector<uint64_t> result(gain.size(), 0);
    for (auto l : order) {
      if (!gain[l]) continue;
      uint64_t best = UINT64_MAX;
      for (const auto& c : constraints)
        if (c.weight[l]) {
          uint64_t available = c.valuation;
          if (undo)
            for (size_t j = 0; j < gain.size(); ++j)
              available = add_checked(available, mul_checked(c.weight[j], m.shifts[j]));
          uint64_t spent = 0;
          for (size_t j = 0; j < gain.size(); ++j)
            spent = add_checked(spent, mul_checked(c.weight[j], result[j]));
          input_require(spent <= available, "rescaling feasibility");
          best = std::min(best, (available - spent) / c.weight[l]);
        }
      result[l] = best;
    }
    return result;
  };
  input_require(greedy(true) == m.shifts, "recorded rescaling greedy shifts");
  auto residual = greedy(false);
  input_require(std::all_of(residual.begin(), residual.end(), [](auto x) { return x == 0; }),
                "noncanonical residual rescaling");
  verification.rescaling_checked = true;
}

Reader::Reader(const std::string& path, ReadOptions requested) : options(std::move(requested)) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  input_require(bool(in), "cannot open file");
  auto endpos = in.tellg();
  input_require(endpos >= 128, "truncated file");
  uint64_t size = uint64_t(endpos);
  auto read = [&](uint64_t off, uint64_t n) {
    input_require(off <= size && n <= size - off && n <= SIZE_MAX &&
                      n <= uint64_t(std::numeric_limits<std::streamsize>::max()),
                  "source range");
    ByteBuffer b(size_t(n), 0);
    in.clear();
    in.seekg(std::streamoff(off));
    in.read(reinterpret_cast<char*>(b.data()), std::streamsize(n));
    input_require(bool(in), "missing/truncated source range");
    return b;
  };
  auto header = read(0, 16), footer = read(size - 112, 112);
  constexpr uint8_t kMagic[] = {83, 73, 67, 88, 13, 10, 26, 10};
  constexpr uint8_t kTail[] = {83, 73, 67, 88, 69, 78, 68, 26};
  input_require(std::equal(std::begin(kMagic), std::end(kMagic), header.begin()), "header magic");
  input_require(little(header, 8, 2) == 1 && !little(header, 10, 2) && !little(header, 12, 4),
                "version/required_features");
  input_require(std::equal(std::begin(kTail), std::end(kTail), footer.begin() + 104) &&
                    little(footer, 96) == size,
                "footer magic/file length");
  uint64_t lo = little(footer, 0), ll = little(footer, 8), po = little(footer, 16),
           pl = little(footer, 24);
  input_require(lo >= 16 && po == add_checked(add_checked(lo, 16), ll) &&
                    size == add_checked(add_checked(add_checked(po, 16), pl), 112),
                "directory geometry");
  resource(ll <= options.max_directory_bytes && pl <= options.max_directory_bytes,
           "directory bytes");
  auto directory = [&](uint64_t off, uint64_t len, const std::string& type, unsigned width) {
    input_require(len >= 8, "directory length");
    auto b = read(off, add_checked(16, len));
    input_require(type_at(b) == type && little(b, 4, 4) == 1 && little(b, 8) == len,
                  "directory header");
    uint64_t n = little(b, 16);
    input_require(n && n <= (1u << 24) && len == add_checked(8, mul_checked(width, n)),
                  "directory count");
    return b;
  };
  auto ld = directory(lo, ll, "LDIR", 64), pd = directory(po, pl, "PDIR", 112);
  std::vector<ByteBuffer> exact_entries, physical_entries;
  for (size_t at = 24; at < ld.size(); at += 64) {
    Logical l;
    l.kind = type_at(ld, at);
    input_require((l.kind == "EXMT" || element_type(l.kind)) && !little(ld, at + 4, 4),
                  "logical kind/reserved");
    l.element = little(ld, at + 8);
    l.first = little(ld, at + 16);
    l.count = little(ld, at + 24);
    input_require(l.count, "logical count zero");
    std::copy_n(ld.begin() + at + 32, 32, l.hash.begin());
    logical.push_back(std::move(l));
    exact_entries.emplace_back(ld.begin() + at, ld.begin() + at + 64);
  }
  uint64_t next = 16;
  for (size_t at = 24; at < pd.size(); at += 112) {
    Chunk c;
    c.type = type_at(pd, at);
    c.flags = little(pd, at + 4, 4);
    c.offset = little(pd, at + 8);
    c.length = little(pd, at + 16);
    input_require(!little(pd, at + 24, 4) && !little(pd, at + 28, 4), "codec/reserved");
    c.decoded_length = little(pd, at + 32);
    c.first_leaf = little(pd, at + 40);
    c.leaf_count = little(pd, at + 48);
    c.object = little(pd, at + 56);
    c.first = little(pd, at + 64);
    c.count = little(pd, at + 72);
    std::copy_n(pd.begin() + at + 80, 32, c.hash.begin());
    chunk_rules(c);
    input_require(c.offset == next, "physical contiguity");
    next = add_checked(add_checked(next, 16), c.length);
    input_require(next <= lo, "physical extent");
    chunks.push_back(std::move(c));
    physical_entries.emplace_back(pd.begin() + at, pd.begin() + at + 112);
  }
  input_require(next == lo && chunks.front().type == "EXMT", "physical extent/EXMT first");
  verification.exact_root = tree_hash(exact_entries, 0, exact_entries.size());
  ByteBuffer last{'S', 'I', 'C', 'X', '1', '-', 'L', 'D', 'I', 'R', 0};
  auto lhash = digest(ld);
  last.insert(last.end(), lhash.begin(), lhash.end());
  physical_entries.push_back(std::move(last));
  verification.file_root = tree_hash(physical_entries, 0, physical_entries.size());
  input_require(std::equal(verification.exact_root.begin(), verification.exact_root.end(),
                           footer.begin() + 32) &&
                    std::equal(verification.file_root.begin(), verification.file_root.end(),
                               footer.begin() + 64),
                "Merkle roots");
  if (options.exact_root) {
    input_require(*options.exact_root == verification.exact_root, "expected exact root mismatch");
    verification.trusted_exact = true;
    verification.exact_trust = TrustRoute::direct;
  }
  if (options.file_root) {
    input_require(*options.file_root == verification.file_root, "expected file root mismatch");
    verification.trusted_file = true;
    verification.file_trust = TrustRoute::direct;
    verification.trusted_exact = true;
    if (!options.exact_root) verification.exact_trust = TrustRoute::file_root;
  }
  if (options.whole_sha256) {
    HashState state;
    for (uint64_t off = 0; off < size;) {
      uint64_t n = std::min<uint64_t>(1u << 20, size - off);
      auto b = read(off, n);
      state.update(b);
      off += n;
    }
    input_require(state.finish() == *options.whole_sha256, "expected whole-file SHA-256 mismatch");
    verification.whole_hash_checked = verification.trusted_file = verification.trusted_exact = true;
    if (!options.exact_root) verification.exact_trust = TrustRoute::whole_sha256;
    if (!options.file_root) verification.file_trust = TrustRoute::whole_sha256;
  }
  input_require(!options.partial || verification.trusted_exact,
                "partial exact authentication needs trusted exact root");
  auto fetch = [&](Chunk& c) {
    // Ordinary headers are checked only when the chunk is consumed (section 6.2).
    auto h = read(c.offset, 16);
    input_require(type_at(h) == c.type && little(h, 4, 4) == c.flags && little(h, 8) == c.length,
                  "physical header disagreement");
    c.payload = read(c.offset + 16, c.length);
    input_require(digest(c.payload) == c.hash, "encoded payload SHA-256");
  };
  fetch(chunks[0]);
  input_require(logical_digest(logical[0], chunks[0].payload) == logical[0].hash,
                "EXMT logical SHA-256");
  metadata(json(chunks[0]));
  layout();
  coverage();
  leaves.resize(m.elements.size());
  loaded_coordinates.resize(m.elements.size());
  complete.resize(m.elements.size());
  std::vector<bool> wanted(chunks.size(), !options.partial),
      wanted_elements(m.elements.size(), false);
  wanted[0] = true;
  for (size_t id : options.elements) {
    input_require(id < m.elements.size(), "requested element range");
    wanted_elements[id] = true;
  }
  for (size_t index : options.physical_chunks) {
    input_require(index < chunks.size(), "requested physical chunk range");
    wanted[index] = true;
    if (!element_type(chunks[index].type) && index)
      input_require(verification.trusted_file, "non-exact authentication needs trusted file root");
  }
  // All defining/action elements below requested field levels are safe, EXMT-derived
  // dependencies.
  unsigned field_level = 0;
  for (size_t index : options.physical_chunks)
    if (element_type(chunks[index].type))
      field_level = std::max(field_level, m.elements[size_t(chunks[index].object)].level);
  for (size_t id = 0; id < wanted_elements.size(); ++id)
    if (wanted_elements[id]) field_level = std::max(field_level, m.elements[id].level);
  for (size_t id = 0; id < wanted_elements.size(); ++id)
    if (m.elements[id].kind != "rep" && m.elements[id].level < field_level)
      wanted_elements[id] = true;
  for (size_t i = 1; i < chunks.size(); ++i) {
    const auto& c = chunks[i];
    if (element_type(c.type) && wanted_elements[size_t(c.object)]) wanted[i] = true;
  }
  // Reading a BALL requires its RAYS interpretation and all earlier BALLs for ordered parsing.
  for (size_t i = 1; i < chunks.size(); ++i)
    if (wanted[i] && chunks[i].type == "BALL")
      for (size_t j = 1; j < i; ++j)
        if (chunks[j].type == "RAYS" || chunks[j].type == "BALL") wanted[j] = true;
  for (size_t i = 0; i < chunks.size(); ++i)
    if (wanted[i]) {
      auto& c = chunks[i];
      if (i) fetch(c);
      if (element_type(c.type))
        decode_element_chunk(c);
      else if (c.type == "RAYS")
        raydata(json(c));
      else if (c.type == "BALL")
        decode_ball(c);
      else if (c.type == "PROV") {
        auto j = json(c);
        input_require(j.kind == Json::object && !j.fields.contains("generator_rescaling"),
                      "PROV key/object");
      } else if (c.type == "PREC") {
        auto j = json(c);
        input_require(j.kind == Json::object, "PREC object");
        for (const auto& [key, value] : j.fields) {
          input_require(key == "branch_bits" || key == "base_path_bits" || key == "delta_bits" ||
                            key == "max_element_bits",
                        "PREC unknown key");
          (void)number(value);
        }
      }
      verification.checked_chunks.push_back(i);
      ByteBuffer{}.swap(c.payload);
    }
  if (!m.rays.empty()) {
    size_t actual_balls = 0;
    for (const auto& c : chunks)
      if (c.type == "BALL") {
        ++actual_balls;
        input_require(c.length == mul_checked(mul_checked(m.d, 2), 32 + 8 * m.limbs),
                      "BALL declared length");
      }
    input_require(actual_balls == m.rays.size(), "RAYS/BALL count");
  }
  finish_elements();

  bool all_exact = std::all_of(complete.begin(), complete.end(), [](bool value) { return value; });
  if (all_exact) {
    rescaling();
    verify_canonical_branches(m, leaves, options.algebraic_limits);
    verification.branches_checked = true;
  }
  if (!options.partial) {
    input_require(all_exact && balls.size() == m.rays.size(), "incomplete full verification");
    verification.full = true;
  }
}

auto Reader::report() const -> const Verification& { return verification; }

auto Reader::summary() const -> std::string {
  auto trust = [](TrustRoute route) {
    switch (route) {
      case TrustRoute::direct:
        return " (trusted identity matched directly)";
      case TrustRoute::file_root:
        return " (transitively trusted via file root and complete LDIR)";
      case TrustRoute::whole_sha256:
        return " (transitively trusted via whole-file SHA-256)";
      default:
        return " (self-consistency only)";
    }
  };
  return std::string(verification.full ? "FULL" : "PARTIAL") +
         " SICX input verification: PASS\nexact_root " + hash_hex(verification.exact_root) +
         trust(verification.exact_trust) + "\nfile_root " + hash_hex(verification.file_root) +
         trust(verification.file_trust) + "\nchecked physical chunks " +
         std::to_string(verification.checked_chunks.size()) + "/" + std::to_string(chunks.size()) +
         "; complete elements " + std::to_string(verification.checked_elements.size()) + "/" +
         std::to_string(m.elements.size()) +
         (verification.branches_checked ? "; rescaling and canonical branches checked"
                                        : "; global rescaling and branch canonicality unchecked") +
         (verification.full ? ""
                            : "; unread headers/payloads and RAYS-dependent checks unchecked") +
         "; optional BALL/exact ratio spot check not performed" +
         "; field/group/actions and representative kernel fixity remain premises" +
         (verification.whole_hash_checked ? "; trusted whole-file SHA-256 matched"
                                          : "; whole-file SHA-256 not checked");
}

auto Reader::meta() const -> const Meta& { return m; }

auto Reader::element_leaves(size_t id) const -> const std::vector<Pair>& {
  input_require(id < complete.size() && complete[id], "element not completely verified");
  return leaves[id];
}

auto Reader::ball_parts(size_t ray) const -> const std::vector<BallPart>& { return balls.at(ray); }
}  // namespace sic_check::norm
