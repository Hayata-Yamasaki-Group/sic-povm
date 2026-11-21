// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::construct {
template <class T>
template <class Parse>
ParsedProducerOption<T>::ParsedProducerOption(Parse parse) {
  try {
    value_ = parse();
  } catch (...) {
    error_ = std::current_exception();
  }
}

}  // namespace sic::construct
