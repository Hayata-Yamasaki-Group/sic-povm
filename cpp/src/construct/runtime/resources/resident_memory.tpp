// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::construct::resources {
template <class T>
void ResidentCensus::dynamic(const T&) {
  static_assert(kPlainLabel<T>, "dynamic resident owner needs an overload");
}

template <class T>
void ResidentCensus::capacity(const std::vector<T>& values) {
  allocation(sic::support::resources::mul(values.capacity(), sizeof(T)));
}

template <class T>
void ResidentCensus::dynamic(const std::vector<T>& values) {
  capacity(values);
  // Ordinary labels have no per-element owner; avoid scanning giant maps.
  if constexpr (!kPlainLabel<T>)
    for (const auto& value : values) dynamic(value);
}

template <class T>
bool ResidentCensus::first(const std::shared_ptr<T>& pointer) {
  if (!pointer || !shared.insert(pointer.get()).second) return false;
  allocation(sizeof(T) + 64);  // object and shared-control allowance
  return true;
}

}  // namespace sic::construct::resources
