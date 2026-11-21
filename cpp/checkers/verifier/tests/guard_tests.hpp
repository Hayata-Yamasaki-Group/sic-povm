// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/fmpz_poly.h>
#include <openssl/sha.h>
#include <sys/mman.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "../src/certificate/branches.hpp"
#include "../src/certificate/branches_numeric.hpp"
#include "../src/decode/check.hpp"
#include "../src/decode/decoder.hpp"
#include "../src/format/ball_codec.hpp"
#include "../src/format/canonical_json.hpp"
#include "../src/format/container.hpp"
#include "../src/format/integer_codec.hpp"
#include "../src/format/metadata.hpp"
#include "../src/format/sha256.hpp"
#include "../src/format/units.hpp"
#include "../src/model/input_model.hpp"
#include "../src/runtime/diagnostics.hpp"
#include "../src/text/text_check.hpp"
#include "fault_seams.hpp"
#include "test_writer.hpp"
namespace sic_check::verifier::test {
using namespace sic_check::verifier;
using namespace fixture;

// SPDX-License-Identifier: AGPL-3.0-or-later
// Boundary and API-failure contracts supplement file mutations.
void guard_tests(size_t& rejects);
}  // namespace sic_check::verifier::test
