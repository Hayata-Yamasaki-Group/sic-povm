# Command-line tools

Generated from the tools’ `--help` output by `scripts/docs/help.mjs`.

<!-- cli:sic-construct -->
## sic-construct

```text
usage: sic-construct S --out FILE [OPTIONS] (default S=1..12; only d=2^S construction)
glibc heap policy: trim 16 MiB, mmap 32 MiB before threads; any MALLOC_* or glibc.malloc.* tunable disables both defaults.

Options (available in the forms listed above):
  --out FILE  Required new output; its parent must be writable.
  --order standard|maximal  Default order follows the canonical dimension profile.
  --threads N  Positive worker limit, honored subject to memory admission.
  --memory-mib M  Positive finite memory cap in MiB; default discovered budget.
  --maximum-exponent S  Construction dimension allowance (default 12; wire maximum 20).
  --maximum-precision BITS  Computational branch precision ceiling (default 65536; 256..1152921504606846975).
  --valuation-table-mib M  Per-table rescaling allowance (default 4096 MiB).
  --valuation-total-mib M  Total rescaling allowance (default 8192 MiB).
  --generator-selector step-selection|conjugate-alignment  Generator selection (default conjugate-alignment).
  --alignment-memory-mib M  Complete incremental search budget in MiB (default 16384); cache when it fits, replay otherwise.
  --alignment-cache-mib M  Optional F-cache ceiling in MiB; 0 forces replay.
  --alignment-batch-mib M  Scoring-batch target in MiB (default 256).
  --alignment-worker-mib M  Candidate-worker target in MiB (default 256).
  --alignment-train N  Training leaves per representative channel (default 64; 1..1000000).
  --alignment-candidates N  Candidate cap per level (default 2048; 1..1000000).
  --alignment-sweeps N  Coordinate-descent sweeps (default 4; 0..64).
  --alignment-seed N  Search seed (default 20261104).
  --alignment-trace  Log high-water memory checks during search.
  --resume  Adopt an existing partial output (S>=2).
  --group-cache FILE  Verified group checkpoint.
  --stop-file FILE  Stop cooperatively while this file exists.
  --progress  Print progress diagnostics.
  --radicals FILE  S=1 radical record override; default embedded pinned d2.sicrad.
  --log-admissions  Log memory, disk and worker admission decisions to stderr.
  --analytic-cache DIR  Coarse analytic checkpoint.
  --completed-class-cache DIR  Current-producer certified class cache.
  --require-completed-cache  Require all selected completed classes.
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-construct -->

<!-- cli:sic-check -->
## sic-check

```text
usage: sic-check FILE|- [TOL=1e-10]

Options (available in the forms listed above):
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-check -->

<!-- cli:sic-overlaps -->
## sic-overlaps

```text
usage: sic-overlaps FILE [OPTIONS]

Options (available in the forms listed above):
  --tol T  Nonnegative finite tolerance (default 1e-10).
  --vectors  Stream all frame vectors.
  --table OUT.bin  Write overlaps as complex128.
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-overlaps -->

<!-- cli:sic-identify -->
## sic-identify

```text
usage: sic-identify FILE [OPTIONS]

Options (available in the forms listed above):
  --catalogue JSON|TSV  Catalogue file (default REPO/data/catalogue.json).
  --repo DIR  Data root; search installed data, then current directory when omitted.
  --tol T  Positive finite match tolerance (default 1e-06).
  --entry-error E  Nonnegative finite entry error (default 8.881784197001252e-16).
  --moments K  Moment count, 1..64 (default 4).
  --unitary-only  Search unitary elements only.
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-identify -->

<!-- cli:sic-stabilizer -->
## sic-stabilizer

```text
usage: sic-stabilizer FILE [OPTIONS] | sic-stabilizer --dim D [OPTIONS]

Options (available in the forms listed above):
  --catalogue JSON|TSV  Catalogue file (default REPO/data/catalogue.json).
  --repo DIR  Data root; dimension mode searches installed data, then current directory; file mode default ".".
  --out FILE  Output stabilizer file; write stdout when omitted.
  --dim D  Positive dimension in catalogue mode; required when FILE omitted.
  --tol T  Positive finite match tolerance (default 1e-06).
  --entry-error E  Nonnegative finite entry error (default 8.881784197001252e-16).
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-stabilizer -->

<!-- cli:sic-decode -->
## sic-decode

```text
usage: sic-decode FILE.sicx OUTDIR [OPTIONS]

