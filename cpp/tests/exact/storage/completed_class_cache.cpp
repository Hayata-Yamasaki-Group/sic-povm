// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/storage/completed_class_cache.hpp"

#include "assertions.hpp"
#include "support/resources.hpp"
#include "temporary_directory.hpp"

namespace {
template <class F>
bool throws(F&& action) {
  try {
    action();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

std::string read(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write(const std::filesystem::path& path, const std::string& value) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(value.data(), value.size());
  if (!output) throw std::runtime_error("test fixture write failed");
}
}  // namespace

int main() {
  using namespace sic::exact;
  sic_test::TemporaryDirectory scratch("sic-completed-class");
  const auto root = scratch.path;
  Arithmetic ar(3);
  Group group(ar);
  group.generate_standard();
  group.close(
      sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")),
      (long)ar.h() * ar.d * ar.d / 2);
  auto layout = std::make_shared<OrbitTableLayout>(ar.d, ar.h(), group.orbit);
  CompletedClassIdentity identity;
  identity.s = ar.s;
  identity.d = ar.d;
  identity.delta = ar.Delta;
  identity.order = ar.order_index;
  identity.degree = group.N;
  identity.slots = layout->slots;
  identity.precision = 144;
  identity.producer = std::string(64, 'a');
  identity.group_digest = std::string(64, 'b');
  identity.coarse_digest = std::string(64, 'c');
  identity.derived.assign(ar.h(), 0);
  identity.elements.assign(ar.h(), -1);
  const int anchor = group.orbit[group.pidx(0, 0, 1)];
  for (long g = 0; g < group.N; ++g) {
    const int c = group.act(g, anchor) / (ar.d * ar.d);
    if (identity.elements[c] < 0) identity.elements[c] = g;
  }
  SIC_CHECK(ar.h() == 2);
  CompletedClassCache cache(root, identity, group);
  SIC_CHECK(throws([] { completed_loaded_object_digest(nullptr); }));
  std::istringstream dependencies(completed_class_dependencies());
  for (const char* expected : {"acb_mat_mul", "__gmpz_init", "mpfr_init2"}) {
    std::string name, digest;
    SIC_CHECK(bool(dependencies >> name >> digest));
    SIC_CHECK(name == expected);
    SIC_CHECK(digest.size() == 64);
    SIC_CHECK(digest.find_first_not_of("0123456789abcdef") == std::string::npos);
  }
  auto inventory = cache.inventory();
  SIC_CHECK(inventory.hits == 0);
  SIC_CHECK(inventory.missing == 2);
  CertifiedGhostRoot fixture;
  const long g = identity.elements[0];
  fixture.metadata = {ar.d, (ar.order_index * group.nn[g]) % (2 * ar.d), 0, group.chi[g], 144, 256};
  fixture.state = sic::support::ComplexBallVector(2 * ar.d - 2);
  for (size_t q = 0; q < fixture.state.size(); ++q) {
    acb_set_si(fixture.state[q], q + 1);
    arb_add_error_2exp_si(acb_realref(fixture.state[q]), -180);
  }
  TableClassBacking original;
  original.ghost = GhostClassBacking::save(fixture, g, 144);
  auto saved_root =
      cache.publish(0, TableClassBacking(original), "newton", "unit fixture, no SIC claim");
  SIC_CHECK(saved_root.ghost);
  SIC_CHECK(!saved_root.values);
  SIC_CHECK(saved_root.ghost->state->path() != original.ghost->state->path());
  SIC_CHECK(std::filesystem::exists(saved_root.ghost->state->path()));
  inventory = cache.inventory();
  SIC_CHECK(inventory.hits == 1);
  SIC_CHECK(inventory.missing == 1);
  SIC_CHECK(inventory.replay_workspace == original.workspace());
  Table table(ar, 144, 1);
  table.enable_compact(layout);
  table.alloc_values();
  const sic::support::resources::Budget budget;
  auto before = original.ghost->load(group, table, 0, budget);
  auto after = inventory.backing[0].ghost->load(group, table, 0, budget);
  for (size_t q = 0; q < before.size(); ++q) SIC_CHECK(acb_equal(before[q], after[q]));
  sic::support::ComplexBallVector full(layout->slots);
  for (size_t q = 0; q < full.size(); ++q) {
    acb_set_si(full[q], (long)q - 3);
    arb_add_error_2exp_si(acb_imagref(full[q]), -170);
  }
  auto saved_full = cache.publish(1, TableClassBacking(BallFile::save(full)), "quadrature",
                                  "unit fixture, no SIC claim");
  CompletedClassCache relative(std::filesystem::relative(root / "relative"), identity, group);
  SIC_CHECK(
      relative.publish(1, TableClassBacking(saved_full), "quadrature", "relative root fixture"));
  SIC_CHECK(relative.inventory().hits == 1);
  for (int repeat = 0; repeat < 2; ++repeat) {
    TableClassBacking unique(BallFile::save(full));
    const auto temporary_path = unique.values->path();
    SIC_CHECK(relative.publish(0, std::move(unique), "quadrature", "unique ownership fixture"));
    SIC_CHECK(!unique);
    SIC_CHECK(!std::filesystem::exists(temporary_path));
  }
  SIC_CHECK(saved_full.values);
  SIC_CHECK(!saved_full.ghost);
  inventory = cache.inventory();
  SIC_CHECK(inventory.hits == 2);
  SIC_CHECK(inventory.missing == 0);
  auto restored = inventory.backing[1].values->complex();
  for (size_t q = 0; q < full.size(); ++q) SIC_CHECK(acb_equal(full[q], restored[q]));

  const auto directory = std::filesystem::path(saved_root.ghost->state->path()).parent_path();
  const auto partial = directory.parent_path() / ".class-0.partial-interrupted";
  std::filesystem::rename(directory, partial);
  inventory = cache.inventory();
  SIC_CHECK(inventory.hits == 1);
  SIC_CHECK(inventory.missing == 1);
  std::filesystem::rename(partial, directory);
  for (int mutation = 0; mutation < 4; ++mutation) {
    auto changed = identity;
    if (mutation == 0) ++changed.precision;
    if (mutation == 1) changed.producer = std::string(64, 'd');
    if (mutation == 2) changed.group_digest = std::string(64, 'd');
    if (mutation == 3) changed.coarse_digest = std::string(64, 'd');
    CompletedClassCache other(root, changed, group);
    SIC_CHECK(other.inventory().hits == 0);
    SIC_CHECK(other.inventory().missing == 2);
  }
  for (int mutation = 0; mutation < 8; ++mutation) {
    TableClassBacking bad;
    bad.ghost = std::make_shared<GhostClassBacking>(*original.ghost);
    if (mutation == 0) bad.ghost->group_element = -1;
    if (mutation == 1) bad.ghost->metadata.chi *= -1;
    if (mutation == 2) ++bad.ghost->metadata.n;
    if (mutation == 3) bad.ghost->metadata.k = ar.d;
    if (mutation == 4) bad.ghost->metadata.precision = 143;
    if (mutation == 5) bad.ghost->metadata.constants_precision = 143;
    if (mutation == 6) bad.ghost->packing_precision = 145;
    if (mutation == 7) ++bad.ghost->metadata.d;
    SIC_CHECK(throws([&] { cache.publish(0, std::move(bad), "newton", "bad fixture"); }));
  }
  SIC_CHECK(
      throws([&] { cache.publish(0, TableClassBacking(original), "quadrature", "bad method"); }));
  auto derived_identity = identity;
  derived_identity.derived[0] = 1;
  derived_identity.paired = true;
  for (long candidate = 0; candidate < group.N; ++candidate)
    if (group.nn[candidate] == 2 * ar.d - 1 && group.chi[candidate] > 0 &&
        group.mul(candidate, candidate) == 0 &&
        group.act(candidate, group.orbit[1]) / (ar.d * ar.d) == 1) {
      derived_identity.cc = candidate;
      break;
    }
  SIC_CHECK(derived_identity.cc >= 0);
  CompletedClassCache derived(root / "derived", derived_identity, group);
  SIC_CHECK(
      throws([&] { derived.publish(0, TableClassBacking(original), "newton", "derived class"); }));

  const auto manifest = directory / "manifest";
  const auto checksum = directory / "manifest.sha256";
  const std::string valid_manifest = read(manifest);
  const std::string valid_checksum = read(checksum);
  for (const auto* header :
       {"SIC_COMPLETED_CLASS 1", "sic-completed-class/2.0"}) {  // identity-negative
    write(manifest, std::string(header) + valid_manifest.substr(valid_manifest.find('\n')));
    write(checksum, checkpoint_digest(manifest) + "\n");
    SIC_CHECK(throws([&] { cache.inventory(); }));
  }
  write(manifest, "truncated");
  SIC_CHECK(throws([&] { cache.inventory(); }));
  write(checksum, checkpoint_digest(manifest) + "\n");
  SIC_CHECK(throws([&] { cache.inventory(); }));
  std::istringstream dependency_rows(completed_class_dependencies());
  std::string dependency_name, dependency_digest;
  dependency_rows >> dependency_name >> dependency_digest;
  for (const auto& text :
       {std::string("0 root newton"), identity.group_digest, dependency_digest}) {
    auto changed = valid_manifest;
    const auto position = changed.find(text);
    SIC_CHECK(position != std::string::npos);
    changed[position] = changed[position] == '0' ? '1' : 'e';
    write(manifest, changed);
    write(checksum, checkpoint_digest(manifest) + "\n");
    SIC_CHECK(throws([&] { cache.inventory(); }));
  }
  write(manifest, valid_manifest);
  write(checksum, valid_checksum);
  const auto payload = directory / "payload.balls";
  const std::string valid_payload = read(payload);
  auto corrupted = valid_payload;
  corrupted.back() ^= 1;
  write(payload, corrupted);
  SIC_CHECK(throws([&] { cache.inventory(); }));
  write(payload, valid_payload.substr(0, valid_payload.size() - 1));
  SIC_CHECK(throws([&] { cache.inventory(); }));
  write(payload, valid_payload);
  SIC_CHECK(cache.inventory().hits == 2);
  original = {};
  saved_root = {};
  SIC_CHECK(cache.inventory().hits == 2);  // Private owners do not own persistent files.

  SIC_CHECK(completed_class_new_disk(0, 1000, 30, 40) == 70);
  SIC_CHECK(completed_class_new_disk(1, 1000, 30, 40) == 2070);
  SIC_CHECK(completed_class_new_disk(2, 1000, 30, 40) == 3070);
  const sic::support::resources::Bytes available = 500;
  SIC_CHECK(completed_class_new_disk(0, 1000, 30, 40) < available);
  SIC_CHECK(completed_class_new_disk(2, 1000, 30, 40) > available);
  SIC_CHECK(throws([&] { completed_class_new_disk(2, UINT64_MAX, 0, 0); }));
  return sic_test::report();
}
