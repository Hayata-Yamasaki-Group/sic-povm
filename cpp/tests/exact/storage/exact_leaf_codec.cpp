// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/storage/exact_leaf_codec.hpp"

#include <memory>

#include "assertions.hpp"

int main() {
  using namespace sic::exact;
  std::unique_ptr<FILE, decltype(&fclose)> file(tmpfile(), fclose);
  SIC_CHECK(file != nullptr);
  if (!file) return sic_test::report();
  Leaf value, decoded;
  fmpz_one(value.x);
  fmpz_mul_2exp(value.x, value.x, 513);
  fmpz_neg(value.x, value.x);
  fmpz_set_si(value.y, 29);
  leaf_codec::write_integer(file.get(), value.x);
  leaf_codec::write_integer(file.get(), value.y);
  rewind(file.get());
  leaf_codec::Count budget = 1024;
  leaf_codec::read_integer(file.get(), decoded.x, budget);
  leaf_codec::read_integer(file.get(), decoded.y, budget);
  SIC_CHECK(fmpz_equal(value.x, decoded.x));
  SIC_CHECK(fmpz_equal(value.y, decoded.y));
  SIC_CHECK(fgetc(file.get()) == EOF);
  for (const std::string& bytes :
       {std::string("\x80\0", 2), std::string("\x81", 1), std::string("\1", 1),
        std::string("\2\0", 2), std::string("\4\1", 2)}) {
    std::unique_ptr<FILE, decltype(&fclose)> bad(tmpfile(), fclose);
    fwrite(bytes.data(), 1, bytes.size(), bad.get());
    rewind(bad.get());
    Leaf leaf;
    leaf_codec::Count budget = 1024;
    bool rejected = false;
    try {
      leaf_codec::read_integer(bad.get(), leaf.x, budget);
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    SIC_CHECK(rejected);
  }
  return sic_test::report();
}