Options (available in the forms listed above):
  --rays all|L1,L2,...  Ray labels (default all).
  --coordinates all|A-B|A,...  Coordinates/ranges, comma separated (default all).
  --maximum-precision BITS  Computational precision ceiling (default 65536; 256..1152921504606846975).
  --target-bits BITS  Accuracy within --maximum-precision (default 128; minimum 64).
  --guard-bits BITS  Additional working bits, at most --maximum-precision (default PREC hint or 64).
  --initial-bits BITS  Base precision, at most --maximum-precision (default PREC hint or min(2048, --maximum-precision), raised to target plus guard bits; minimum explicit 64).
  --threads T  Worker count, 1..1024 (default process CPU allowance, capped at 32).
  --memory-mib M  Positive memory budget in MiB (default 16384).
  --no-pin  Disable worker pinning.
  --stop-file FILE  Poll for cancellation at decoder task boundaries.
  --log-admissions  Log memory admission decisions to stderr.
  --profile  Enable decoder profiling.
  --tower auto|resident|stream  Tower storage policy (default auto).
  --mem-policy bind:N|preferred:N|interleave  Process NUMA memory policy; node index 0..63.
  --check-budget  Fail when peak RSS exceeds the budget.
  --finalize  Append certified finalization records in place.
  --resume  Resume interrupted finalization.
  --working-bits B  Finalization precision, 1..1048576 (default 256).
  --ray LABEL WORD ANCHOR  Repeatable ray: WORD comma-separated unsigned integers; ANCHOR >= 0 is required.
  --remote PARTS.json  Read a local manifest through remote byte access.
  --source URL|DIR  Remote parts source override.
  --cache DIR  Remote extent cache; use XDG cache or ~/.cache/sicx when omitted.
  --range-jobs N  Concurrent requests, 1..32 (default 4).
  --range-bytes N  Extent size, 1..8388608 (default 1048576).
  --coalesce-gap N  Maximum scheduled gap bytes, 0..8388608 (default 4096).
  --prefetch-mib N  Disk lookahead, 0..256 MiB (default 8; 0 disables).
  --cache-max-ranges N  Extent/plan cap, 1..1048576 (default 262144).
  --remote-max-fraction F  Missing-byte fraction, (0,1] (default 0.5).
  --remote-timeout-ms N  Per-attempt timeout in milliseconds (default 30000).
  --remote-retries N  Additional attempts, 0..10 (default 3).
  --expect-exact-root HEX  Trusted exact-root pin.
  --expect-root HEX  Trusted full-root pin.
  --fetch-full  Permit verified full-download fallback.
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-decode -->

<!-- cli:sic-verify -->
## sic-verify

```text
usage: sic-verify FILE [OPTIONS]

Options (available in the forms listed above):
  --maximum-precision BITS  Computational precision ceiling (default 65536; 256..1152921504606846975).
  --threads T  Positive worker count (default 8).
  --quick  Verify footer, index, Merkle root and EXMT only.
  --sic-check  Add a binary64 SIC diagnostic.
  --rescaling  Check canonical 2-adic rescaling.
  --remote PARTS.json  Read a local manifest through remote byte access.
  --source URL|DIR  Remote parts source override.
  --cache DIR  Remote extent cache; use XDG cache or ~/.cache/sicx when omitted.
  --range-jobs N  Concurrent requests, 1..32 (default 4).
  --range-bytes N  Extent size, 1..8388608 (default 1048576).
  --coalesce-gap N  Maximum scheduled gap bytes, 0..8388608 (default 4096).
  --prefetch-mib N  Disk lookahead, 0..256 MiB (default 8; 0 disables).
  --cache-max-ranges N  Extent/plan cap, 1..1048576 (default 262144).
  --remote-max-fraction F  Missing-byte fraction, (0,1] (default 0.5).
  --remote-timeout-ms N  Per-attempt timeout in milliseconds (default 30000).
  --remote-retries N  Additional attempts, 0..10 (default 3).
  --expect-exact-root HEX  Trusted exact-root pin.
  --expect-root HEX  Trusted full-root pin.
  --fetch-full  Permit verified full-download fallback.
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-verify -->

<!-- cli:sic-inspect -->
## sic-inspect

```text
usage: sic-inspect FILE MODE [OPTIONS] | sic-inspect --exact-json IN.sicx OUT.json [OPTIONS] | sic-inspect --catalogue ROOT

