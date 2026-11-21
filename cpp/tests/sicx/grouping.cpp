// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/block.hpp"
// Synthetic framing/control test. This is not a number-field or SIC certificate.
#include <iostream>

#include "sic/sicx/io.hpp"

int main(int argc, char** argv) {
  try {
    if (argc != 2) return 2;
    std::filesystem::create_directories(argv[1]);
    sic::sicx::Meta m;
    m.d = 2;
    m.D0 = -3;
    m.degrees.assign(18, 2);
    m.width = {1};
    for (auto p : m.degrees) m.width.push_back(m.width.back() * p);
    m.D = m.width.back();
    m.smith = {m.D};
    m.unit_generator_words = {};
    m.rescaling_shifts.assign(18, 0);
    arb_t root;
    arb_init(root);
    arb_set_ui(root, 3);
    arb_sqrt(root, root, 512);
    char* dump = arb_dump_str(root);
    m.branches.assign(18, {dump, "0 0 0 0"});
    flint_free(dump);
    arb_clear(root);
    for (unsigned i = 0; i < 18; ++i) {
      sic::sicx::ElementInfo e;
      e.kind = "wsq";
      e.level = i;
      e.scale = std::to_string(3 * m.width[i]);
      e.max_bits = i + 2;
      e.layout = sic::sicx::SubLayout(m.degrees, m.width, {});
      sic::sicx::derive_blocks(e, m.block_coordinates);
      m.elements.push_back(e);
    }
    for (unsigned r = 0; r < 2; ++r) {
      sic::sicx::ElementInfo e;
      e.kind = "rep";
      e.position = r;
      e.level = 18;
      e.max_bits = 1024;
      for (unsigned i = 0; i < 18; ++i) e.levels.push_back(i);
      e.layout = sic::sicx::SubLayout(m.degrees, m.width, e.levels);
      sic::sicx::derive_blocks(e, m.block_coordinates);
      m.elements.push_back(e);
    }
    sic::sicx::FmpzPair large, one;
    fmpz_one(one.x);
    fmpz_one(large.x);
    fmpz_mul_2exp(large.x, large.x, 1023);
    fmpz_add_ui(large.x, large.x, 1);
    sic::sicx::Digest exact{}, last_file{};
    unsigned different = 0;
    for (uint64_t mib : {16, 32, 64}) {
      const auto path = std::filesystem::path(argv[1]) / ("group-" + std::to_string(mib) + ".sicx");
      std::filesystem::remove(path);
      sic::sicx::StreamWriter writer(path, sic::sicx::StreamOptions{false, false, {}, false});
      const auto text = sic::support::dump_json(sic::sicx::meta_to_json(m));
      writer.chunk("EXMT", 1, {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
      sic::sicx::BlockEncoder encoder(m.D0);
      for (size_t id = 0; id < m.elements.size(); ++id) {
        const auto& e = m.elements[id];
        sic::sicx::ByteBuffer group;
        uint64_t first = 0, count = 0;
        auto flush = [&] {
          if (!group.empty())
            writer.chunk(e.kind == "rep" ? "BODY" : "TOWR", 1, group, id, first, count);
          group.clear();
          count = 0;
        };
        for (uint64_t start = 0; start < e.layout.coordinates(); start += e.block) {
          const auto leaf = encoder.encode(id, start, e.block, e.block, [&](size_t q) {
            const auto& value = e.kind == "rep" && start + q ? large : one;
            return std::tuple<uint64_t, const fmpz*, const fmpz*>(start + q, value.x, value.y);
          });
          if (!group.empty() && group.size() + leaf.size() > (mib << 20)) flush();
          if (group.empty()) first = start;
          group.insert(group.end(), leaf.begin(), leaf.end());
          count += e.block;
        }
        flush();
      }
      writer.finish();
      sic::sicx::Reader reader(path);
      if (mib == 16) exact = reader.exact_root();
      if (reader.exact_root() != exact) throw std::runtime_error("grouping changed exact root");
      if (mib != 16 && reader.root() != last_file) ++different;
      last_file = reader.root();
      for (size_t id = 18; id < m.elements.size(); ++id)
        for (auto li : reader.element_blocks(id)) {
          const auto bytes = reader.leaf(li);
          if (bytes.empty()) throw std::runtime_error("missing logical bytes");
        }
      std::cout << "group_target_mib=" << mib << " bytes=" << reader.size()
                << " logical=" << reader.logical().size() << " physical=" << reader.index().size()
                << " exact=" << sic::sicx::hex(exact) << '\n';
      std::filesystem::remove(path);
    }
    if (different != 2) throw std::runtime_error("physical groupings did not differ");
    std::cout << "PASS grouping invariance 16/32/64 MiB\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
