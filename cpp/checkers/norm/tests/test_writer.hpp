// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <functional>
#include <sstream>

#include "../src/format/container.hpp"
#include "../src/model/input_model.hpp"
namespace fixture {

using sic_check::norm::ByteBuffer;

using sic_check::norm::Hash;

void put(ByteBuffer& b, uint64_t x, unsigned n = 8);

void overwrite(ByteBuffer& b, size_t at, uint64_t x, unsigned n = 8);

ByteBuffer bytes(const std::string& s);

void append(ByteBuffer& a, const ByteBuffer& b);

Hash hash(const ByteBuffer& b);

Hash tree(std::vector<ByteBuffer> items);

struct Node {
  enum Type { text, list, map } type = text;
  std::string value;
  std::vector<Node> array;
  std::map<std::string, Node> object;
  Node() = default;

  Node(const char* s);

  Node(std::string s);

  static Node a(std::vector<Node> values = {});

  static Node o(std::map<std::string, Node> values = {});

  Node& operator[](const std::string& key);

  std::string dump() const;
};

Node num(uint64_t n);

void var(ByteBuffer& b, uint64_t x);

void integer(ByteBuffer& b, int64_t x);

struct Leaf {
  std::string kind;
  uint64_t id = 0, first = 0, count = 1;
  ByteBuffer payload{};
};

ByteBuffer leaf_bytes(uint64_t id, uint64_t first,
                      const std::vector<std::pair<int64_t, int64_t>>& pairs);

struct Physical {
  std::string type;
  uint64_t flags = 1, codec = 0, reserved = 0, first_leaf = 0, leaf_count = 0, object = 0,
           first = 0, count = 0;
  ByteBuffer payload{};
};

struct Document {
  uint64_t d;
  Node exmt, rays, prov = Node::o(), prec = Node::o();
  std::vector<Leaf> leaves;
  std::vector<Physical> physical;
  ByteBuffer header{'S', 'I', 'C', 'X', 13, 10, 26, 10, 1, 0, 0, 0, 0, 0, 0, 0};
  std::function<void(std::vector<ByteBuffer>&)> edit_ldir, edit_pdir;
  std::function<void(ByteBuffer&)> edit_exmt;
  std::function<void(ByteBuffer&)> edit_rays;
  std::function<void(ByteBuffer&)> edit_prov;

  explicit Document(uint64_t dimension, bool finalized = true, unsigned levels = 1,
                    unsigned degree = 2);

  Physical& get(const std::string& type);

  void sync_leaves();

  ByteBuffer encode() const;
};
}  // namespace fixture
