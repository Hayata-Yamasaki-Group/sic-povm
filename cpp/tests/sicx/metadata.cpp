// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/block.hpp"
#include "support/sha256.hpp"
// SICX primitive and fault-control tests:
//   sic_test_sicx_sicx FIXTURE.sicx WORKDIR
// Primitives: canonical varints, integer code, canonical JSON, block runs.
// Faults on FIXTURE: a flipped byte in every chunk, truncation, a broken
// footer, and a mutated text-form line must each be refused (fail-closed).
// Streaming writer: kill/resume, corruption, parts, finalization.
#include <sys/wait.h>

#include <fstream>
#include <iostream>
#include <random>
#include <thread>

#include "fixtures/fixture.hpp"
#include "sic/sicx/finalize.hpp"
#include "sic/sicx/text.hpp"
namespace sicx_tests {
void metadata_cases() {
  // Canonical JSON.
  check(!throws([] { sic::support::JsonParser::parse(R"({"a":"1","b":["x","-2"]})"); }),
        "canonical JSON accepted");
  check(throws([] { sic::support::JsonParser::parse(R"({"b":"1","a":"2"})"); }),
        "unsorted keys refused");
  check(throws([] { sic::support::JsonParser::parse(R"({"a": "1"})"); }), "whitespace refused");
  check(throws([] { sic::support::JsonParser::parse(R"({"a":1})"); }), "bare number refused");
  check(throws([] {
          sic::support::JsonParser::parse(std::string("{\"a\":\"") + char(92) + "u0041\"}");
        }),
        "unnecessary escape refused");
  check(throws([] { sic::support::Json::string_value("007").as_int(); }),
        "non-canonical decimal string refused");
  check(sic::support::Json::string_value("-12").as_int() == -12, "decimal string parsed");
  check(sic::sicx::field_radicand(8) == 5 && sic::sicx::field_radicand(1024) == 41861 &&
            sic::sicx::field_radicand(2) == -3,
        "D0 from d");
}
}  // namespace sicx_tests
