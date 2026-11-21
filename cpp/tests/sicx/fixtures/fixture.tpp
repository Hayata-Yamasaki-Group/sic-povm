// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sicx_tests {
template <class Mutate>
void Fixture::rejected(const std::string& name, Mutate mutate) {
  auto rows = good;
  mutate(rows);
  const auto path = work / "bad.sicx";
  write(path, rows);
  bool refused = false;
  try {
    sic::sicx::Reader r(path);
    (void)sic::sicx::verify_all(r, 1);
  } catch (const std::exception&) {
    refused = true;
  }
  if (!refused) throw std::runtime_error("accepted " + name);
  ++tests;
  std::cout << "PASS reject " << name << '\n';
}
}  // namespace sicx_tests

namespace sicx_tests {
template <class F>
bool throws(F f) {
  try {
    f();
  } catch (const sic::Error&) {
    return true;
  } catch (const std::exception&) {
    return true;
  }
  return false;
}
}  // namespace sicx_tests
