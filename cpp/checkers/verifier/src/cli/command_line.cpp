// SPDX-License-Identifier: AGPL-3.0-or-later
#include "command_line.hpp"

#include <cstdio>

#include "../certificate/branches.hpp"
#include "../decode/check.hpp"
#include "../format/canonical_json.hpp"
#include "../format/container.hpp"
#include "../model/input_model.hpp"
#include "../runtime/diagnostics.hpp"
#include "../self_test/self_test.hpp"
#include "../text/text_check.hpp"
namespace sic_check::verifier {
int run_command(int argc, char** argv) {
  for (int version_arg = 1; version_arg < argc; ++version_arg) {
    if (std::string(argv[version_arg]) == "--version") {
      if (argc != 2) {
        std::fputs("sic-verify-independent: --version requires no other arguments\n", stderr);
        return 2;
      }
      std::printf(
          "{\"name\":\"sic-verify-independent\",\"schema\":\"sic-tool-version/1.0\","
          "\"source_commit\":\"%s\",\"version\":\"1.0.0\"}\n",
          SIC_SOURCE_COMMIT);
      return 0;
    }
  }
  if (argc == 2 && std::string(argv[1]) == "--help") {
    std::fputs(
        "usage: sic-verify-independent FILE [--text FILE] [--decode auto|full|ratio|off] "
        "[--ray LABEL] [--max-steps N] [--extra-bits N] [--release] [--partial-element "
        "ID] [--expect-exact-root HEX] [--expect-file-root HEX] [--expect-sha256 HEX] "
        "[--branch-degree-limit N] | --selftest\n  --help  Print help and exit 0.\n  "
        "--version  Print canonical tool identity and exit 0.\n"
        "Exit codes: 0 scoped pass, 2 invalid data or usage, 3 resource/precision refusal.\n",
        stdout);
    return 0;
  }
  Opts O;
  bool st = false, has_path = false;
  auto number = [](const string& value) -> Uint64 {
    if (!is_uint(value) || value.size() > 18)
      throw UsageErr("option requires an unsigned decimal of at most 18 digits");
    return strtoull(value.c_str(), nullptr, 10);
  };
  try {
    for (int i = 1; i < argc; i++) {
      string a = argv[i];
      auto val = [&]() -> string {
        if (i + 1 >= argc) {
          throw UsageErr("missing value for " + a);
        }
        return argv[++i];
      };
      if (a == "--text")
        O.text_path = val();
      else if (a == "--decode")
        O.decode = val();
      else if (a == "--max-steps")
        O.max_steps = std::min<Uint64>(number(val()), 1u << 20);
      else if (a == "--extra-bits")
        O.extra_bits = std::min<Uint64>(number(val()), 1u << 20);
      else if (a == "--ray")
        O.only_ray = val();
      else if (a == "--selftest")
        st = true;
      else if (a == "--partial-element")
        O.partial_elements.push_back(number(val()));
      else if (a == "--expect-exact-root")
        O.expected_exact = val();
      else if (a == "--expect-file-root")
        O.expected_file = val();
      else if (a == "--expect-sha256")
        O.expected_sha = val();
      else if (a == "--branch-degree-limit")
        O.branch_degree_limit = number(val());
      else if (a == "--release")
        O.release = true;
      else if (a.empty() || a[0] != '-') {
        if (has_path) throw UsageErr("expected at most one input path");
        has_path = true;
        O.path = a;
      } else {
        fprintf(stderr,
                "unknown option %s\nusage: sic-verify-independent FILE.sicx [--text FILE.jsonl] "
                "[--decode "
                "auto|full|ratio|off] [--ray LABEL] [--max-steps N] [--extra-bits N] [--release] "
                "[--partial-element ID] [--expect-exact-root HEX] [--expect-file-root HEX] "
                "[--expect-sha256 HEX] [--branch-degree-limit N] | --selftest\n",
                a.c_str());
        return 2;
      }
    }
  } catch (const UsageErr& e) {
    fprintf(stderr, "%s\n", e.what());
    return 2;
  }
  if (O.branch_degree_limit < 1 || O.branch_degree_limit > (1u << 20)) {
    fprintf(stderr, "branch degree resource limit must be 1..2^20\n");
    return 2;
  }
  if (O.decode != "auto" && O.decode != "full" && O.decode != "ratio" && O.decode != "off") {
    fprintf(stderr, "--decode auto|full|ratio|off\n");
    return 2;
  }
  if (st) {
    int b = selftest();
    say(b ? "SELFTEST: %d failures\n" : "SELFTEST PASS\n", b);
    return b ? 1 : 0;
  }
  if (O.path.empty()) {
    fprintf(stderr, "usage: sic-verify-independent FILE.sicx ...\n");
    return 2;
  }
  try {
    if (!O.partial_elements.empty() &&
        (!O.text_path.empty() || !O.expected_sha.empty() || O.release))
      throw UsageErr("partial mode cannot claim whole-file hash, text, or release verification");
    Container C;
    open_container(C, O);
    verify_chunks(C, O);
    if (C.all_exact) {
      rescaling_check(C, O);
      branch_check(C, O);
    }
    C.f.stable();
    report_trust(C, O);
    if (!O.partial_elements.empty()) {
      say("RESULT: PARTIAL PASS (EXMT, both directories, both roots, selected complete elements; "
          "%s; unread ordinary headers and optional/finalization payloads unchecked; %s)\n",
          O.expected_exact.empty() && O.expected_file.empty()
              ? "self-consistent, exact values NOT authenticated"
              : "exact values authenticated by trusted root",
          C.all_exact ? "all exact data, global rescaling and canonical branches checked"
                      : "global rescaling and canonical branches deferred");
      return 0;
    }
    if (!O.text_path.empty()) text_check(C, O.text_path);
    if (O.decode != "off") decode_check(C, O);
    if (O.release && C.M.has_rays && C.dec_full + C.dec_ratio == 0)
      throw PrecErr("--release: no ray was decoded against the exact data (--decode off?)");
    string dec_note = !C.M.has_rays     ? ""
                      : C.M.has_decoder ? "; decoder record"
                                        : "; no decoder record";
    string dsum;
    if (O.decode == "off")
      dsum = "decode off";
    else if (!C.M.has_rays)
      dsum = "no BALL to decode";
    else {
      dsum = "BALL vs exact data: " + std::to_string(C.dec_full) + " full + " +
             std::to_string(C.dec_ratio) + " ratio of " + std::to_string(C.dec_total) + " rays";
      if (O.decode == "auto") dsum += " (partial spot check)";
      if (C.dec_full + C.dec_ratio == 0) dsum += " (NO ray decoded: step budget)";
    }
    C.f.stable();
    say("RESULT: PASS (%s%s%s; %s%s; %s)\n", "SICX 1.0 final container",
        C.M.has_rays ? "" : ", exact part only", dec_note.c_str(), C.rescaling_line.c_str(),
        O.release ? "; release requirements met" : "", dsum.c_str());
    return 0;
  } catch (Reject& e) {
    say("RESULT: REJECT: %s\n", e.what());
    return 2;
  } catch (PrecErr& e) {
    say("RESULT: REFUSED: %s\n", e.what());
    return 3;
  } catch (std::bad_alloc&) {
    say("RESULT: REFUSED: allocation resource limit\n");
    return 3;
  } catch (UsageErr& e) {
    say("RESULT: USAGE ERROR: %s\n", e.what());
    return 2;
  }
}

}  // namespace sic_check::verifier