Options (available in the forms listed above):
  --text-json OUT|-  Dump canonical JSON Lines.
  --part-bytes N  text-json: positive part byte limit (default 0: unsplit).
  --threads T  text-json: positive worker count (default 1).
  --json-rays DIR  Write per-ray JSON; requires --multiplet mN.
  --json-all OUT  Write all-in-one JSON; requires --multiplet mN.
  --exact-json  Write exact.json; two positional paths IN and OUT.
  --multiplet N|mN  json-rays/json-all/exact-json: required mN; release-layout: positive N.
  --no-check  exact-json: skip numerical check.
  --block I  Write text-form chunk at nonnegative index I.
  --entry I  Write index entry and audit path at nonnegative index I.
  --release-layout ROOT  Write release tree.
  --provenance-conductors LIST  release-layout: provenance conductor list.
  --provenance-conductor C  release-layout: chosen conductor.
  --assets-dir DIR  release-layout: assets directory (default "": hash only).
  --release-repo OWNER/REPO  release-layout: repository (default OWNER/REPOSITORY).
  --release-tag TAG  release-layout: tag (default v1.0.0).
  --part-size N  release-layout: positive part byte count (default 1900000000).
  --catalogue ROOT  Rebuild catalogue.json.
  --info  Show header, EXMT and index.
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-inspect -->

<!-- cli:sic-container -->
## sic-container

```text
usage: sic-container split FILE --out DIR --asset-prefix d<d>-m<N> [OPTIONS]
       sic-container fetch|repair d<d>/m<N> [OPTIONS]
       sic-container from-text TEXT.jsonl|TEXT.parts.json OUT.sicx
       sic-container merge-parts|merge-element-parts OUT.sicx PART...
       sic-container reencode IN.sicx OUT.sicx [--threads N] [--maximum-precision BITS]
       sic-container read-range FILE|--remote PARTS.json OFFSET LENGTH [OPTIONS]

Options (available in the forms listed above):
  --out DIR  split: required output directory; fetch/repair: default build/data/SPEC.
  --asset-prefix PREFIX  split: d<d>-m<N> asset prefix.
  --repository OWNER/REPO  split: repository for the parts manifest (default OWNER/REPOSITORY).
  --tag TAG  split: release tag (default v1.0.0).
  --emit-and-upload CMD  split: upload command.
  --confirm-size-cmd CMD  split: required remote-size confirmation command with --emit-and-upload.
  --part-size N  split: fixed ceiling (must be 1900000000).
  --source URL|DIR  fetch/repair/read-range: source override.
  --maximum-precision BITS  reencode: computational precision ceiling (default 65536; 256..1152921504606846975).
  --threads J  fetch/repair/reencode: 1..64 workers; fetch/repair (default 4); reencode: default 1.
  --data-root DIR  fetch/repair: manifest root (default data).
  --parts FILE  fetch/repair: parts manifest (default DATA_ROOT/SPEC/multiplet.sicx.parts.json).
  --name NAME  fetch/repair: output name (default multiplet.sicx).
  --extra-bytes N  fetch: extra required disk bytes (default 0).
  --force  fetch: replace a differing file.
  --remote PARTS.json  Read a local manifest through remote byte access.
  --cache DIR  Remote extent cache; use XDG cache or ~/.cache/sicx when omitted.
  --range-jobs N  Concurrent requests, 1..32 (default 4).
  --range-bytes N  Extent size, 1..8388608 (default 1048576).
  --coalesce-gap N  Maximum scheduled gap bytes, 0..8388608 (default 4096).
  --prefetch-mib N  Disk lookahead, 0..256 MiB (default 8; 0 disables).
  --cache-max-ranges N  Extent/plan cap, 1..1048576 (default 262144).
  --remote-max-fraction F  Missing-byte fraction, (0,1] (default 0.5).
  --remote-timeout-ms N  Per-attempt timeout in milliseconds (default 30000).
  --remote-retries N  Additional attempts, 0..10 (default 3).
  --expect-exact-root HEX  Trusted exact-root pin.
  --expect-root HEX  Trusted full-root pin.
  --fetch-full  Permit verified full-download fallback.
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-container -->

<!-- cli:sic-container split -->
## sic-container split

```text
usage: sic-container split FILE --out DIR --asset-prefix d<d>-m<N> [--emit-and-upload CMD --confirm-size-cmd CMD] [OPTIONS]

