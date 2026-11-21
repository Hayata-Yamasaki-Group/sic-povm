// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
namespace sic_check::norm::test {
using namespace sic_check::norm;
using fixture::Document;
using fixture::Node;
using fixture::num;
using fixture::overwrite;
void container_envelopes(TestContext& context, Document& original, ByteBuffer& valid) {
  for (const auto& [offset, expected] : std::vector<std::pair<size_t, std::string>>{
           {0, "REJECTED: SICX header magic"},
           {valid.size() - 1, "REJECTED: SICX footer magic/file length"}}) {
    auto corrupt = valid;
    corrupt[offset] ^= 1;
    write(context.scratch, corrupt);
    try {
      Reader reader(context.scratch.string(), {});
      throw std::runtime_error("corrupt envelope accepted");
    } catch (const InvalidInput& error) {
      check(error.what() == expected, "wrong envelope rejection: " + std::string(error.what()));
    }
  }
  for (auto [off, name] :
       std::vector<std::pair<size_t, std::string>>{{0, "header magic"},
                                                   {8, "major"},
                                                   {10, "minor"},
                                                   {12, "header features"},
                                                   {valid.size() - 1, "footer magic"},
                                                   {valid.size() - 16, "file length"},
                                                   {valid.size() - 80, "exact root"},
                                                   {valid.size() - 48, "file root"},
                                                   {32, "payload hash"},
                                                   {16, "header type mismatch"},
                                                   {20, "header flags mismatch"},
                                                   {24, "header length mismatch"}})
    context.raw(name, [=](auto& b) { b[off] ^= 1; });
  for (unsigned length : {0, 16, 64, 104, 111, 127})
    context.raw("truncation " + std::to_string(length), [=](auto& b) { b.resize(length); });
  context.raw("truncated last byte", [](auto& b) { b.pop_back(); });
  context.raw("trailing byte", [](auto& b) { b.push_back(0); });
  context.raw("footer offset overflow", [](auto& b) { overwrite(b, b.size() - 112, UINT64_MAX); });
  context.raw("footer directory overlap",
              [](auto& b) { overwrite(b, b.size() - 96, little(b, b.size() - 112)); });
  context.raw(
      "directory allocation", [](auto& b) { overwrite(b, b.size() - 104, UINT64_C(1) << 50); }, 1);
  for (const auto& type : {"LDIR", "PDIR"}) {
    bool logical = std::string(type) == "LDIR";
    for (auto [delta, name] : std::vector<std::pair<size_t, std::string>>{
             {0, "type"}, {4, "flags"}, {8, "length"}, {16, "count"}})
      context.raw(std::string(type) + " " + name, [=](auto& b) {
        size_t at = little(b, b.size() - 112 + (logical ? 0 : 16));
        b[at + delta] ^= 1;
      });
    context.raw(std::string(type) + " count zero", [=](auto& b) {
      overwrite(b, little(b, b.size() - 112 + (logical ? 0 : 16)) + 16, 0);
    });
    context.raw(std::string(type) + " count limit", [=](auto& b) {
      overwrite(b, little(b, b.size() - 112 + (logical ? 0 : 16)) + 16, (1u << 24) + 1);
    });
  }
  for (const auto& type : {"JUNK", "META", "INDX", "LDIR", "PDIR", "\x01XYZ"})
    for (unsigned flags : {0, 1})
      context.mutation(std::string("closed grammar ") + type + std::to_string(flags), [=](auto& d) {
        d.get("PROV").type = type;
        d.physical[d.physical.size() - 4].flags = flags;
      });
  for (const auto& type : {"EXMT", "TOWR", "BODY", "RAYS", "BALL", "PROV", "PREC"}) {
    context.mutation(std::string("wrong flags ") + type, [=](auto& d) { d.get(type).flags ^= 1; });
    context.mutation(std::string("reserved flags ") + type,
                     [=](auto& d) { d.get(type).flags |= 2; });
    context.mutation(std::string("unknown codec ") + type, [=](auto& d) { d.get(type).codec = 1; });
  }
  context.mutation("EXMT not first", [](auto& d) { std::swap(d.physical[0], d.physical[1]); });
  for (const auto& type : {"EXMT", "PROV", "RAYS", "PREC", "BALL"})
    context.mutation(std::string("duplicate ") + type, [=](auto& d) {
      auto c = d.get(type);
      d.physical.push_back(c);
    });
  context.mutation("PROV before elements",
                   [](auto& d) { std::swap(d.physical[1], d.get("PROV")); });
  context.mutation("element after PROV", [](auto& d) {
    auto c = d.physical[1];
    d.physical.push_back(c);
  });
  context.mutation("BALL without RAYS", [](auto& d) {
    std::erase_if(d.physical, [](auto& c) { return c.type == "RAYS"; });
  });
  context.mutation("PREC without RAYS", [](auto& d) {
    std::erase_if(d.physical, [](auto& c) { return c.type == "RAYS" || c.type == "BALL"; });
  });
  context.mutation("PREC before BALL", [](auto& d) { std::swap(d.get("PREC"), d.get("BALL")); });
  context.mutation("missing BALL", [](auto& d) {
    std::erase_if(d.physical, [](auto& c) { return c.type == "BALL"; });
  });
  for (auto [offset, value] : std::vector<std::pair<size_t, uint64_t>>{{8, 1}, {16, 1}, {24, 2}}) {
    auto d = original;
    auto meta = fixture::bytes(d.exmt.dump());
    d.edit_ldir = [=](auto& entries) {
      overwrite(entries[0], offset, value);
      ByteBuffer message = fixture::bytes(std::string("SICX1-LEAF\0", 11));
      fixture::append(message, fixture::bytes("EXMT"));
      fixture::put(message, offset == 8 ? value : 0);
      fixture::put(message, offset == 16 ? value : 0);
      fixture::put(message, offset == 24 ? value : 1);
      fixture::put(message, meta.size());
      fixture::append(message, meta);
      auto h = fixture::hash(message);
      std::copy(h.begin(), h.end(), entries[0].begin() + 32);
    };
    context.run(d.encode(), "rehashed EXMT noncanonical logical mapping", 1);
  }
  auto shifted_leaf = original;
  shifted_leaf.leaves[0].first = 1;
  shifted_leaf.leaves[0].payload = fixture::leaf_bytes(0, 1, {{1, 0}});
  shifted_leaf.sync_leaves();
  shifted_leaf.physical[1].first = 1;
  context.run(shifted_leaf.encode(), "consistent noncanonical logical boundary", 1);
}

void partial_directory_refusals(TestContext& context, uint64_t& base_dimension, Document& original,
                                Document& empty_chunk, fixture::Physical& empty) {
  empty_chunk.physical.insert(empty_chunk.physical.begin() + 1, empty);
  context.run(empty_chunk.encode(), "empty physical leaf ownership", 1);
  context.ldir("EXMT logical digest only", 0, 32, 0);
  auto tail_leaf = original;
  auto extra_leaf = tail_leaf.leaves.back();
  extra_leaf.first = 1;
  extra_leaf.payload = fixture::leaf_bytes(extra_leaf.id, 1, {{1, 0}});
  tail_leaf.leaves.push_back(extra_leaf);
  tail_leaf.physical.insert(
      tail_leaf.physical.end() - 4,
      {"BODY", 1, 0, 0, tail_leaf.leaves.size(), 1, extra_leaf.id, 1, 1, extra_leaf.payload});
  context.partial_bad(tail_leaf, "partial rejects extra logical leaf");
  auto missing_tail = original;
  missing_tail.physical.erase(missing_tail.physical.end() - 4, missing_tail.physical.end());
  missing_tail.physical.pop_back();
  context.partial_bad(missing_tail, "partial rejects incomplete exact coverage");
  auto misplaced_ball = original;
  misplaced_ball.get("BALL").object = 1;
  context.partial_bad(misplaced_ball, "partial rejects BALL ray index");
  auto duplicate_rays = original;
  duplicate_rays.physical.insert(duplicate_rays.physical.end() - 2, duplicate_rays.get("RAYS"));
  context.partial_bad(duplicate_rays, "partial rejects duplicate RAYS");
  auto offset_gap = original;
  offset_gap.edit_pdir = [](auto& entries) { overwrite(entries[1], 8, 0); };
  context.partial_bad(offset_gap, "partial rejects unconsumed physical offset");
  auto nonsequence = original;
  nonsequence.exmt["tower"]["branch_bits"] = "192";
  context.partial_bad(nonsequence, "partial rejects nonsequence branch precision");
  Document composite(base_dimension, false, 1, 4);
  context.run(composite.encode(), "coherent composite tower degree", 1);
  Document small_smith(base_dimension, false, 2);
  small_smith.exmt["galois"]["smith"] = Node::a({"2"});
  context.run(small_smith.encode(), "coherent deficient Smith product", 1);
  auto absent_elements = original;
  absent_elements.exmt["elements"] = Node::a();
  context.run(absent_elements.encode(), "empty element descriptor array", 1, {}, "missing element");
  // A wrapped product can pass the final product equality. This partial
  // fixture admits a large degree but loads no dense field coordinates.
  Document wrapped_smith(4, false, 33);
  wrapped_smith.exmt["galois"]["smith"] =
      Node::a({"4294967295", "4294967297", "4294967295", "4294967297", "8589934592"});
  wrapped_smith.exmt["galois"]["unit_generator_words"] =
      Node::a({Node::a({"0", "0", "0", "0", "0"})});
  auto wrapped_bytes = wrapped_smith.encode();
  ReadOptions wrapped_options;
  wrapped_options.partial = true;
  wrapped_options.max_field_dimension = UINT64_C(1) << 33;
  wrapped_options.exact_root = root(wrapped_bytes, 32);
  context.run(wrapped_bytes, "overflowing Smith product with matching wrapped result", 1,
              wrapped_options);
  context.mutation("extra entry representative",
                   [](auto& d) { d.exmt["entry_rule"]["representatives"].array.push_back("99"); });
  context.mutation("branch has ignored third component", [](auto& d) {
    d.exmt["tower"]["branches"].array[0].array.push_back("0 0 0 0");
  });
  context.mutation("scale trailing whitespace",
                   [](auto& d) { d.exmt["elements"].array[0]["scale"] = "1 "; });
  auto short_ball = original;
  short_ball.get("BALL").payload.resize(short_ball.get("BALL").payload.size() - 40);
  context.run(short_ball.encode(), "truncated whole BALL part", 1, {}, "BALL geometry/order");
  auto empty_run = original;
  empty_run.leaves[0].payload = ByteBuffer{0, 0, 1, 0};
  empty_run.sync_leaves();
  context.run(empty_run.encode(), "empty run rejected before advancing", 1, {},
              "run length/alternation/coverage");
  for (bool flag : {false, true}) {
    auto tiny_part = original;
    auto& payload = tiny_part.get("BALL").payload;
    overwrite(payload, 80, flag ? 2 : 0);
    overwrite(payload, 88, uint64_t(flag ? -512 : -(INT64_C(1) << 40) - 1));
    overwrite(payload, 112, 1);
    context.run(tiny_part.encode(), "nonanchor tiny BALL flags/exponent", 1);
  }
  context.ldir("LDIR reserved", 0, 4, 1, 4);
  context.ldir("LDIR kind", 1, 0, 0, 4);
  context.ldir("EXMT element", 0, 8, 1);
  context.ldir("EXMT first", 0, 16, 1);
  context.ldir("EXMT count", 0, 24, 2);
  context.ldir("logical zero count", 1, 24, 0);
  context.ldir("logical element", 1, 8, 1);
  context.ldir("logical first", 1, 16, 1);
  context.ldir("logical boundary", 1, 24, 2);
  context.ldir("logical digest", 1, 32, 0);
  context.mutation("missing logical leaf",
                   [](auto& d) { d.edit_ldir = [](auto& e) { e.pop_back(); }; });
  context.mutation("extra logical leaf",
                   [](auto& d) { d.edit_ldir = [](auto& e) { e.push_back(e.back()); }; });
  context.mutation("logical reordering",
                   [](auto& d) { d.edit_ldir = [](auto& e) { std::swap(e[1], e[2]); }; });
  context.pdir("physical reserved", 0, 28, 1, 4);
  context.pdir("raw decoded length", 0, 32, 1);
  context.pdir("first offset", 0, 8, 17);
  context.pdir("physical gap", 1, 8, 0);
  context.pdir("physical overlap", 1, 8, 16);
  context.pdir("physical overflow", 0, 16, UINT64_MAX);
  context.pdir("physical flags/header mismatch", 0, 4, 0, 4);
  context.pdir("physical type/header mismatch", 0, 0, 0, 4);
  context.pdir("physical digest", 0, 80, 0);
  context.pdir("EXMT first_leaf", 0, 40, 1);
  context.pdir("EXMT leaf_count", 0, 48, 2);
  context.pdir("EXMT object", 0, 56, 1);
  context.pdir("EXMT coordinate count", 0, 72, 1);
  context.pdir("element empty coverage", 1, 48, 0);
  context.pdir("element range overflow", 1, 48, UINT64_MAX);
  context.pdir("duplicate coverage", 2, 40, 1);
  context.pdir("element wrong object", 1, 56, 1);
  context.pdir("element wrong first", 1, 64, 1);
  context.pdir("element wrong count", 1, 72, 2);
  context.pdir("element wrong kind", 1, 0, 0, 4);
  context.pdir("nonleaf logical ownership", original.physical.size() - 4, 48, 1);
  context.pdir("BALL wrong object", original.physical.size() - 2, 56, 1);
  context.pdir("BALL wrong first", original.physical.size() - 2, 64, 1);
  context.pdir("BALL wrong count", original.physical.size() - 2, 72, 3);
  context.pdir("optional wrong object", original.physical.size() - 1, 56, 1);
  context.mutation("missing physical entry",
                   [](auto& d) { d.edit_pdir = [](auto& e) { e.pop_back(); }; });
  context.mutation("extra physical entry",
                   [](auto& d) { d.edit_pdir = [](auto& e) { e.push_back(e.back()); }; });
}

void whole_hash_mismatch(TestContext& context, ByteBuffer& valid, ReadOptions& wrong) {
  context.run(valid, "expected whole mismatch", 1, wrong);
}

void authenticated_partial_input(TestContext& context, std::filesystem::path& scratch,
                                 Document& original, ByteBuffer& valid, fixture::Physical& empty,
                                 ReadOptions& partial) {
  partial.partial = true;
  partial.exact_root = root(valid, 32);
  partial.elements = {1};
  context.run(valid, "authenticated partial element", 0, partial);
  write(scratch, valid);
  Reader pr(scratch.string(), partial);
  check(!pr.report().full && !pr.report().trusted_file && !pr.report().branches_checked &&
            !pr.report().rescaling_checked,
        "partial scope flags");
  check(pr.report().checked_elements == std::vector<size_t>({0, 1}), "EXMT-derived dependencies");
  try {
    (void)pr.element_leaves(2);
    throw std::runtime_error("unread element exposed");
  } catch (const InvalidInput&) {
  }
  auto partial_hash = partial;
  partial_hash.whole_sha256 = digest(valid);
  partial_hash.exact_root.reset();
  context.run(valid, "partial semantic verification with complete trusted byte scan", 0,
              partial_hash);
  write(scratch, valid);
  Reader scanned_partial(scratch.string(), partial_hash);
  check(!scanned_partial.report().full && scanned_partial.report().whole_hash_checked &&
            scanned_partial.report().exact_trust == TrustRoute::whole_sha256 &&
            scanned_partial.report().file_trust == TrustRoute::whole_sha256,
        "partial whole-hash scan scope");
  auto requested_bad = partial;
  requested_bad.elements = {999};
  context.run(valid, "partial element range", 1, requested_bad);
  requested_bad = partial;
  requested_bad.physical_chunks = {999};
  context.run(valid, "partial chunk range", 1, requested_bad);
  auto all_exact = partial;
  all_exact.elements.clear();
  for (size_t id = 0; id < original.leaves.size(); ++id) all_exact.elements.push_back(id);
  context.run(valid, "partial all exact leaves finishes global checks", 0, all_exact);
  write(scratch, valid);
  Reader exact_partial(scratch.string(), all_exact);
  check(!exact_partial.report().full && exact_partial.report().branches_checked &&
            exact_partial.report().rescaling_checked && exact_partial.meta().rays.empty(),
        "partial all-exact scope");
  auto bad_global = original;
  bad_global.exmt["elements"].array[0]["scale"] = "4";
  bad_global.exmt["elements"].array[0]["max_bits"] = "3";
  auto bad_global_bytes = bad_global.encode();
  all_exact.exact_root = root(bad_global_bytes, 32);
  context.run(bad_global_bytes, "partial all exact leaves rejects residual rescaling", 1,
              all_exact);
  bad_global = original;
  bad_global.exmt["tower"]["branch_bits"] = "256";
  bad_global_bytes = bad_global.encode();
  all_exact.exact_root = root(bad_global_bytes, 32);
  context.run(bad_global_bytes, "partial all exact leaves rejects branch minimum", 1, all_exact);
  auto no_trust = partial;
  no_trust.exact_root.reset();
  context.run(valid, "partial needs trust", 1, no_trust);
  auto unread = valid;
  size_t ball_at = 0;
  for (size_t at = 16; at + 16 < valid.size(); at += 16 + size_t(little(valid, at + 8)))
    if (std::string(reinterpret_cast<const char*>(valid.data() + at), 4) == "BALL") {
      ball_at = at;
      break;
    }
  check(ball_at, "test BALL offset");
  unread[ball_at + 32] ^= 2;
  context.run(unread, "partial unread BALL stays unchecked", 0, partial);
  context.run(unread, "full corrupt BALL", 1);
  auto unread_header = valid;
  unread_header[ball_at] = 'X';
  context.run(unread_header, "partial defers unread BALL header", 0, partial);
  context.run(unread_header, "full rejects unread BALL header", 1, {},
              "physical header disagreement");
  auto pb = partial;
  pb.physical_chunks = {original.physical.size() - 2};
  context.run(valid, "partial BALL needs file trust", 1, pb);
  pb.file_root = root(valid, 64);
  context.run(valid, "partial BALL file authentication", 0, pb);
  context.run(unread, "partial read corrupt BALL", 1, pb);
  context.run(unread_header, "partial consumed BALL header disagreement", 1, pb,
              "physical header disagreement");
  pb.exact_root.reset();
  context.run(valid, "partial BALL with file-root trust alone", 0, pb);
  auto transitive_wrong = pb;
  transitive_wrong.exact_root = root(valid, 32);
  (*transitive_wrong.exact_root)[0] ^= 1;
  context.run(valid, "direct exact mismatch despite matching file trust", 1, transitive_wrong);
  auto directory_bad = original;
  directory_bad.edit_pdir = [=](auto& entries) {
    overwrite(entries.at(original.physical.size() - 2), 4, 0, 4);
  };
  auto directory_bad_bytes = directory_bad.encode();
  context.run(directory_bad_bytes, "partial still rejects unread directory flag", 1, partial);
  auto rays_request = pb;
  rays_request.physical_chunks = {original.physical.size() - 3};
  context.run(unread, "reading RAYS defers unused corrupt BALL", 0, rays_request);
  auto bad_rays = original;
  bad_rays.rays["rays"].array.push_back(bad_rays.rays["rays"].array[0]);
  bad_rays.rays["rays"].array.back()["label"] = "c1";
  bad_rays.rays["rays"].array.back()["word"].array[0] = "1";
  auto bad_rays_bytes = bad_rays.encode();
  context.run(bad_rays_bytes, "partial defers unread RAYS count mismatch", 0, partial);
  rays_request.file_root = root(bad_rays_bytes, 64);
  context.run(bad_rays_bytes, "consumed RAYS checks total BALL count", 1, rays_request,
              "RAYS/BALL count");
  bad_rays = original;
  bad_rays.rays["balls"]["limbs"] = "2";
  bad_rays_bytes = bad_rays.encode();
  context.run(bad_rays_bytes, "partial defers unread RAYS width mismatch", 0, partial);
  rays_request.file_root = root(bad_rays_bytes, 64);
  context.run(bad_rays_bytes, "consumed RAYS checks declared BALL widths", 1, rays_request,
              "BALL declared length");
  auto bad_exmt_header = valid;
  bad_exmt_header[16] = 'X';
  context.run(bad_exmt_header, "partial always checks EXMT header", 1, partial,
              "physical header disagreement");
  auto bad_body_header = valid;
  size_t body_at = 16;
  for (size_t index = 0; index < 3; ++index) body_at += 16 + size_t(little(valid, body_at + 8));
  bad_body_header[body_at] = 'X';  // Element 2 is outside partial.elements = {1}.
  context.run(bad_body_header, "partial defers unread BODY header", 0, partial);
  auto body_request = partial;
  body_request.elements = {2};
  context.run(bad_body_header, "partial consumed BODY header disagreement", 1, body_request,
              "physical header disagreement");
  auto bad_element = valid;
  size_t eo = size_t(little(valid, 24)) + 48;  // first element payload follows EXMT
  bad_element[eo] ^= 1;
  context.run(bad_element, "partial checked element corruption", 1, partial);
  auto constrained = ReadOptions{};
  constrained.max_directory_bytes = 8;
  context.run(valid, "directory resource refusal", 2, constrained);
  constrained = ReadOptions{};
  constrained.max_dense_coordinates = 1;
  context.run(valid, "dense resource refusal", 2, constrained);
  // A negative discriminant, imaginary selected root, and balanced negative y.
  Document imaginary(2);
  imaginary.exmt["tower"]["branches"].array[0] = Node::a({"0 0 0 0", "1 0 0 0"});
  imaginary.leaves[0].payload = fixture::leaf_bytes(0, 0, {{-1, 0}});
  imaginary.leaves[1].payload = fixture::leaf_bytes(1, 0, {{-1, -1}});
  imaginary.sync_leaves();
  context.run(imaginary.encode(), "d2 imaginary root and balanced pair");
  write(scratch, imaginary.encode());
  Reader ir(scratch.string());
  check(ir.meta().D0 == -3 && fmpz_is_zero(ir.element_leaves(1)[0].x.v) &&
            fmpz_equal_si(ir.element_leaves(1)[0].y.v, -1),
        "d2 pair recovery");
  // All literal positive-D0 pair-transform examples from section 4.4.
  // A second coordinate (1,0) makes the element primitive without changing the vector.
  for (auto [x, y, rvalue] : std::vector<std::tuple<int64_t, int64_t, int64_t>>{
           {0, 4, -2}, {2, -2, 4}, {8, -8, 13}, {0, -1, 1}, {0, -8, 5}, {-2, 2, -3}}) {
    Document pair(4);
    pair.leaves[1].payload = fixture::leaf_bytes(1, 0, {{rvalue, y}, {1, 0}});
    pair.leaves[1].count = 2;
    pair.physical[2].count = 2;
    pair.exmt["elements"].array[1]["levels"] = Node::a({"0"});
    uint64_t maximum = uint64_t(std::max(x < 0 ? -x : x, y < 0 ? -y : y));
    pair.exmt["elements"].array[1]["max_bits"] = num(std::bit_width(maximum));
    pair.sync_leaves();
    context.run(pair.encode(), "positive pair vector");
    write(scratch, pair.encode());
    Reader check_pair(scratch.string());
    check(fmpz_equal_si(check_pair.element_leaves(1)[0].x.v, x) &&
              fmpz_equal_si(check_pair.element_leaves(1)[0].y.v, y),
          "pair vector decoded incorrectly");
  }
}
}  // namespace sic_check::norm::test
