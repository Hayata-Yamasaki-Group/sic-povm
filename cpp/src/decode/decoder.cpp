// SPDX-License-Identifier: AGPL-3.0-or-later
// Opaque owner boundary; numerical loops remain in the private implementation.
#include "decode/decoder.hpp"

namespace sic::decode {
Decoder::Options Decoder::validate_options(Options options, const Reader& reader) {
  return Impl::validate_options(std::move(options), reader);
}
Decoder::Decoder(const Reader& reader, Options options)
    : impl_(std::make_unique<Impl>(reader, std::move(options))) {}
Decoder::Decoder(const Reader& reader, Options options, const std::vector<Request>& requests)
    : impl_(std::make_unique<Impl>(reader, std::move(options), requests)) {}
Decoder::~Decoder() = default;
Decoder::Decoder(Decoder&& other) {
  if (other.impl_) other.impl_->check_owner();
  impl_ = std::move(other.impl_);
}
Decoder& Decoder::operator=(Decoder&& other) {
  if (impl_) impl_->check_owner();
  if (other.impl_) other.impl_->check_owner();
  if (this == &other) return *this;
  if (impl_ && other.impl_ && (impl_->placement().pin || other.impl_->placement().pin))
    throw sic::Error(ErrorKind::invalid_argument, "move assignment between live pinned decoders");
  impl_ = std::move(other.impl_);
  return *this;
}
Decoder::Impl& Decoder::state() const {
  if (!impl_) throw sic::Error(ErrorKind::invalid_argument, "moved-from decoder");
  return *impl_;
}
std::vector<size_t> Decoder::input_chunks(const Reader& reader,
                                          const std::vector<Request>& requests) {
  return Impl::input_chunks(reader, requests);
}
const Stats& Decoder::stats() const { return state().stats(); }
const Placement& Decoder::placement() const { return state().placement(); }
slong Decoder::base_precision() const { return state().base_precision(); }
unsigned Decoder::pool_groups() const { return state().pool_groups(); }
ComplexBallVector Decoder::decode(const std::vector<Request>& requests) {
  return state().decode(requests);
}
}  // namespace sic::decode

namespace sic::decode {}  // namespace sic::decode

namespace sic::decode {}  // namespace sic::decode
