// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/sicx/finalize.hpp"

#include <fstream>
#include <iostream>

#include "sicx/codec/ball.hpp"

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  try {
    // A narrow source with a long midpoint must grow under a one-limb wire width.
    arb_t source, stored;
    arb_init(source);
    arb_init(stored);
    arb_set_ui(source, 1);
    arb_div_ui(source, source, 3, 512);
    const auto outward = sic::sicx::outward_ball_part(source, 1);
    sic::sicx::ball_part_arb(stored, outward);
    const bool contains = arb_contains(stored, source);
    const bool same64 = sic::sicx::certified_round(source, "source") ==
                        sic::sicx::certified_round(stored, "stored");
    arb_clear(source);
    arb_clear(stored);
    if (!contains || !same64) throw std::runtime_error("outward midpoint/radius rounding failed");
    arb_init(source);
    arb_one(source);
    arb_add_error_2exp_si(source, -52);
    bool boundary_refused = false;
    try {
      (void)sic::sicx::outward_ball_part(source, 1);
    } catch (const sic::Error&) {
      boundary_refused = true;
    }
    arb_clear(source);
    if (!boundary_refused) throw std::runtime_error("ambiguous binary64 rounding was accepted");
    const std::filesystem::path work = argv[2];
    std::filesystem::create_directories(work);
    sic::sicx::Reader fixture(argv[1]);
    const auto path = work / "exact.sicx";
    sic::sicx::StreamWriter writer(path, {});
    for (size_t i = 0; i < fixture.exact_count(); ++i) {
      const auto& e = fixture.index()[i];
      writer.chunk(e.type_name(), e.flags, fixture.chunk(i), e.object, e.first, e.count);
    }
    writer.finish();
    sic::sicx::Meta meta = fixture.meta();
    std::vector<sic::sicx::FinalizeRay> rays;
    std::vector<std::vector<sic::sicx::BallPart>> wire_balls;
    for (size_t r = 0; r < meta.rays.size(); ++r) {
      const auto& ray = meta.rays[r];
      const auto balls = work / (ray.label + ".balls");
      std::ofstream out(balls);
      out << "sic-vector-balls/1.0\nd " << meta.d << '\n';
      const auto parts = fixture.ball_parts(r);
      for (uint64_t j = 0; j < meta.d; ++j)
        out << sic::sicx::ball_part_text(parts[2 * j]) << " | "
            << sic::sicx::ball_part_text(parts[2 * j + 1]) << '\n';
      out << "end\n";
      rays.push_back({ray.label, ray.word, static_cast<int64_t>(ray.anchor)});
      wire_balls.push_back(parts);
    }
    const auto f =
        sic::sicx::build_finalization(meta, rays, wire_balls, fixture.meta().ball_working_bits);
    auto wrong_root = fixture.exact_root();
    wrong_root[0] ^= 1;
    bool refused = false;
    try {
      sic::sicx::finalize_in_place(path, meta, f, false, wrong_root);
    } catch (const sic::Error& e) {
      refused =
          std::string(e.what()).find("differs from the certified decode") != std::string::npos;
    }
    sic::sicx::Reader check(path);
    if (!refused || check.finalized() || check.exact_root() != fixture.exact_root() ||
        sic::sicx::finalization_in_progress(path))
      throw std::runtime_error("changed-root refusal mutated the input");
    const auto good = work / "matching-root.sicx";
    std::filesystem::copy_file(path, good);
    const auto [root, full_root] =
        sic::sicx::finalize_in_place(good, meta, f, false, fixture.exact_root());
    if (root != fixture.exact_root() || !sic::sicx::Reader(good).finalized())
      throw std::runtime_error("matching-root finalization failed");
    std::cout << "PASS: exact-root handover and CLI inputs\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
