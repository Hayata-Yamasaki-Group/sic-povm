// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decode/model.hpp"

#include "decode/decoder.hpp"

namespace sic::decode {
Class::Class(uint64_t p, unsigned i, unsigned degree) : parent(p), index(i), dual(degree) {}

Class::Class() : parent(0), index(0) {}

}  // namespace sic::decode

namespace sic::decode {
Decoder::Impl::Family::Family() = default;
}  // namespace sic::decode
