// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/integer.hpp"
#include "sicx/container/directory.hpp"
// Cross-reader fixtures: keep hashes, labels and BALL associations valid while
// permuting only the ray order specified by SICX 1.0 section 5.1a.
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>

#include "sic/sicx/finalize.hpp"

namespace filesystem = std::filesystem;

int main(int argc, char** argv) {
  try {
    if (argc != 3) return 2;
    const filesystem::path out = argv[2];
    filesystem::create_directories(out);
    sic::sicx::Reader source(argv[1]);
    (void)sic::sicx::verify_all(source, 2);
    const auto& meta = source.meta();
    const std::vector<std::vector<uint64_t>> expected{
        {1, 0, 0, 24, 11}, {1, 0, 0, 24, 12}, {1, 1, 0, 24, 11}, {1, 1, 0, 24, 12}};
    if (meta.d != 64 || meta.rays.size() != 4) throw std::runtime_error("four d64 rays required");
    for (size_t i = 0; i < 4; ++i)
      if (meta.rays[i].word != expected[i] || meta.rays[i].label != "c" + std::to_string(i))
        throw std::runtime_error("unexpected d64 ray selection/order");

    // Every mutant has recomputed physical digests and roots. Its labels remain
    // c0..c3, and each BALL follows its original word and anchor.
    const auto rays_json = sic::sicx::rays_to_json(meta);
    std::ifstream input(argv[1], std::ios::binary);
    const sic::sicx::ByteBuffer original((std::istreambuf_iterator<char>(input)), {});
    auto emit = [&](const filesystem::path& path, const std::array<size_t, 4>& order,
                    bool exact_only) {
      if (exact_only) {
        sic::sicx::StreamWriter writer(path, sic::sicx::StreamOptions{false, false, {}, false});
        for (size_t i = 0; i < source.exact_count(); ++i) {
          const auto& e = source.index()[i];
          writer.chunk(e.type_name(), e.flags, source.chunk(i), e.object, e.first, e.count);
        }
        writer.finish();
        return;
      }
      auto file = original;
      auto index = source.index();
      auto j = rays_json;
      for (size_t i = 0; i < 4; ++i) {
        j["rays"].array[i] = rays_json.at("rays").array[order[i]];
        j["rays"].array[i]["label"] = sic::support::Json::string_value("c" + std::to_string(i));
      }
      const auto text = sic::support::dump_json(j);
      for (auto& e : index) {
        sic::sicx::ByteBuffer payload;
        if (e.type_name() == "RAYS")
          payload.assign(text.begin(), text.end());
        else if (e.type_name() == "BALL")
          payload = source.chunk(source.ball_chunk(order.at(e.object)));
        else
          continue;
        if (payload.size() != e.length) throw std::runtime_error("mutation changed chunk length");
        std::copy(payload.begin(), payload.end(),
                  file.begin() + e.offset + sic::sicx::kChunkHeaderBytes);
        e.sha256 = sic::sicx::sha256(payload.data(), payload.size());
      }
      const size_t footer = file.size() - 112;
      const auto lo = sic::sicx::get_le(file.data() + footer, 8);
      const auto ll = sic::sicx::get_le(file.data() + footer + 8, 8);
      const auto po = sic::sicx::get_le(file.data() + footer + 16, 8);
      const sic::sicx::ByteBuffer ldir(file.begin() + lo, file.begin() + lo + 16 + ll);
      sic::sicx::ByteBuffer pt;
      sic::sicx::put_le(pt, index.size(), 8);
      for (const auto& e : index) e.put(pt);
      const auto pdir = sic::sicx::directory_chunk("PDIR", pt);
      std::copy(pdir.begin(), pdir.end(), file.begin() + po);
      const auto root = sic::sicx::file_tree_root(index, ldir);
      std::copy(root.begin(), root.end(), file.begin() + footer + 64);
      std::ofstream output(path, std::ios::binary);
      output.write(reinterpret_cast<const char*>(file.data()), file.size());
      if (!output) throw std::runtime_error("mutation write failed");
    };
    std::array<size_t, 4> order{0, 1, 2, 3};
    emit(out / "exact.sicx", order, true);
    unsigned count = 0;
    do {
      std::vector<sic::sicx::FinalizeRay> rays;
      std::vector<std::vector<sic::sicx::BallPart>> parts;
      std::ofstream args(out / ("order-" + std::to_string(count) + ".args"));
      for (size_t i = 0; i < 4; ++i) {
        const auto& r = meta.rays[order[i]];
        rays.push_back({"c" + std::to_string(i), r.word, static_cast<int64_t>(r.anchor)});
        parts.push_back(source.ball_parts(order[i]));
        args << "--ray c" << i << " ";
        for (size_t l = 0; l < r.word.size(); ++l) args << (l ? "," : "") << r.word[l];
        args << " " << r.anchor << "\n";
      }
      bool rejected = false;
      auto copy = meta;
      try {
        (void)sic::sicx::build_finalization(copy, rays, parts, meta.ball_working_bits);
      } catch (const std::exception& e) {
        if (std::string(e.what()).find("ray words") == std::string::npos) throw;
        rejected = true;
      }
      if (rejected != (count != 0)) throw std::runtime_error("finalizer ray-order gate");
      emit(out / ("order-" + std::to_string(count) + ".sicx"), order, false);
      ++count;
    } while (std::next_permutation(order.begin(), order.end()));
    std::cout << "PASS: finalizer accepts canonical d64 order; rejects 23 permutations\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
