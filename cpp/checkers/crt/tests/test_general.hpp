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

// SPDX-License-Identifier: AGPL-3.0-or-later
// Included inside test_input's test namespace; no production implementation is consulted.
Uint64 brute_order(Uint64 a, Uint64 n);

// The oracle exhausts residues of each prime-power group, then all global unit tuples.
// It does not call the reader's factoring, primitive-root, CRT or discrete-log helpers.
vector<Input::UnitGenerator> brute_generators(Uint64 d);

void general_cases();
}  // namespace sic_check::crt::test