Options (available in the forms listed above):
  --out DIR  split: required output directory; fetch/repair: default build/data/SPEC.
  --asset-prefix PREFIX  split: d<d>-m<N> asset prefix.
  --repository OWNER/REPO  split: repository for the parts manifest (default OWNER/REPOSITORY).
  --tag TAG  split: release tag (default v1.0.0).
  --emit-and-upload CMD  split: upload command.
  --confirm-size-cmd CMD  split: required remote-size confirmation command with --emit-and-upload.
  --part-size N  split: fixed ceiling (must be 1900000000).
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-container split -->

<!-- cli:sic-container fetch -->
## sic-container fetch

```text
usage: sic-container fetch d<d>/m<N> [OPTIONS]

Options (available in the forms listed above):
  --out DIR  split: required output directory; fetch/repair: default build/data/SPEC.
  --source URL|DIR  fetch/repair/read-range: source override.
  --threads J  fetch/repair/reencode: 1..64 workers; fetch/repair (default 4); reencode: default 1.
  --data-root DIR  fetch/repair: manifest root (default data).
  --parts FILE  fetch/repair: parts manifest (default DATA_ROOT/SPEC/multiplet.sicx.parts.json).
  --name NAME  fetch/repair: output name (default multiplet.sicx).
  --extra-bytes N  fetch: extra required disk bytes (default 0).
  --force  fetch: replace a differing file.
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-container fetch -->

<!-- cli:sic-container repair -->
## sic-container repair

```text
usage: sic-container repair d<d>/m<N> [OPTIONS]

Options (available in the forms listed above):
  --out DIR  split: required output directory; fetch/repair: default build/data/SPEC.
  --source URL|DIR  fetch/repair/read-range: source override.
  --threads J  fetch/repair/reencode: 1..64 workers; fetch/repair (default 4); reencode: default 1.
  --data-root DIR  fetch/repair: manifest root (default data).
  --parts FILE  fetch/repair: parts manifest (default DATA_ROOT/SPEC/multiplet.sicx.parts.json).
  --name NAME  fetch/repair: output name (default multiplet.sicx).
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-container repair -->

<!-- cli:sic-container read-range -->
## sic-container read-range

```text
usage: sic-container read-range FILE|--remote PARTS.json OFFSET LENGTH [OPTIONS]

Options (available in the forms listed above):
  --remote PARTS.json  Read a local manifest through remote byte access.
  --source URL|DIR  Remote parts source override.
  --cache DIR  Remote extent cache; use XDG cache or ~/.cache/sicx when omitted.
  --range-jobs N  Concurrent requests, 1..32 (default 4).
  --range-bytes N  Extent size, 1..8388608 (default 1048576).
  --coalesce-gap N  Maximum scheduled gap bytes, 0..8388608 (default 4096).
  --prefetch-mib N  Disk lookahead, 0..256 MiB (default 8; 0 disables).
  --cache-max-ranges N  Extent/plan cap, 1..1048576 (default 262144).
  --remote-max-fraction F  Missing-byte fraction, (0,1] (default 0.5).
  --remote-timeout-ms N  Per-attempt timeout in milliseconds (default 30000).
  --remote-retries N  Additional attempts, 0..10 (default 3).
  --expect-exact-root HEX  Trusted exact-root pin.
  --expect-root HEX  Trusted full-root pin.
  --fetch-full  Permit verified full-download fallback.
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-container read-range -->

<!-- cli:sic-container from-text -->
## sic-container from-text

```text
usage: sic-container from-text TEXT.jsonl|TEXT.parts.json OUT.sicx

