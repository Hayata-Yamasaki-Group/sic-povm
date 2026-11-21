// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <random>

#include "../src/format/canonical_json.hpp"
#include "../src/format/container.hpp"
#include "../src/format/integer_codec.hpp"
#include "../src/format/metadata.hpp"
#include "../src/format/sicx_input.hpp"
#include "test_diagnostics.hpp"
#include "test_writer.hpp"
namespace sic_check::crt::test {
namespace filesystem = std::filesystem;
using namespace sic_check::crt::sicx;
extern size_t passed;
extern filesystem::path scratch;

void require(bool p, const string& msg);

string save(const fixture::ByteBuffer& bytes);

void positive(const string& name, const fixture::ByteBuffer& bytes);

void negative(const string& name, const fixture::ByteBuffer& bytes, const string& diagnostic = "");

void refusal(const string& name, const fixture::ByteBuffer& bytes,
             const std::function<void(Input&)>& change, const string& diagnostic = "");

void mutation(const string& name, const std::function<void(fixture::File&)>& change,
              bool finalized = false, const string& diagnostic = "");

void meta(const string& name, const string& a, const string& b);

void raymeta(const string& name, const string& a, const string& b);

void valid_cases();

void partial_cases();

void rejection_cases();

void fuzz_cases();

void boundary_cases();

void primitive_cases();

void amendment_cases();

void hybrid_cases();

void targeted_cases();
}  // namespace sic_check::crt::test
