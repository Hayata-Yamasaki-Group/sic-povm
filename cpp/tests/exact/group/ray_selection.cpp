// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/group/ray_selection.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>

#include "assertions.hpp"
#include "sic/error.hpp"
#include "sicx/model/metadata.hpp"
#include "temporary_directory.hpp"

namespace {
using namespace sic::exact;

std::vector<SmithWord> scan_principal_column(const Group& group) {
  const auto& ar = group.ar;
  const auto mixed_sign = sub(QuadraticNumber(1), ar.eps);
  std::vector<bool> seen(ar.h());
  std::vector<SmithWord> words;
  for (long row = 0; row < group.N; ++row) {
    const auto narrow = group.act(row, 1) / (ar.d * ar.d);
    if (seen[narrow]) continue;
    const auto ideal = lattice_scale(ar.kc, mixed_sign, ar.class_involution(narrow));
    const auto partner = ar.identify(ideal).class_index;
    seen[narrow] = seen[partner] = true;
    words.push_back(group.coord(row));
  }
  std::sort(words.begin(), words.end());
  return words;
}

void check_d64_coset(const std::filesystem::path& path) {
  Arithmetic arithmetic(6);
  Group group(arithmetic);
  group.generate_standard();
  group.close(1, static_cast<long>(arithmetic.h()) * arithmetic.d * arithmetic.d / 2);
  GroupCheckpoint::save(group, path);

  const auto native = GroupCheckpoint::project(path, {}, false);
  const auto live = GroupCheckpoint::project(group);
  SIC_CHECK(live.smith == native.smith && live.stride == native.stride &&
            live.axis_classes == native.axis_classes);
  const RaySelection selection(native, 1);
  // The last Smith axis translates a two-element prefix along a second
  // independent axis. Require the representatives of both elements in that coset.
  const std::vector<SmithWord> expected{
      {0, 0, 0, 0, 0}, {0, 0, 0, 0, 1}, {0, 1, 0, 0, 0}, {0, 1, 0, 0, 1}};
  const auto words = selection.derive();
  SIC_CHECK(RaySelection(live, 2).derive() == words);
  const auto scanned = scan_principal_column(group);
  SIC_CHECK(scanned == expected);
  SIC_CHECK(words == scanned);
  selection.check(words);
}
}  // namespace

int main() {
  std::vector<SmithWord> digits{{10, 0}, {2, 1}, {2, 0}};
  sort_ray_words(digits);
  const std::vector<SmithWord> ordered{{2, 0}, {2, 1}, {10, 0}};
  SIC_CHECK(digits == ordered);
  SIC_CHECK(sic::sicx::ray_label(10) == "c10");
  const sic_test::TemporaryDirectory scratch("sic-ray-selection");
  const auto path = scratch.path / "group.bin";
  Arithmetic arithmetic(4);
  Group group(arithmetic);
  group.generate_standard();
  group.close(1, static_cast<long>(arithmetic.h()) * arithmetic.d * arithmetic.d / 2);
  GroupCheckpoint::save(group, path);

  const auto native = GroupCheckpoint::project(path, {}, false);
  const auto live = GroupCheckpoint::project(group);
  SIC_CHECK(native.smith == group.invf);
  SIC_CHECK(native.stride == group.stride);
  SIC_CHECK(native.digest == checkpoint_digest(path));
  const RaySelection selection(native, 1);
  const auto words = selection.derive();
  SIC_CHECK(RaySelection(live, 2).derive() == words);
  const std::vector<SmithWord> expected{{0, 0, 0, 0}, {0, 0, 0, 1}};
  SIC_CHECK(words == expected);
  SIC_CHECK(words == scan_principal_column(group));
  selection.check(words);
  // A second valid representative of the identity class is allowed.
  auto alternate = words;
  alternate[0][0] = 1;
  selection.check(alternate);
  // Distinct in-range words need not belong to different classes.
  alternate[1] = alternate[0];
  alternate[0] = words[0];
  bool rejected = false;
  try {
    selection.check(alternate);
  } catch (const sic::Error& error) {
    rejected = error.kind() == sic::ErrorKind::integrity;
  }
  SIC_CHECK(rejected);

  auto reduced_standard = native;
  reduced_standard.elements /= 2;
  bool unsupported = false;
  try {
    (void)RaySelection(reduced_standard, 1);
  } catch (const sic::Error& error) {
    unsupported = error.kind() == sic::ErrorKind::unsupported;
  }
  SIC_CHECK(unsupported);
  check_d64_coset(scratch.path / "group-d64.bin");
  return sic_test::report();
}
