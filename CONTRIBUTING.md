# Contributing

Code, documentation, script, schema and test contributions are accepted under
[AGPL-3.0-or-later](LICENSE). Dataset contributions to `data/` or to the datasets distributed as
GitHub Release assets are accepted under the [ODbL v1.0](LICENSE-ODbL), with any rights in their
individual contents licensed under the [DbCL v1.0](LICENSE-DbCL).

Code, comments and documentation are written in English. The code is C++20, built with CMake and
tested with CTest; format C++ sources with the repository `.clang-format` (the continuous
integration uses clang-format 14). Exact arithmetic uses FLINT 3 with Arb; changes keep the
certified ball bounds, recognition checks and quadrature fallbacks.

Start with the [README](README.md), the
[mathematical conventions](docs/sicx.md#mathematical-conventions) and
[verification](docs/verification.md). `sic-construct` constructs the exact data, the
[SICX specification](docs/sicx.md#spec-sicx-10-final-exact-weylheisenberg-sic-fiducial-format)
defines its format, and the [data guide](docs/data.md#data-guide) describes the released files and
their [downloads](docs/data.md#transport-getting-the-exact-data).

## Design rules

**One name, one meaning, one owner, and the documentation follows the code.**
The checks below enforce specific parts; review covers the rest of each rule.

1. **Names state role.** Name paths, identifiers, targets, tests and options by
   responsibility. Do not encode versions, dates or development history. A frozen
   external identity (wire format, schema, algorithm, cache, receipt or record tag)
   and a file named for it retain that identity. `sic_test_policy_hygiene` checks
   development-history wording; `sic_test_policy_format_identities` checks frozen
   identities; `sic_test_policy_data_layout` checks data-path spelling and version labels.
   Review checks whether names describe their roles.

2. **Spelling follows the kind of thing named.** Use this single spelling table:

   | Kind | Spelling |
   | --- | --- |
   | C++ files/folders, data paths, functions, variables, members, namespaces, CMake library targets, CTest names/labels | `lower_snake_case` |
   | C++ types/type aliases; CMake module files | `PascalCase` |
   | Named constants | `kPascalCase` |
   | Macros, CMake options, environment variables | `SIC_UPPER_SNAKE_CASE` |
   | Executable targets, command-line commands and options, frozen external identities and files named for them | `lower-kebab-case` |

   CMake scripts, `.mjs` scripts and documentation paths use `lower_snake_case`.
   Keep mathematical letters and external/platform spellings. Frozen identities
   retain their specified version suffixes and record-tag spelling.
   `sic_test_policy_layout` checks C++ paths; `sic_test_policy_data_layout` checks
   data paths. CMake checks CTest names during
   configuration. Review checks the remaining spelling. Installed headers must
   not contain namespace-scope `using namespace` directives or define macros that
   contain them (`sic_test_policy_header_namespaces`).

3. **One concept, one owner.** Put each production implementation in its owning
   component or shared support owner. Keep one contract per file and one component
   or topic per folder. Remove local copies unless a recorded semantic difference
   justifies them. Independent checkers deliberately keep separate implementations
   and never share production or peer-checker code. `sic_test_policy_layout` checks
   layout/include cycles; `sic_test_policy_symbols` checks symbol/link cycles.
   `sic_test_policy_hex_owner` rejects duplicate lookup, formatted-byte and stream-byte hex
   encoders outside the shared owner; checker implementations remain independent.
   Arithmetic encoders and indirect forwarding require review.
   Tests labeled `independence` check checker boundaries; review checks semantic ownership.

4. **Types carry units and meaning.** Names say what numbers count: bytes, count,
   bits, limbs, mib or seconds. Reserve `Bytes` for byte counts; name buffers
   differently. Use named structs for records with distinct semantic fields,
   rather than pairs or tuples. Checked in review.

5. **Failures are categorized at every public boundary.** Follow the
   [`sic::Error` contract](docs/library.md#library-c-library-api) for public
   failures and its documented exceptions. Expected non-applicability is a status.
   `sic_test_policy_failure_boundaries` rejects direct `runtime_error`, `logic_error` and
   `invalid_argument` throws in installed headers, production library components and apps,
   with exact reviewed internal invariant sites. Aliases, indirect callees
   and control flow require review; this lexical check does not prove coverage of
   every public boundary.

6. **Resources come from the process allowance.** Derive default threads and
   memory budgets from process affinity and cgroup/job limits, never host totals.
   Explicit settings take precedence; memory admission fails closed. Platforms
   without an affinity API use OS logical/physical CPU counts (on macOS,
   `hw.logicalcpu`/`hw.physicalcpu`). The logical count falls back to
   `hardware_concurrency()` and the physical count to 1.
   One function owns CPU allowance; cache topology once per process.
   `sic_test_policy_newton_workers` checks that the Newton table takes class
   workers from their owner; review checks the allowance, topology and admission rules.

7. **Scalability is measured and canonical bytes never move.** Accept runtime or
   memory changes only with recorded A/B measurements on physical cores and
   identical exact roots in dimensions 4 to 256, BALL and binary64 digests and native SICX whole-file SHA-256.
   Never silently rebaseline canonical bytes in fixtures or goldens.
   Keep certification, precision, retries and fallback policy fixed. Measure
   the peak memory and parallel efficiency in dimension 512 alongside the wall time. Checked in review.

8. **Documentation and tests follow the code.** Check every documented path,
   command, option, default, test name and label against the code; prefer generated
   references. Test every public contract and register every test explicitly.
   `sic_test_policy_test_registry` checks test labels and documented selectors. The documentation checks
   below check links, examples and help drift; `sic_test_support_help_defaults`
   checks help defaults. Enforce each documented-claim check with a policy CTest
   where one exists; review covers claims without one, contract coverage and
   explicit registration.

## Build and test

Use the [plain and FLINT build/install recipes](docs/install.md). Run the full CTest suite for
affected configurations. Production builds compile fault hooks out. Fault-injection controls require
a separate `SIC_ENABLE_TEST_HOOKS=ON` build, which cannot be installed and needs CMake 3.21. Do not
define the hook macro manually. Run `ctest -L test_hooks` without excluding heavy controls.

## Changes and evidence

Keep pull requests focused and report the validation commands and their results. Nontrivial code,
mathematical arguments and computational results need an independent review. For benchmarks,
record the inputs and their checksums, the dimension, precision, seed, threads, hardware, command
and certification status.

Follow the [Weyl–Heisenberg convention](docs/sicx.md#mathematical-conventions) and distinguish proved bounds,
conditional results, certified numerical bounds, exact-zero certificates and
empirical measurements. State premises, cite external mathematical claims and
update the relevant `docs/` pages; [verification](docs/verification.md) defines the status words.

## Offline documentation checks

With Node.js installed, build the project and run `ctest --test-dir cpp/build -L docs
--output-on-failure` and `ctest --test-dir cpp/build -L policy --output-on-failure`. Replace
`cpp/build` with your configured build directory. Also run `ctest --test-dir cpp/build -L hygiene
--output-on-failure`. `docs/tools.md` is generated from executable help by `scripts/docs/help.mjs`;
its drift CTests check every available production tool and the whole file when all tools are built.
Change parser help first, then regenerate the reference. Place `<!-- example -->` immediately before
a fenced `cpp` block containing a complete, zero-argument C++20 program; CMake compiles and CTest
runs it. Standalone examples in `cpp/examples` are explicitly registered in
`cpp/cmake/checks/Docs.cmake`; register the source, executable, test and any fixture
arguments there. The status and private-reference lints use exact `path:trimmed
source line` entries in `scripts/docs/allow_list.txt` for reviewed exceptions. Lint findings require
human review: proximity is not itself a claim of incorrect wording.

<a id="test-selection-and-independence"></a>
## Test selection and independence

Configure with `SIC_BUILD_TESTS=ON` (the default), build, then run
`ctest --test-dir cpp/build --output-on-failure`. Enable `SIC_ENABLE_FLINT`
for the exact-arithmetic, producer, decoder, and release suites.

Every registered test has one primary CTest label:

- `unit`: numerical kernels, exact arithmetic, resource accounting, and codecs.
- `integration`: tools, data readers, independent checkers, and format operations.
- `e2e`: producer goldens, certified decoding, kill/resume, and release graphs.

Every test also has an owning component label matching its name after
`sic_test_` (for example `sicx`, `data` or `policy`). Topic labels refine
selection: `docs` selects offline documentation checks and examples;
`independence` selects checker boundary checks. Topic names
`sic-container`, `release` and `regression` are not selectors.
`sic_test_policy_test_registry` checks documented selectors against the final
CTest registry, primary labels, owners, hook enrollment, timeouts and the heavy
membership below. The `crt` selector in `docs/` requires FLINT; `hoggar`
is available in plain builds too. Cross-mode hook registration and stale
enrollment checks require a full FLINT configuration (`SIC_BUILD_APPS=ON`,
`SIC_BUILD_REGRESSION=ON`): the policy configures the opposite hook mode and
the optional PARI/GP registration without building or running either. This covers
entries absent from a plain or reduced registry; every mode validates the
properties of its registered tests.

Hook controls are labeled `test_hooks` in the separate
`SIC_ENABLE_TEST_HOOKS=ON` tree and included in its complete suite. Every test
registered only in ON belongs to the explicit hook-control list.
In FLINT builds, when registered,
`sic_test_construct_failure_categories`, `sic_test_exact_storage_failure_categories`,
`sic_test_exact_failure_categories` and `sic_test_support_cli_failure_categories`
are registered in both modes and gain `test_hooks` only in ON. Hook-mode
isolation, compiler and install policy checks run in their applicable modes.

`heavy` has this explicit membership (patterns match complete test names):

<!-- test-heavy -->
- `^sic_test_search_clifford_fiducials_d(128|256)$`: Large Clifford collections
- `^sic_test_search_produce_d256$`: Exact production for the Clifford collection in dimension 256
- `^sic_test_construct_native_s6_phased_(default_t(1|4)(_repeat_2)?|standard_t4)$`: construction controls
  in dimension 64
- `^sic_test_construct_native_s7_(phased_default_t4(_repeat_2)?|auto_standard_t4)$`: construction
  controls in dimension 128
- `^sic_test_construct_memory_strategy_identity_(s[2-6]|reduced)$`: Retention identity controls
- `^sic_test_sicx_exact_gp_d16$`: Opt-in PARI/GP verification
<!-- /test-heavy -->

Ordinary tests, including policy tests and default heavy controls, have a
positive timeout of at most 120 seconds. The sole long-test exemption is
`sic_test_sicx_exact_gp_d16`: explicitly opt in with `SIC_SICX_GP_D16=ON`;
it is heavy and retains a 3600-second timeout (one PARI/GP shift takes several
minutes empirically). Its registration is checked without requiring the long
computation. Use `ctest -L independence` for checker boundaries, or
`ctest -LE heavy` for the ordinary suite. The publication manifest pins the
copied test inputs.

`SIC_TEST_WORKERS` is an optional positive CMake cache integer. When unset or
empty, the parallel-product test discovers its process CPU allowance at run time
and uses `max(3, allowance)` native FLINT workers. The hooks install-refusal test
uses the same runtime default for nested build jobs. An explicit value (for
example `-DSIC_TEST_WORKERS=8`) is honoured unchanged and never capped. The
parallel-product coverage guard fails loudly below three workers; even a
one-CPU test process exercises helper leases. Tile requests `{1,2,8,16,32}` and
explicit worker lists in other controls retain their values. Run oversubscribed
controls with `ctest -j1`. Use atomic `mkdtemp` directories for scratch paths
shared across process namespaces, or paths scoped to the configured CTest build
directory.
Tests requiring an optional external program must report SKIPPED with a clear
reason when it is unavailable, and use its configured executable path when available.

`SIC_BUILD_REGRESSION=OFF` explicitly disables golden suites. Set
`SIC_BUILD_BENCH=ON` to build `bench-overlaps` and `bench-verify`; benchmarks
are not CTests. Research programs are not required to build or test the production tree.

One bounded integration regression exceeds the per-test CTest limit and is
run by hand: `cpp/tests/construct/canonical/run_trace_band_identity.cmake`
builds the maximal order in dimension 256 (the smallest mixed-radix tower) unbanded and with a
forced representative band, and requires identical bytes. Run it with a
`SIC_ENABLE_TEST_HOOKS=ON` FLINT producer as `cmake -DPROGRAM=<sic-construct>
-DWORK=<fresh directory> -P cpp/tests/construct/canonical/run_trace_band_identity.cmake`
(about 6 minutes on 32 CPUs).

<a id="independent-checker-tests"></a>
## Independent checker tests

The independent checkers ([verification](docs/verification.md#independent-checker-contracts)) have
their own test suites and test-only writers implemented from the specification.

### Chinese remainder theorem certificate

`ctest -L crt` exercises eleven input groups (valid, rejection mutations, boundaries/regrouping,
partial trust/scope, amendments, primitive guards, mixed odd/quadratic branches, targeted guards,
general dimensions, deterministic fuzz mutations, and public command-line interface), five fixture CRT proofs,
bound-only diagnostics, strict command-line option and error cases, help/parser defaults, help, source
independence, a shift retry, and three deliberate mathematical negative controls. The independent
executables are enrolled in the repository’s independence audit. Rehashed negative fixtures pin
rejection categories so a different later failure cannot silently substitute for the intended check.
Five additional `sic-test-crt-faults` cases interpose OS/FLINT failures to test otherwise rare
fail-closed paths; interposition is test-only.

`test_writer.hpp` is a test-only writer implemented from the final specification. It uses OpenSSL
SHA-256 independently of the reader's hash implementation and emits synthetic general-dimension exact-only
containers and d=4/d=8 finalized container controls. Tests include multi-leaf elements in different
physical groupings, unchanged exact-prefix/LDIR bytes on finalization, odd-degree/action grammar,
exact rounding ties of both signs, and a minimum-precision-256 branch construction. These are
container controls, not new SIC-POVM claims. d=2 balanced pairs are also covered.

`sic-test-crt-input --write-fixtures DIR` saves eight reviewable d=4/d=8 fixtures.
`sic-test-crt-external OUTDIR FILE...` verifies an executable-produced corpus, reproduces every byte
with the independent OpenSSL writer, checks both roots and whole-file hashes, rejects raw and
independently rehashed corruptions, and exercises partial trust/completion. `sic-test-crt-input
--write-general-fixtures DIR` saves the general-dimension fixtures for d=2,3,5,6,7,9,12,16.
Exhaustive unit-group/entry oracles include faithful C4/C6 factors, composite CRT lifts, full
exponent digits, both real integral bases, rational pair rejection and the first-coordinate-first
Smith label rule. The selection of CRT, input and independence tests includes 19 mixed-branch
checks, signed initially nonexact half ties, unresolved singleton fallback and low-degree admission
controls.

### Norm certificate

`sic-test-norm-reader SCRATCH_DIRECTORY [4|8]` writes its own fixtures and mutation corpus.
`test_writer.hpp` uses the SHA-256 primitive but implements its own JSON serialization, encodings,
directories, domain messages and RFC tree. Known SHA and an externally computed tagged-leaf hash
constrain that shared primitive. The generated `d4.sicx` and `d8.sicx` use the genuine quadratic
extension `K(sqrt(2))/K` and its nontrivial C2 action. Their representatives are constant, their
BALLs enclose the uniform unit vector, and they are **not SIC-POVM fixtures**. The larger grouping
fixtures deliberately exercise container checks with a synthetic tower; they do not establish the
imported field and group premises. These synthetic fixtures make no SIC-POVM claim.

### Second reader

`cpp/checkers/verifier/tests/writer.cpp` contains a spec-derived test-only writer with separate byte, integer, hash-domain
and RFC tree encoders; its hashes use OpenSSL. The fixture suite in `cpp/checkers/verifier/tests/tests.cpp` and
`cpp/checkers/verifier/tests/conformance_*.cpp` uses it to create small synthetic containers at d=4 and d=8, finalization
pairs, a 65,536-coordinate support for physical regrouping, odd cyclic towers, negative-D0 pairs and
mutations. These are format/decoder controls, not newly constructed SIC-POVM fiducial vectors. The fixture suite uses no
production producer or production fixtures. `cpp/checkers/verifier/tests/general_tests.cpp` compares entries with
exhaustive direct-product enumeration and smallest-lift scans, covering d=2,3,5,6,7,9,12 and powers
of two, plus additional CRT cases. `cpp/checkers/verifier/tests/guard_tests.cpp` checks allocation admission, protected
JSON input spans and test-only one-shot libc/FLINT failures. These are specification/conformance
controls, separate from actual SIC-POVM proof runs.

## Adding fixtures

A fixture is an authenticated test input with a stated scope. Add it to the owning test family and
checksum manifest in the same change; preserve existing fixtures and logs. Record the producing
source/tool identity, input hashes, complete command and byte-affecting options, dimension/order,
precision, seed, native threads, resource settings, exact root, file root and whole-file hash as
applicable. Include a machine-independent explanation of mathematical premises and whether the
fixture is a SIC, a non-SIC control, or synthetic conformance data.

For numerical fixtures, pin normalized ball inputs and every coordinate word. Verify
CSV/NPY/MAT/HDF5 readbacks using the independent readers; test changed digits/flipped bits,
malformed/truncated and trailing data, failures without publication, and the correct anchor
gauge/error scope. Update `SHA256SUMS` explicitly when authenticated metadata changes; unchanged
coordinates retain their byte identity. Supply all committed fixtures: missing inputs fail rather
than silently disabling coverage.

Register source/include/link independence checks in CMake/CTest for new independent-checker fixture
writers and add rehashed mutation controls that fail at the intended gate. Container fixtures need
not be SIC-POVM fiducial vectors; say so. Exact certificates must identify the exact root actually checked and report
refusal, estimate and partial modes separately from a proof. Obtain independent review for
nontrivial fixture or certificate changes.

The policy checks also enforce the [command-line contract inventory](docs/cli_contract.json),
[configuration census](docs/configuration.md) and
[install inventory](docs/install_contract.json). `sic_test_policy_help_contract`
checks each selected subcommand's option operands/repeatability and grammar;
`sic_test_policy_source_claims` checks parser declarations against reads, the
runtime/CMake input census and the documented read owners. `sic_test_policy_documented_defaults` binds default
claims to their source authorities and runs the compiled omitted-option checks.
`sic_test_support_docs_links` checks explicit code/data pointers as well as links;
`sic_test_support_docs_fixture_inventory` reconciles purpose, checksums and build copies.
`sic_test_policy_schema_smoke` validates every schema family against fresh d4/d8
outputs and retained exceptional and numerical metadata. `sic_test_policy_shell_examples`
runs blocks tagged `<!-- shell:run -->` with explicit resources; other shell
blocks must be marked `<!-- shell:template -->` and describe their bindings.
The install policy reuses the relocated consumer owner. All tests named `sic_test_policy_*` have
negative controls enumerated in `scripts/docs/policy_controls.json`;
`sic_test_policy_claim_registry` rejects an unenrolled policy and requires both
exit status and output verdict for every regex-based success assertion.

To update generated contracts after an intentional parser or source change, use
an exact build containing every public tool:

<!-- shell:template -->
```sh
node scripts/docs/contracts/generate_contracts.mjs --write cpp/build-off
node scripts/docs/contracts/generate_contracts.mjs --check cpp/build-off
```

Review the generated diff and update prose to agree with source values. The
locator specifications in `scripts/docs/contracts/contract_sources.json` and
`scripts/docs/contracts/default_sources.json` classify parser owners and prose authorities;
new owners or claims need explicit enrollment. Semantic comparisons in
`scripts/docs/contracts/default_values.mjs` bind numeric prose to source expressions with
unit conversion. The generated command-line inventory contains grammar metadata;
[command help](docs/tools.md) remains the generated help reference. Regenerate
that reference with `scripts/docs/help.mjs --write` and the installed prefix.
