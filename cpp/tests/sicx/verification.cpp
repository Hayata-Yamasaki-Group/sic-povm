// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fixtures/fixture.hpp"
namespace sicx_tests {
void verification_cases(Fixture& fixture) {
  auto& source = fixture.source;
  auto& good = fixture.good;
  auto& tests = fixture.tests;
  const auto& work = fixture.work;

  for (const std::string type : {"META", "INDX", "LDIR", "PDIR", "ZZZZ"})
    for (uint32_t flags : {0u, 1u})
      fixture.rejected(type + "/" + std::to_string(flags), [&](auto& rows) {
        auto e = rows.back();
        std::copy(type.begin(), type.end(), e.entry.type.begin());
        e.entry.flags = flags;
        rows.push_back(e);
      });
  for (const auto type : {"EXMT", "TOWR", "BODY", "RAYS", "BALL", "PROV", "PREC"}) {
    const auto it = std::find_if(good.begin(), good.end(),
                                 [&](const Row& r) { return r.entry.type_name() == type; });
    if (it != good.end())
      fixture.rejected(std::string(type) + " flags",
                       [&](auto& rows) { rows[it - good.begin()].entry.flags ^= 1; });
  }
  fixture.rejected("EXMT duplicate",
                   [](auto& rows) { rows.insert(rows.begin() + 1, rows.front()); });
  fixture.rejected("EXMT not first", [](auto& rows) { std::swap(rows[0], rows[1]); });
  auto bad_meta = [&](const std::string& name, auto mutate) {
    fixture.rejected(name, [&](auto& rows) {
      auto j = json(rows[0]);
      mutate(j);
      set_json(rows[0], j);
    });
  };
  bad_meta("unknown EXMT key",
           [](auto& j) { j["unknown"] = sic::support::Json::string_value("0"); });
  bad_meta("EXMT version",
           [](auto& j) { j["version"].array[1] = sic::support::Json::string_value("1"); });
  bad_meta("EXMT feature", [](auto& j) {
    j["required_features"].array.push_back(sic::support::Json::string_value("unknown"));
  });
  bad_meta("dimension", [](auto& j) { j["d"] = sic::support::Json::string_value("1"); });
  bad_meta("degree composite",
           [](auto& j) { j["tower"]["degrees"].array[0] = sic::support::Json::string_value("4"); });
  bad_meta("branch grammar", [](auto& j) {
    j["tower"]["branches"].array[0].array[0] = sic::support::Json::string_value("invalid");
  });
  bad_meta("branch canonical grid", [](auto& j) {
    j["tower"]["branches"].array[0].array[0] = sic::support::Json::string_value("0 0 1 -7f");
  });
  bad_meta("branch minimum",
           [](auto& j) { j["tower"]["branch_bits"] = sic::support::Json::string_value("256"); });
  bad_meta("rescaling rule", [](auto& j) {
    j["generator_rescaling"]["rule"] = sic::support::Json::string_value("unknown");
  });
  bad_meta("rescaling count", [](auto& j) { j["generator_rescaling"]["shifts"].array.clear(); });
  bad_meta("Smith product",
           [](auto& j) { j["galois"]["smith"].array[0] = sic::support::Json::string_value("3"); });
  bad_meta("word residue", [](auto& j) {
    j["galois"]["unit_generator_words"].array[0].array[0] = j["galois"]["smith"].array[0];
  });
  bad_meta("entry rule",
           [](auto& j) { j["entry_rule"]["name"] = sic::support::Json::string_value("unknown"); });
  bad_meta("element missing", [](auto& j) { j["elements"].array.pop_back(); });
  bad_meta("element order",
           [](auto& j) { std::swap(j["elements"].array[0], j["elements"].array[1]); });
  bad_meta("element duplicate",
           [](auto& j) { j["elements"].array.push_back(j["elements"].array[0]); });
  bad_meta("level set order", [](auto& j) {
    auto& levels = j["elements"].array.back()["levels"].array;
    levels.push_back(levels.front());
  });
  bad_meta("entry representatives",
           [](auto& j) { j["entry_rule"]["representatives"].array.pop_back(); });
  bad_meta("branch count", [](auto& j) { j["tower"]["branches"].array.pop_back(); });
  bad_meta("branch precision sequence",
           [](auto& j) { j["tower"]["branch_bits"] = sic::support::Json::string_value("129"); });
  bad_meta("negative shift", [](auto& j) {
    j["generator_rescaling"]["shifts"].array[0] = sic::support::Json::string_value("-1");
  });
  bad_meta("element kind", [](auto& j) {
    j["elements"].array[0]["kind"] = sic::support::Json::string_value("unknown");
  });
  bad_meta("scale leading zero", [](auto& j) {
    j["elements"].array[0]["scale"] = sic::support::Json::string_value("01");
  });
  bad_meta("scale positive", [](auto& j) {
    j["elements"].array[0]["scale"] = sic::support::Json::string_value("0");
  });
  bad_meta("max_bits attained", [](auto& j) {
    j["elements"].array[0]["max_bits"] = sic::support::Json::string_value("999");
  });
  bad_meta("max_bits upper", [](auto& j) {
    j["elements"].array[0]["max_bits"] = sic::support::Json::string_value("0");
  });
  const size_t element = 1;
  fixture.rejected("trailing decoded byte", [&](auto& rows) { rows[element].bytes.push_back(0); });
  fixture.rejected("nonminimal varint", [&](auto& rows) {
    rows[element].bytes.insert(rows[element].bytes.begin(), 0x80);
  });
  fixture.rejected("zero run", [&](auto& rows) { rows[element].bytes[3] = 0; });
  fixture.rejected("negative zero integer",
                   [&](auto& rows) { rows[element].bytes = {0, 0, 1, 3, 1, 0}; });
  fixture.rejected("nonminimal magnitude",
                   [&](auto& rows) { rows[element].bytes = {0, 0, 1, 3, 4, 1, 0, 0}; });
  fixture.rejected("zero data pair", [&](auto& rows) { rows[element].bytes = {0, 0, 1, 3, 0, 0}; });
  fixture.rejected("missing element payload",
                   [&](auto& rows) { rows.erase(rows.begin() + element); });
  fixture.rejected("out-of-range element", [&](auto& rows) { rows[element].bytes[0] = 127; });
  fixture.rejected("incorrect leaf count", [&](auto& rows) { rows[element].bytes[2] = 2; });
  fixture.rejected("run exceeds leaf", [&](auto& rows) { rows[element].bytes[3] = 127; });
  fixture.rejected("noncanonical JSON whitespace",
                   [&](auto& rows) { rows[0].bytes.insert(rows[0].bytes.begin(), ' '); });
  fixture.rejected("JSON numeric literal", [&](auto& rows) {
    std::string text(rows[0].bytes.begin(), rows[0].bytes.end());
    const auto at = text.find("\"d\":\"4\"");
    if (at == std::string::npos) throw std::runtime_error("test requires d4");
    text.replace(at, 7, "\"d\":4");
    rows[0].bytes.assign(text.begin(), text.end());
  });
  fixture.rejected("PROV override", [&](auto& rows) {
    auto it = std::find_if(rows.begin(), rows.end(),
                           [](const Row& r) { return r.entry.type_name() == "PROV"; });
    if (it == rows.end()) throw std::runtime_error("test needs PROV");
    auto j = json(*it);
    j["generator_rescaling"] = sic::support::Json::object_value();
    set_json(*it, j);
  });
  for (const std::string type : {"PROV", "RAYS", "PREC"}) {
    const auto it = std::find_if(good.begin(), good.end(),
                                 [&](const Row& r) { return r.entry.type_name() == type; });
    if (it != good.end()) {
      fixture.rejected("duplicate " + type,
                       [&](auto& rows) { rows.insert(rows.begin() + (it - good.begin()), *it); });
      fixture.rejected(type + " not object", [&](auto& rows) {
        set_json(rows[it - good.begin()], sic::support::Json::array_value());
      });
    }
  }
  // Isolate element-wide requirements from the independent rescaling and
  // root checks. Each file has fresh valid directories and physical hashes.
  auto element_rejected = [&](const std::string& name, size_t id, bool zero,
                              const std::string& scale, const std::string& diagnostic,
                              bool doubled = false) {
    auto rows = good;
    auto j = json(rows[0]);
    auto& element_json = j["elements"].array[id];
    element_json["scale"] = sic::support::Json::string_value(scale);
    element_json["max_bits"] = sic::support::Json::string_value(zero ? "0" : doubled ? "2" : "1");
    set_json(rows[0], j);
    sic::sicx::FmpzPair pair;
    fmpz_set_ui(pair.x, doubled ? 2 : 1);
    const auto& info = source.meta().elements[id];
    for (auto& row : rows) {
      const auto type = row.entry.type_name();
      if ((type != "TOWR" && type != "BODY") || row.entry.object != id) continue;
      sic::sicx::BlockEncoder encoder(source.meta().D0);
      row.bytes = encoder.encode(
          id, row.entry.first, info.block, zero || row.entry.first ? 0 : 1, [&](size_t) {
            return std::tuple<uint64_t, const fmpz*, const fmpz*>(0, pair.x, pair.y);
          });
    }
    const auto path = work / "element.sicx";
    write(path, rows);
    std::string message;
    try {
      sic::sicx::Reader r(path);
      (void)sic::sicx::check_element(r, id, 1);
    } catch (const std::exception& e) {
      message = e.what();
    }
    if (message.find(diagnostic) == std::string::npos)
      throw std::runtime_error("element rejection " + name + ": " + message);
    ++tests;
    std::cout << "PASS reject " << name << '\n';
  };
  const size_t representative = source.meta().elements.size() - 1;
  element_rejected("nonprimitive nonzero element", representative, false, "1", "not primitive",
                   true);
  element_rejected("nonminimal nonzero level set", representative, false, "1", "not minimal");
  element_rejected("zero element scale convention", representative, true, "2", "not primitive");
  element_rejected("zero element level convention", representative, true, "1", "not minimal");
}
}  // namespace sicx_tests
int main(int argc, char** argv) {
  return sicx_tests::run_fixture_case(argc, argv, &sicx_tests::verification_cases);
}