Options (available in the forms listed above):
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-container from-text -->

<!-- cli:sic-container merge-parts -->
## sic-container merge-parts

```text
usage: sic-container merge-parts OUT.sicx PART...

Options (available in the forms listed above):
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-container merge-parts -->

<!-- cli:sic-container merge-element-parts -->
## sic-container merge-element-parts

```text
usage: sic-container merge-element-parts OUT.sicx PART...

Options (available in the forms listed above):
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-container merge-element-parts -->

<!-- cli:sic-container reencode -->
## sic-container reencode

```text
usage: sic-container reencode IN.sicx OUT.sicx [OPTIONS]

Options (available in the forms listed above):
  --maximum-precision BITS  reencode: computational precision ceiling (default 65536; 256..1152921504606846975).
  --threads J  fetch/repair/reencode: 1..64 workers; fetch/repair (default 4); reencode: default 1.
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-container reencode -->

<!-- cli:sic-data -->
## sic-data

```text
usage: sic-data COMMAND [ARGS]
       COMMAND: formats, write, check, bundle, check-bundle, layout, catalogue, list
       sic-data COMMAND --help shows positional grammar and options

Options (available in the forms listed above):
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-data -->

<!-- cli:sic-data formats -->
## sic-data formats

```text
usage: sic-data formats

Options (available in the forms listed above):
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-data formats -->

<!-- cli:sic-data write -->
## sic-data write

```text
usage: sic-data write RAY.json OUTDIR [--formats LIST]

Options (available in the forms listed above):
  --formats LIST  Write npy,mat,csv,h5 (default npy,mat,csv).
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-data write -->

<!-- cli:sic-data check -->
## sic-data check

```text
usage: sic-data check RAY.json FILE...

Options (available in the forms listed above):
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-data check -->

<!-- cli:sic-data bundle -->
## sic-data bundle

```text
usage: sic-data bundle OUT.npz D [RAY.json...] [--exceptional RAY.json GROUP]...
       At least one regular or exceptional ray is required.

Options (available in the forms listed above):
  --exceptional RAY.json GROUP  Exceptional ray and covariance group.
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-data bundle -->

<!-- cli:sic-data check-bundle -->
## sic-data check-bundle

```text
usage: sic-data check-bundle FILE.npz [RAY.json...] [--exceptional RAY.json]...
       At least one regular or exceptional ray is required.

Options (available in the forms listed above):
  --exceptional RAY.json  Exceptional ray.
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-data check-bundle -->

<!-- cli:sic-data list -->
## sic-data list

```text
usage: sic-data list CATALOGUE.json

Options (available in the forms listed above):
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-data list -->

<!-- cli:sic-data layout -->
## sic-data layout

```text
usage: sic-data layout INPUT.sicx ROOT --multiplet N [--threads T]

Options (available in the forms listed above):
  --multiplet N  Positive multiplet number (required).
  --threads T  Worker count (default 1; 1..65536).
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-data layout -->

<!-- cli:sic-data catalogue -->
## sic-data catalogue

```text
usage: sic-data catalogue ROOT

Options (available in the forms listed above):
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-data catalogue -->

<!-- cli:sic-check-balls -->
## sic-check-balls

```text
usage: sic-check-balls BALLS [OPTIONS]

Options (available in the forms listed above):
  --maximum-dimension D  Dimension policy (default 2048; 2..4611686018427387903).
  --maximum-work N  Work score policy (default 10000000000).
  --maximum-precision W  Precision policy (default 4096; 64..1152921504606846975).
  --memory-mib M  Workspace limit in MiB (default 512).
  --bits B  Accuracy bits (default 100; 1..maximum-precision minus 40; default maximum 4056).
  --working-bits W  Working bits (default 256; minimum 64, at most maximum-precision; default ceiling 4096).
  --version  Print tool identity as canonical JSON and exit 0.
  --help  Print this help to stdout and exit 0.

Exit codes: 0 success; 1 failed check or runtime/input error; 2 usage error.
```
<!-- /cli:sic-check-balls -->

<!-- cli:sic-certify-stabilizer -->
## sic-certify-stabilizer

