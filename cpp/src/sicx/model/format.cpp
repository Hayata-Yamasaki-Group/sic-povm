// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/sicx/format.hpp"

#include "support/hex.hpp"
#include "support/sha256.hpp"

namespace sic::sicx {
[[noreturn]] void fail(ErrorKind kind, const std::string& message) {
  throw Error(kind, "SICX: " + message);
}
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t ValuationPolicy::admit(uint64_t count, uint64_t used) const {
  if (count > SIZE_MAX / sizeof(uint32_t) || count > table_bytes / sizeof(uint32_t) ||
      used > total_bytes || count > (total_bytes - used) / sizeof(uint32_t))
    throw Error(ErrorKind::resource, "rescaling valuation table resource refusal");
  return count * sizeof(uint32_t);
}
}  // namespace sic::sicx

namespace sic::sicx {
void admit_chunk(std::string_view type, uint64_t bytes, const ResourceLimits& limits) {
  const bool text = type == "EXMT" || type == "PROV" || type == "RAYS" || type == "PREC";
  if (bytes > limits.block_bytes || (text && bytes > limits.text_chunk_bytes))
    throw Error(ErrorKind::resource, "SICX: chunk resource limit");
}
}  // namespace sic::sicx

namespace sic::sicx {
Digest sha256(const uint8_t* data, size_t size) { return support::sha256::digest(data, size); }
}  // namespace sic::sicx

namespace sic::sicx {
std::string hex(std::span<const uint8_t> bytes) { return support::hex_encode(bytes); }
}  // namespace sic::sicx

namespace sic::sicx {
Meta::Entry Meta::entry(uint64_t m) const {
  if (m >= d) fail(ErrorKind::invalid_argument, "coordinate out of range");
  if (!units || units->d != d || units->logarithm.size() != d)
    fail(ErrorKind::invalid_argument, "entry unit table has not been derived");
  const uint64_t g = std::gcd(m, d), n = d / g;
  uint64_t lift = n == 1 ? 1 : m / g;
  while (std::gcd(lift, d) != 1) lift += n;
  if (lift >= d) fail(ErrorKind::invalid_argument, "canonical unit lift unavailable");
  auto index = units->logarithm[lift];
  if (index == UnitGroup::kNonunit) fail(ErrorKind::invalid_argument, "unit logarithm unavailable");
  Entry e{g == d ? 0 : static_cast<int64_t>(g), std::vector<uint64_t>(smith.size(), 0)};
  for (size_t j = 0; j < units->orders.size(); ++j) {
    const auto digit = index % units->orders[j];
    index /= units->orders[j];
    for (size_t l = 0; l < smith.size(); ++l)
      e.word[l] = (e.word[l] + digit * unit_generator_words[j][l]) % smith[l];
  }
  return e;
}
}  // namespace sic::sicx
