<a id="reproducing-the-published-results"></a>
# Reproduction

This page gives the commands that construct, finalize and check every multiplet of the release and
rebuild the numerical data in `data/`, and the time and memory that the construction and the
finalization of each multiplet took for the release.
Install the FLINT-enabled configuration as described in [installation](install.md) and put its `bin`
folder on `PATH`. [Verification](verification.md) defines the status of each check, and the
[data guide](data.md) describes the files.

## Selecting and finalizing rays

The producer derives class representatives in the native Smith basis after
Group closure. It writes a [ray list](schemas/sic-ray-list-1.schema.json) bound
to the exact SICX root. The list assigns c0, c1, … in numeric lexicographic
word order, comparing the first Smith coordinate first. The decoder checks
the binding and certifies an anchor for every ray before it builds RAYS and
BALL. For standalone finalization, use a ray list from a trusted producer
run. The exact-root and Smith-basis checks bind the list to the SICX; they
cannot establish that the caller supplied every ray. Pass `--rays-sha256 HEX`
to authenticate the sidecar bytes against a trusted digest from that run.
The closed form in dimension 2 has one ray, c0 at word 0 in Smith basis [2].
The decoder scans the raw coordinates in index order, keeping the first
certified nonzero coordinate until another coordinate's lower bound on
squared modulus exceeds the incumbent's upper bound. The bounds are rounded
outward at the normalization precision. The decoder refuses if no raw
coordinate is certified nonzero. See the [phase convention](sicx.md#conventions-4-sic-condition-normalization-and-phase).

For a dimension `d = 2^S`, set `S` to the exponent, `ORDER` to the construction order, and `T` and
`M` as described below. Use fresh output paths.

<!-- shell:template -->
```sh
sic-construct "$S" --out exact.sicx --rays-out rays.json --order "$ORDER" \
  --threads "$T" --memory-mib "$M"
sic-decode exact.sicx finalized --finalize --ray-list rays.json --threads "$T" --memory-mib "$M" --check-budget
sic-verify exact.sicx --threads "$T"
```

## Settings and resource choices

The default worker ceiling follows the analytic grid size (1 worker through d4, then d/4 through
d128, 32 at d256, and at most 64 thereafter), bounded by the process CPU allowance. An explicit
`--threads` uses the requested limit, subject to memory admission. Class concurrency adapts to
remaining classes, free worker shares and fully charged owner forecasts; each completed owner
releases its admission before queued work can reuse it.

Run the construction and finalization commands without further options: the built-in generator
selector settings are the ones used for the release, and changing them or the order can change the
exact presentation and its root. The search chooses exact Galois conjugates of eligible quadratic
generators of the class field; cyclotomic, odd-degree and top-character levels keep their base
candidates. The choices are evaluated and recognized again at each precision, including retries.
The selector score is a heuristic accelerator, not a bound on conditioning. `--help` describes
every option.

Set `T` to the number of worker threads and `M` to the memory cap in MiB before running the
commands. For a small dimension, for example:

<!-- shell:template -->
```sh
T=2
M=4096
```

Large dimensions need correspondingly more memory and disk ([measured resources](#measured-resources)).
The planner inspects the memory, job and cgroup limits, the working precision and the actual tower;
it keeps, spills or recomputes caches and reduces the number of workers under memory or I/O pressure.
Its forecasts are engineering allowances, not allocator bounds; use an operating-system memory
ceiling separately when needed. Storage, cache, batch and worker controls preserve the exact bytes
in the tested configurations. Precision retries and quadrature fallbacks are part of the algorithm.

The selector’s complete incremental budget defaults to 16384 MiB; optional
cache ceilings, batch and worker budgets constrain it further. Cache mode is
used only when both the complete incremental and global live sets fit;
`--alignment-cache-mib 0` forces deterministic replay. Search parameters,
base candidates and exact conjugates enter PROV and authenticated resume
journals; changing the semantic plan refuses resume. Resource-only retention
choices do not enter that semantic plan. `--log-admissions` records decisions.
Do not interpret an admission refusal as a mathematical disproof.

## Construction orders

Release 1.0 covers Weyl–Heisenberg construction through dimension 2048. Order enumeration and
construction reject dimension 4096 before allocating the Group; the diagnostic reports the required
point count and the signed 32-bit index capacity.

For any supported exponent S ≥ 2, run `sic-construct S --orders` to get the
construction orders and their conductors, sorted by increasing conductor.
The standard order has discriminant (d+1)(d−3). The maximal order removes
its square factor. When the discriminant is squarefree the two coincide,
and the command lists a single standard-order row. Give each distinct order
to `sic-construct S --order ORDER`. The default order is standard in every dimension.
Number the rows m1, m2, and so on. The order, dimension and resource flags are
all the producer needs to construct the exact part.

With `S`, `T` and `M` set as above, use a fresh output folder:

<!-- shell:template -->
```sh
sic-construct "$S" --orders > orders.txt
N=0
while read -r ORDER CONDUCTOR; do
  N=$((N + 1))
  mkdir "m$N"
  sic-construct "$S" --order "$ORDER" --out "m$N/multiplet.sicx" \
    --rays-out "m$N/rays.json" --threads "$T" --memory-mib "$M"
  sic-decode "m$N/multiplet.sicx" "m$N/decoded" --finalize \
    --ray-list "m$N/rays.json" --threads "$T" --memory-mib "$M"
  sic-verify "m$N/multiplet.sicx" --threads "$T"
done < orders.txt
```

## Checks shared by the recipes

Each per-multiplet block constructs the exact part and then finalizes it in place from the decoder's
own certified normalized enclosures. A finalized file is never made by attaching external balls.
Use a new output path and folder for each attempt. The construction tool refuses an existing final
path and publishes from a private quarantine only after its certified overlap bound passes.
Resumption requires the matching spool, journal, semantic plan and component bindings; partial and
final files are not interchangeable.

After each block, run this sequence with `FILE`, `MULTIPLET`, `D` and `N` set to that block's file,
multiplet label, dimension and number of fiducial vectors:

<!-- shell:template id=shared-checks -->
```sh
sic-verify "$FILE" --threads "$T" --rescaling
sic-verify-independent "$FILE" --release --decode "${READER_MODE:-full}" --max-steps 1048576
sic-decode "$FILE" "$OUT/decoded" --rays all --threads "$T" --memory-mib "$M" \
  --target-bits 128 --guard-bits 64 --initial-bits 2048 --maximum-precision 65536
sic-inspect "$FILE" --json-rays "$OUT/rays" --multiplet "$MULTIPLET"
count=0
for balls in "$OUT"/decoded/c*.normalized.balls; do
  sic-check-balls "$balls" --bits 100 --working-bits 256
  count=$((count + 1))
done
test "$count" -eq "$N"
count=0
for ray in "$OUT"/rays/c*.json; do
  label=$(basename "$ray" .json)
  sic-data write "$ray" "$OUT/numeric/$label" --formats npy,mat,csv
  sic-data check "$ray" "$OUT/numeric/$label/$label.npy" \
    "$OUT/numeric/$label/$label.mat" "$OUT/numeric/$label/$label.csv"
  { echo "$D"; awk -F, 'NR>1 {print $2, $3}' "$OUT/numeric/$label/$label.csv"; } | sic-check - 1e-10
  count=$((count + 1))
done
test "$count" -eq "$N"
```

Run every command with failure checking enabled (for example `set -e` in a script). Compare all
decoded normalized balls with the stored balls for overlap and every certified binary64 word for
equality, for every coordinate of every fiducial vector. The count checks detect a missing fiducial
vector but do not authenticate the selected ones: compare the complete association of labels, words
and roots with `data/catalogue.json`. Container integrity, consistency of overlap and rounding,
containment by the finalizer, numerical overlap bounds and exact certificates are separate results.

The second line depends on the dimension through `READER_MODE`. For `d ≤ 256`, leave it unset (the
all-coordinate mode `full`). For `d512-m1`, `d512-m2` and `d1024-m1`, set `READER_MODE=ratio`, the
spot check used for the release. For `d1024-m2` and `d2048-m1`, the independent reader refuses at its
fixed resource limit of `2^24` rescaling weight keys: run that line on its own with
`READER_MODE=ratio`, outside the `set -e` sequence, expect exit status 3 and the line
`RESULT: REFUSED: rescaling weight-table resource limit`, and run the remaining lines of the block
without it. A refusal is not a pass, and a ratio or partial check does not replace an
all-coordinate check.

`sic-check-balls` fails unless its certified bound is below `2^−100` and prints the bound as
`max_deviation_upper_log2`. Applied to the released SICX files (the files whose exact roots are in
`data/catalogue.json`), with the decoding settings of the block, every printed value is below
−129.5. The `--bits` and `--working-bits` options keep their accuracy and precision meanings;
`sic-check` is only a floating-point diagnostic.

For `4 ≤ d ≤ 256`, run the exact certificate on the file and compare its printed exact root with
the catalogue:

<!-- shell:template -->
```sh
sic-certify-crt "$FILE" --threads "$T"
```

Require the final `[PROVED]` verdict for every Galois word. A bound-only result, a prime-count
override, an estimate, an incomplete result or a resource refusal is not a certificate. The norm
method is a second route where its domain and resources permit (in practice `d ≤ 16`):

<!-- shell:template -->
```sh
sic-certify-norm "$FILE" --threads "$T"
```

It requires finalized rays and balls, a real quadratic base field and supported quadratic tower
levels, with `d` and the field degree at most 4096. The Chinese remainder theorem certificate does
not apply in dimension 2; the closed form there is proved by its exact radical identities
([dimension-two checks](#dimension-two-checks)).

## Per-multiplet commands

Each block constructs and finalizes one multiplet of the release with the built-in selector
settings. Set `T` and `M` first and use a fresh `OUT` for the shared checks. `sic-verify` prints the
exact root of the file; `sic-verify "$FILE" --expect-exact-root ROOT` checks that the construction
reproduced the released presentation.

### d2/m1: standard order

Fiducial vectors: 1. Exact root of the release:
`74091708eaba84f3922cffb86dba33bc1aa6d6033362e8706ecad763ead5e24d`.

<!-- shell:template -->
```sh
FILE=d2-m1.sicx
MULTIPLET=m1
D=2
N=1
OUT=d2-m1-checks
sic-construct 1 --out "$FILE" --rays-out "$FILE.rays.json" --order standard --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize \
  --threads "$T" --memory-mib "$M" --ray-list "$FILE.rays.json"
```

Run the shared checks above.

### d4/m1: standard order

Fiducial vectors: 1. Exact root of the release:
`616f89361d9a0646ddcd169e7a59a63cdcc7adead9990ca8882ae424f5b846a1`.

<!-- shell:template id=d4-recipe -->
```sh
FILE=d4-m1.sicx
MULTIPLET=m1
D=4
N=1
OUT=d4-m1-checks
sic-construct 2 --out "$FILE" --rays-out "$FILE.rays.json" --order standard --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize \
  --threads "$T" --memory-mib "$M" --ray-list "$FILE.rays.json"
```

Run the shared checks above.

### d8/m1: maximal order

Fiducial vectors: 1. Exact root of the release:
`24a5215393dc338c412c2ce9645e93d3985fea868b624e85e63c1a4db7f9e463`.

<!-- shell:template id=d8-recipe -->
```sh
FILE=d8-m1.sicx
MULTIPLET=m1
D=8
N=1
OUT=d8-m1-checks
sic-construct 3 --out "$FILE" --rays-out "$FILE.rays.json" --order maximal --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize \
  --threads "$T" --memory-mib "$M" --ray-list "$FILE.rays.json"
```

Run the shared checks above.

### d8/m2: standard order

Fiducial vectors: 1. Exact root of the release:
`71e665d53173ce0a692dd13d5dc274a39741c7360895cd23dd28aad8f848674a`.

<!-- shell:template -->
```sh
FILE=d8-m2.sicx
MULTIPLET=m2
D=8
N=1
OUT=d8-m2-checks
sic-construct 3 --out "$FILE" --rays-out "$FILE.rays.json" --order standard --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize \
  --threads "$T" --memory-mib "$M" --ray-list "$FILE.rays.json"
```

Run the shared checks above.

### d16/m1: standard order

Fiducial vectors: 2. Exact root of the release:
`4512ae607c93f442b785ff1aa90c557f2661ad67c68a720a88bf65a393271359`.

<!-- shell:template -->
```sh
FILE=d16-m1.sicx
MULTIPLET=m1
D=16
N=2
OUT=d16-m1-checks
sic-construct 4 --out "$FILE" --rays-out "$FILE.rays.json" --order standard --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize \
  --threads "$T" --memory-mib "$M" --ray-list "$FILE.rays.json"
```

Run the shared checks above.

### d32/m1: standard order

Fiducial vectors: 2. Exact root of the release:
`9592d2782badfaad160450456868322bf0454c8d7523d21208f0600c33e5d053`.

<!-- shell:template -->
```sh
FILE=d32-m1.sicx
MULTIPLET=m1
D=32
N=2
OUT=d32-m1-checks
sic-construct 5 --out "$FILE" --rays-out "$FILE.rays.json" --order standard --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize \
  --threads "$T" --memory-mib "$M" --ray-list "$FILE.rays.json"
```

Run the shared checks above.

### d64/m1: standard order

Fiducial vectors: 4. Exact root of the release:
`4dc3c9a2ffd642ca3403817151b33758c4926f2b12212daea02a537dcb52c162`.

<!-- shell:template -->
```sh
FILE=d64-m1.sicx
MULTIPLET=m1
D=64
N=4
OUT=d64-m1-checks
sic-construct 6 --out "$FILE" --rays-out "$FILE.rays.json" --order standard --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize \
  --threads "$T" --memory-mib "$M" --ray-list "$FILE.rays.json"
```

Run the shared checks above.

### d128/m1: maximal order

Fiducial vectors: 2. Exact root of the release:
`39d53a036a3715097a19b5a753a5a7267934a27578075130e4f5441489471cda`.

<!-- shell:template -->
```sh
FILE=d128-m1.sicx
MULTIPLET=m1
D=128
N=2
OUT=d128-m1-checks
sic-construct 7 --out "$FILE" --rays-out "$FILE.rays.json" --order maximal --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize \
  --threads "$T" --memory-mib "$M" --ray-list "$FILE.rays.json"
```

Run the shared checks above.

### d128/m2: standard order

Fiducial vectors: 10. Exact root of the release:
`5d2d6ace4b175889306f6e9d2930c3dde35a95e2490ef87a6b22aefbea1972a9`.

<!-- shell:template -->
```sh
FILE=d128-m2.sicx
MULTIPLET=m2
D=128
N=10
OUT=d128-m2-checks
sic-construct 7 --out "$FILE" --rays-out "$FILE.rays.json" --order standard --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize \
  --threads "$T" --memory-mib "$M" --ray-list "$FILE.rays.json"
```

Run the shared checks above.

### d256/m1: standard order

Fiducial vectors: 12. Exact root of the release:
`bb20c2bcaaffc0818709be72be713c6371be9a3f8f28ecbc61099c9348846bdd`.

<!-- shell:template -->
```sh
FILE=d256-m1.sicx
MULTIPLET=m1
D=256
N=12
OUT=d256-m1-checks
sic-construct 8 --out "$FILE" --rays-out "$FILE.rays.json" --order standard --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize \
  --threads "$T" --memory-mib "$M" --ray-list "$FILE.rays.json"
```

Run the shared checks above.

### d512/m1: maximal order

Fiducial vectors: 6. Exact root of the release:
`80f2119d3c4a909af888b04ac247216b6ed92ec860a890d47913a19603c17a0c`.

<!-- shell:template -->
```sh
FILE=d512-m1.sicx
MULTIPLET=m1
D=512
N=6
OUT=d512-m1-checks
sic-construct 9 --out "$FILE" --rays-out "$FILE.rays.json" --order maximal --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize \
  --threads "$T" --memory-mib "$M" --ray-list "$FILE.rays.json"
```

Run the shared checks above.

### d512/m2: standard order

Fiducial vectors: 18. Exact root of the release:
`31f93c6d39a3d90e109285c57df3049f5be6f144245b212c35b254a3b4d14099`.

<!-- shell:template -->
```sh
FILE=d512-m2.sicx
MULTIPLET=m2
D=512
N=18
OUT=d512-m2-checks
sic-construct 9 --out "$FILE" --rays-out "$FILE.rays.json" --order standard --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize \
  --threads "$T" --memory-mib "$M" --ray-list "$FILE.rays.json"
```

Run the shared checks above.

### d1024/m1: maximal order

Fiducial vectors: 10. Exact root of the release:
`be175720247a71f2e7182ddf86aed9e9ad4e27d1974b36be83beba89c230a4d8`.

<!-- shell:template -->
```sh
FILE=d1024-m1.sicx
MULTIPLET=m1
D=1024
N=10
OUT=d1024-m1-checks
sic-construct 10 --out "$FILE" --rays-out "$FILE.rays.json" --order maximal --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize \
  --threads "$T" --memory-mib "$M" --ray-list "$FILE.rays.json"
```

Run the shared checks above.

### d1024/m2: standard order

Fiducial vectors: 40. Exact root of the release:
`71861719651e9e3ec57a7469baf9ec12e71a55aa82d321ff000d0c784dda9466`.

<!-- shell:template -->
```sh
FILE=d1024-m2.sicx
MULTIPLET=m2
D=1024
N=40
OUT=d1024-m2-checks
sic-construct 10 --out "$FILE" --rays-out "$FILE.rays.json" --order standard --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize --ray-list "$FILE.rays.json" \
  --threads "$T" --memory-mib "$M"
```

Run the shared checks above.

### d2048/m1: standard order

Fiducial vectors: 64. Exact root of the release:
`13d1ebab88fbe53f70cfddf1750cd095d2029bcc0d5c5ab1a55d76ed4bdf146b`.

<!-- shell:template -->
```sh
FILE=d2048-m1.sicx
MULTIPLET=m1
D=2048
N=64
OUT=d2048-m1-checks
sic-construct 11 --out "$FILE" --rays-out "$FILE.rays.json" --order standard --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize --ray-list "$FILE.rays.json" \
  --threads "$T" --memory-mib "$M"
```

Run the shared checks above.

## Dimension-two checks

The default S=1 source is the embedded pinned radical record. To use the
explicit identical input, add `--radicals data/d2/d2.sicrad`.
Its parsed coefficients must satisfy positivity, A+2B=1, 4AB=1/3 and
(A−2B)^2=1/3; these are exact identities in Q(√3), not residual tests.
An independent exact stabilizer check reads the radical input directly:

<!-- shell:template -->
```sh
sic-stabilizer --dim 2 --catalogue data/catalogue.json --out d2_stabilizers.txt
sic-certify-stabilizer d2_stabilizers.txt --catalogue data/catalogue.json \
  --d2-exact data/d2/d2.sicrad
sic-inspect --exact-json d2-m1.sicx d2-exact.json --multiplet m1
SIC_EXACT_JSON=d2-exact.json gp -q -s 2G cpp/checkers/gp/sicx_exact_verify.gp < /dev/null
```

The stabilizer check concerns the listed symmetry elements. The PARI/GP script checks the
polynomial SIC-POVM identities exactly but uses an uncertified floating-point embedding for
conjugation and positivity, so its PASS alone is not an unconditional certificate. The radical
identities supply the exact proof in dimension 2.

## Rebuilding the numerical data

`data/` was written from the finalized SICX files with the commands below; the `h5` files need a
build with HDF5 enabled (`SIC_ENABLE_HDF5`). Start with an empty release folder `RELEASE_TREE` and
copy `data/d2/d2.sicrad` and `data/d8/hoggar/` into it. Write each multiplet with its file `FILE`,
dimension `D`, the conductors `CONDUCTORS` of all orders of the dimension (comma-separated, from
`sic-construct S --orders`) and the conductor `CONDUCTOR` of its own order:

<!-- shell:template -->
```sh
sic-inspect "$FILE" --release-layout "$RELEASE_TREE" \
  --provenance-conductors "$CONDUCTORS" --provenance-conductor "$CONDUCTOR" \
  --release-repo Hayata-Yamasaki-Group/sic-povm --release-tag v1.0.0
for ray in "$RELEASE_TREE/d$D"/m*/c*.json; do
  sic-data write "$ray" "$(dirname "$ray")" --formats npy,mat,csv,h5
done
```

The layout command records the digests of the transport parts in `multiplet.sicx.parts.json`;
`--assets-dir DIR` also writes the parts themselves (the release assets) into `DIR`.

After every multiplet of a dimension is written, bundle its fiducial vectors, build the catalogue,
compute and certify the symmetry table, and rebuild the catalogue so that it includes the table.
In dimension 8 the bundle also contains the Hoggar vector, and in dimension 2 the certificate also
checks the elements exactly:

<!-- shell:template -->
```sh
case "$D" in
  8) sic-data bundle "$RELEASE_TREE/d8/all.npz" 8 "$RELEASE_TREE/d8"/m*/c*.json \
       --exceptional "$RELEASE_TREE/d8/hoggar/c0.json" "3-qubit Pauli group" ;;
  *) sic-data bundle "$RELEASE_TREE/d$D/all.npz" "$D" "$RELEASE_TREE/d$D"/m*/c*.json ;;
esac
sic-inspect --catalogue "$RELEASE_TREE"
sic-stabilizer --dim "$D" --catalogue "$RELEASE_TREE/catalogue.json" --out "$RELEASE_TREE/d$D/stabilizers.txt"
if [ "$D" = 2 ]; then
  sic-certify-stabilizer "$RELEASE_TREE/d2/stabilizers.txt" --catalogue "$RELEASE_TREE/catalogue.json" \
    --dim 2 --d2-exact "$RELEASE_TREE/d2/d2.sicrad"
else
  sic-certify-stabilizer "$RELEASE_TREE/d$D/stabilizers.txt" --catalogue "$RELEASE_TREE/catalogue.json" \
    --dim "$D"
fi
sic-inspect --catalogue "$RELEASE_TREE"
```

After the last dimension, write the checksum list of the whole tree, with sorted `./`-relative paths:

<!-- shell:template -->
```sh
(cd "$RELEASE_TREE" && find . -type f ! -name sha256sums.txt | LC_ALL=C sort |
  xargs sha256sum > sha256sums.txt)
```

When the inputs are the released SICX files, the files written in this way have the bytes of the
released files.

## Exact data from the release

Use the parts manifest in `data/` as the trusted source of the roots; do not take trust from the
server whose data you are checking:

<!-- shell:template -->
```sh
sic-container fetch d256/m1 --data-root data --out downloaded
sic-container repair d256/m1 --data-root data --out downloaded
sic-verify --remote data/d256/m1/multiplet.sicx.parts.json --quick \
  --expect-exact-root bb20c2bcaaffc0818709be72be713c6371be9a3f8f28ecbc61099c9348846bdd \
  --expect-root ab93b7de43486be2aca90e2b639e8d4f99ddc8a111691f2a752cac2bb7506191
```

Fetch and repair verify each complete part and the whole file before atomic publication. The
`--quick` check reads the header, footer, directories and EXMT and matches both roots against the
trusted values from the manifest; this authenticates the root commitments, including the file root
that binds the finalization, without reading the finalization payloads. Omit `--quick` to verify
every payload as well. An exact root alone does not authenticate the finalization. Payload reads through `sic-verify --remote` and `sic-decode --remote`
check every physical chunk and logical leaf they consume, but they do not establish a whole-file or
part digest from a subset. `sic-container read-range` writes raw bytes for diagnostics; the bytes it
returns are not authenticated. A complete fiducial vector needs essentially all exact data;
coordinate and ratio subsets can save transfer, but selected raw coordinates cannot be normalized
without the complete vector. See the [data guide](data.md#remote-decoding) for the cache and HTTP
contracts.

<a id="measured-resources"></a>
## Measured resources

The release was computed on Google Cloud H4D machines with 192 AMD EPYC 9B45 cores (no simultaneous
multithreading) and about 1.4 TiB of memory, running Ubuntu 24.04 with GCC 13.3, CMake 3.28 and
FLINT 3.6.0 (Release build with link-time optimization). Every construction and finalization ran
with `--threads 188` and `--memory-mib 32768` for `d ≤ 256`, `--memory-mib 98304` for `d = 512` and
`d = 1024`, and `--memory-mib 1048576` for `d = 2048`. Wall time and peak resident memory are the
whole-process measurements of GNU `time`; single measurements on cloud machines vary from run to
run.

| multiplet | construction wall time | construction peak memory | finalization wall time | finalization peak memory | exact file bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `d2-m1` | < 0.01 s | 9.2 MiB | 0.03 s | 9.1 MiB | 3,211 |
| `d4-m1` | 0.37 s | 13.7 MiB | 0.04 s | 9.2 MiB | 6,361 |
| `d8-m1` | 0.43 s | 15.2 MiB | 0.04 s | 9.2 MiB | 7,821 |
| `d8-m2` | 0.71 s | 13.8 MiB | 0.04 s | 9.2 MiB | 11,992 |
| `d16-m1` | 1.16 s | 13.7 MiB | 0.05 s | 9.2 MiB | 23,379 |
| `d32-m1` | 1.73 s | 15.4 MiB | 0.06 s | 9.1 MiB | 55,110 |
| `d64-m1` | 3.23 s | 49.6 MiB | 0.18 s | 10.7 MiB | 438,417 |
| `d128-m1` | 3.96 s | 131 MiB | 0.23 s | 10.6 MiB | 676,549 |
| `d128-m2` | 9.63 s | 627 MiB | 1.22 s | 38.9 MiB | 7,173,685 |
| `d256-m1` | 33.9 s | 1.79 GiB | 6.53 s | 193 MiB | 47,745,553 |
| `d512-m1` | 67.4 s | 4.75 GiB | 11.2 s | 290 MiB | 77,752,179 |
| `d512-m2` | 202 s | 7.29 GiB | 59.2 s | 2.06 GiB | 665,733,653 |
| `d1024-m1` | 707 s | 9.52 GiB | 101 s | 3.24 GiB | 748,193,718 |
| `d1024-m2` | 5,549 s (1.54 h) | 49.3 GiB | 960 s | 36.7 GiB | 11,726,032,122 |
| `d2048-m1` | 46,162 s (12.8 h) | 373 GiB | 10,338 s (2.87 h) | 386 GiB | 128,506,739,686 |

The construction of `d2048-m1` used a 1.5 TB data volume. On a second machine of the same type
(the multiplets with `d ≤ 512` were constructed for the release on another machine), five
constructions of each multiplet with `d ≤ 512` other than `d256-m1` produced the exact roots of the
release, and their exact files were byte-identical to one another. The exact root does not depend on
the recorded build identity; the provenance chunk of a rebuilt file records its own source and tool
identity, so a rebuilt file can differ from the released file in its whole-file digest and file
root. The cost of the exact certificates is given in
[verification](verification.md#crt-measured-cost).

## Hoggar lines

The Hoggar lines are separate from the Weyl–Heisenberg SIC-POVMs in dimension 8. Their exact closed
form `(-1+2i,1,1,1,1,1,1,1)/sqrt(12)` and the three-qubit Pauli action are described in the
[data guide](data.md#data-8-the-hoggar-lines-datad8hoggar). Check them with:

<!-- shell:template -->
```sh
gp -q cpp/checkers/gp/hoggar_verify.gp
ctest --test-dir build-exact -R hoggar --output-on-failure
```

The checks establish the exact norm, the 63 overlaps equal to 1/9 and the 2016 pairwise line
relations. The Hoggar lines are not a Weyl–Heisenberg multiplet, have no SICX file and are not
counted among the 174 fiducial vectors.

## Small runnable example

This runnable block constructs the standard-order multiplets in dimensions 4 and 8, finalizes them,
and runs the full verification, the independent reader and the exact certificate. `T` is the thread
count and `M` the memory cap in MiB; the documentation test sets them to 1 and 2048 in an empty
working folder. The other shell blocks on this page are templates that need the settings described
in their surrounding text.

<!-- shell:run id=small-certificates -->
```sh
for S in 2 3; do
  D=$((1 << S))
  mkdir "d$D"
  FILE="$PWD/d$D/fresh.sicx"
  sic-construct "$S" --out "$FILE" --rays-out "$PWD/d$D/ray-list.json" --order standard --threads "$T" --memory-mib "$M"
  sic-decode "$FILE" "$PWD/d$D/finalize" --finalize \
    --threads "$T" --memory-mib "$M" --ray-list "$PWD/d$D/ray-list.json"
  sic-verify "$FILE" --threads "$T" --rescaling
  sic-verify-independent "$FILE" --release --decode full --max-steps 1048576
  sic-certify-crt "$FILE" --threads "$T"
done
```