```text
usage: sic-certify-stabilizer STABILIZER_FILE [options]
  --catalogue JSON|TSV    ray catalogue (default REPO/data/catalogue.json)
  --repo DIR              data root (default installed data, then current directory)
  --prec BITS             ball precision, 64..100000 (default 384)
  --threshold T           positive overlap tolerance (default 2^-100)
  --d2-exact SICRAD        also verify d2 elements exactly from radical data
  --version               print canonical tool identity and exit 0
  --help                  print help to stdout and exit 0
```
<!-- /cli:sic-certify-stabilizer -->

<!-- cli:sic-verify-independent -->
## sic-verify-independent

```text
usage: sic-verify-independent FILE [--text FILE] [--decode auto|full|ratio|off] [--ray LABEL] [--max-steps N] [--extra-bits N] [--release] [--partial-element ID] [--expect-exact-root HEX] [--expect-file-root HEX] [--expect-sha256 HEX] [--branch-degree-limit N] | --selftest
  --help  Print help and exit 0.
  --version  Print canonical tool identity and exit 0.
Exit codes: 0 scoped pass, 2 invalid data or usage, 3 resource/precision refusal.
```
<!-- /cli:sic-verify-independent -->

<!-- cli:sic-certify-crt -->
## sic-certify-crt

```text
usage: sic-certify-crt FILE.sicx [options]
Unconditional CRT exactness certificate: proves that every Galois word (hence every ray) of the multiplet
stored in FILE.sicx is an exact Weyl-Heisenberg SIC fiducial (d = 2^s >= 4).
options:
  --exact-root HEX        expected trusted exact root (64 lowercase hex digits)
  --file-root HEX         expected trusted file root
  --sha256 HEX            expected trusted whole-file SHA-256
  --threads T             worker threads (default 1)
  --seed S                seed for the choice of primes (default 1; any distinct primes give a valid proof)
  --bound-only            compute bounds and prime count using split/reconstruction primes; skip residual certificate primes
  --integral-max-dim N    run Lemma I (integral generators) at levels with 2[F_j:K] <= N (default 8192)
  --no-shift-hints        ignore the EXMT generator_rescaling hints (start every level at J = 0)
  --primes K              run K primes instead of the bound (timing diagnostic; never a certificate: exit 3)
  --prec P                initial ball precision in bits (default: max_bits + 192, doubled on failure)
  --inject rep|act|conj   fault injection for negative-control tests (must fail)
  --version               print canonical tool identity and exit 0
  --help                  print this help
exit codes: 0 proved (or --bound-only done), 1 failed/refused/error, 2 usage, 3 incomplete (--primes), 4 undetected fault injection
```
<!-- /cli:sic-certify-crt -->

<!-- cli:sic-certify-norm -->
## sic-certify-norm

```text
usage: sic-certify-norm FILE.sicx [options]
  --threads T             worker threads (default 1)
  --estimate              estimate the required precision without proving SIC identities
  --leaf-estimate         estimate leaf heights without proving SIC identities
  --ray I                 check only zero-based ray index I (default all)
  --max-bits B            precision cap (default 134217728)
  --verify-input          verify the container only
  --partial               verify directories and EXMT only
  --partial-element ID    partially verify the requested element (repeatable)
  --partial-chunk ID      partially verify the requested physical chunk (repeatable)
  --exact-root HEX        expected trusted exact root
  --file-root HEX         expected trusted file root
  --sha256 HEX            expected trusted whole-file SHA-256
  --max-dimension D       input dimension limit (default 4096)
  --max-field-dimension D input field dimension limit (default 65536)
  --version               print canonical tool identity and exit 0
  --help                  print help to stdout and exit 0
```
<!-- /cli:sic-certify-norm -->

<!-- cli:sic-check-crt-input -->
## sic-check-crt-input

```text
usage: sic-check-crt-input FILE [--exact-root HEX] [--file-root HEX] [--sha256 HEX] [--partial-element ID | --partial-chunk ID | --partial-metadata]
  --help  Print help and exit 0.
  --version  Print canonical tool identity and exit 0.
Element/chunk requests may be combined and repeated; --partial-metadata is exclusive.
Exit codes: 0 scoped pass, 1 invalid data, 2 usage, 3 resource/precision refusal.
```
<!-- /cli:sic-check-crt-input -->
