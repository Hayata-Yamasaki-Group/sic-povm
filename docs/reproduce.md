# Reproducing the published results

These recipes use installed tools directly. Install the FLINT-enabled configuration
as described in [installation](install.md), and put its bin directory on PATH.
[Verification and claim scope](verification.md) owns the status definitions,
proof requirements, trusted components and limitations. [Data](data.md) owns
file layouts, download trust and loading recipes.

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
The d2 closed form has one ray, c0 at word 0 in Smith basis [2].
The decoder scans the raw coordinates in index order, keeping the first
certified nonzero coordinate until another coordinate’s lower bound on
squared modulus exceeds the incumbent’s upper bound. The bounds are rounded
outward at the normalization precision. The decoder refuses if no raw
coordinate is certified nonzero. See the [gauge convention](sicx.md#conventions-4-sic-condition-normalization-and-phase).

For any supported dyadic construction, set S to the exponent, ORDER to the
construction order and T and M as described below. Use fresh output paths.

<!-- shell:template -->
```sh
sic-construct "$S" --out exact.sicx --rays-out rays.json --order "$ORDER" \
  --threads "$T" --memory-mib "$M"
sic-decode exact.sicx finalized --finalize --ray-list rays.json --threads "$T" --memory-mib "$M" --check-budget
sic-verify exact.sicx --threads "$T"
```

## Canonical settings and resource choices

The default worker ceiling follows the analytic grid size (1 worker through d4, then d/4 through
d128, 32 at d256, and at most 64 thereafter), bounded by the process CPU allowance. An explicit
`--threads` uses the requested limit, subject to memory admission. Class concurrency adapts to
remaining classes, free worker shares and fully charged owner forecasts; each completed owner
releases its admission before queued work can reuse it.

Use the construction and finalization templates without adding optional flags.
See `--help` for flag descriptions. Changing the selector settings or the order
can change the exact presentation and its root. The search chooses
bare exact Galois conjugates of eligible class-field quadratic generators;
cyclotomic, odd-degree and top-character levels retain base candidates. Choices
are evaluated and recognized again at each precision, including retries.
The score is an empirical accelerator, not a uniform conditioning bound.

Set T to an available native worker count and M to an admitted memory cap in
MiB, before running the templates. For a small instance an initial example is:

<!-- shell:template -->
```sh
T=2
M=4096
```

Large cases need separate memory/disk admission; M=4096 is not a large-case
recommendation. The planner inspects RAM, job/cgroup limits, working precision
and the actual tower, keeps/spills/recomputes caches, and reduces workers under
memory or I/O pressure. Forecasts are engineering allowances, not allocator
bounds. Use an operating-system memory ceiling separately when needed.
Storage/cache/batch/worker controls preserve canonical bytes in the tested
configurations; those controls are finite evidence, not a theorem about every
resource setting. Preserve precision retries and quadrature fallback.

The selector’s complete incremental budget defaults to 16384 MiB; optional
cache ceilings, batch and worker budgets constrain it further. Cache mode is
used only when both the complete incremental and global live sets fit;
`--alignment-cache-mib 0` forces deterministic replay. Search parameters,
base candidates and exact conjugates enter PROV and authenticated resume
journals; changing the semantic plan refuses resume. Resource-only retention
choices do not enter that semantic plan. `--log-admissions` records decisions.
Do not interpret an admission refusal as a mathematical disproof.

## Construction orders

Release 1.0 covers WH construction through d = 2048. Order enumeration and
construction reject d = 4096 before allocating the Group. The diagnostic
reports the required point count and the signed 32-bit index capacity.

For any supported exponent S ≥ 2, run `sic-construct S --orders` to get the
construction orders and their conductors, sorted by increasing conductor.
The standard order has discriminant (d+1)(d−3). The maximal order removes
its square factor. When the discriminant is squarefree the two coincide,
and the command lists a single standard-order row. Give each distinct order
to `sic-construct S --order ORDER`. The default order is standard in every dimension.
Number the rows m1, m2, and so on. The order, dimension and resource flags are
all the producer needs to construct the exact part.

With S, T and M set as above, use a fresh output directory:

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

## Build the release records

Start with an empty release directory. For each finalized multiplet from the
construction and decoding steps, pass its SICX file as FILE and its order
number as N. Set D to the dimension and RELEASE_TREE to the output directory.
Before publishing, run the [per-ray checks](verification.md) on the
normalized balls from each finalized SICX file.

<!-- shell:template -->
```sh
sic-inspect "$FILE" --release-layout "$RELEASE_TREE" --multiplet "$N"
for ray in "$RELEASE_TREE/d$D/m$N"/c*.json; do
  sic-data write "$ray" "$RELEASE_TREE/d$D/m$N" --formats npy,mat,csv
done
```

After writing every multiplet in that dimension, use the catalogue to pass
its generated rays to the stabilizer tool. Rebuild the catalogue after adding
the table so its digest is included. A release run also bundles the numeric
files and writes the checksum manifest.

<!-- shell:template -->
```sh
sic-inspect --catalogue "$RELEASE_TREE"
sic-stabilizer --dim "$D" --catalogue "$RELEASE_TREE/catalogue.json" --out "$RELEASE_TREE/d$D/stabilizers.txt"
sic-inspect --catalogue "$RELEASE_TREE"
```

## Multiplet inventory and resource guidance

GB in this table means decimal 10^9 bytes. The small-instance row is a wildcard
planning estimate, not nine independent measurements. The four measured rows
use a different recorded native recipe, nominally 32 threads; compiler and
per-row library details were not recorded in that estimate table. They are
not measurements of the final recipe printed below and are not guaranteed
admissions. Keep them separate from the fully conditioned observations later.

| multiplet | exponent S | order | recorded rays | resource guidance: wall / RAM / disk |
| --- | ---: | --- | ---: | --- |
| d2/m1 | 1 | standard | 1 | 60 s / 1 GB RAM / 0.01 GB disk (estimate) |
| d4/m1 | 2 | standard | 1 | 60 s / 1 GB / 0.01 GB (estimate) |
| d8/m1 | 3 | maximal | 1 | 60 s / 1 GB / 0.01 GB (estimate) |
| d8/m2 | 3 | standard | 1 | 60 s / 1 GB / 0.01 GB (estimate) |
| d16/m1 | 4 | standard | 2 | 60 s / 1 GB / 0.01 GB (estimate) |
| d32/m1 | 5 | standard | 2 | 60 s / 1 GB / 0.01 GB (estimate) |
| d64/m1 | 6 | standard | 4 | 60 s / 1 GB / 0.01 GB (estimate) |
| d128/m1 | 7 | maximal | 2 | 60 s / 1 GB / 0.01 GB (estimate) |
| d128/m2 | 7 | standard | 10 | 60 s / 1 GB / 0.01 GB (estimate) |
| d256/m1 | 8 | standard | 12 | 101.2 s / 1.51 GB / 0.14 GB (different recipe measurement) |
| d512/m1 | 9 | maximal | 6 | 266.9 s / 3.37 GB / 0.21 GB (different recipe measurement) |
| d512/m2 | 9 | standard | 18 | 1257.5 s / 11.35 GB / 1.74 GB (different recipe measurement) |
| d1024/m1 | 10 | maximal | 10 | 2918 s / 11.6 GB / 1.96 GB (different recipe measurement) |
| d1024/m2 | 10 | standard | not published | 13700 s / 55 GB / 4.6 GB (estimate; not published) |
| d2048/m1 | 11 | standard | not published | 138000 s / 1500 GB / 670 GB (estimate; not published) |

The recorded collection totals 70 rays in 13 populated multiplets. The two
unpublished rows have no ray records: 40 d1024-standard and 64 d2048 rays are
classification targets, not completed output. Including those targets gives
174 conjectural rays. The 1500 GB d2048 planning estimate is not an admitted
workload. No new construction, large certificate or performance run is
claimed by these recipes.

## Finalization and checks shared by the recipes

Each per-multiplet block constructs the exact part and then finalizes it in
place from the decoder’s own certified normalized/gauged enclosures. A
finalized file is not made by attaching external publication balls. Use a
new output path and directory for each attempt. The construct tool refuses an
existing final path and publishes from a private quarantine only after its
certified SIC bound passes. Resume requires the matching spool, journal,
semantic plan and component bindings; partial/final files are not interchangeable.

After each populated block, run this sequence with FILE, MULTIPLET, D and N
set to that block’s file, m-label, dimension and expected ray count:

<!-- shell:template id=shared-checks -->
```sh
sic-verify "$FILE" --threads "$T" --rescaling
sic-verify-independent "$FILE" --release --decode full --max-steps 1048576
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

Run every command with failure checking enabled (for example `set -e` in a
script). Compare all decoded normalized balls against the published inputs
for overlap and every certified binary64 word for equality; do not restrict
the comparison to c0 or a sample of coordinates. The count checks detect a
missing ray, but do not authenticate the identity of the selected rays:
compare the complete label/word/root association with the trusted manifest.
Container integrity, overlap/rounding consistency, finalizer containment,
WH numerical bounds and exact SIC certificates are separate gates.

The independent reader may refuse a valid large file under its own branch,
precision or memory policies; a refusal is not a pass. Ratio/partial checks
must be reported with deferred data and cannot replace an all-coordinate
check. The helper `sic-check-balls` bits and working-bits options retain
their accuracy/precision semantics; `sic-check` is only a floating-point
diagnostic. Never replace an exact certificate by a small residual.

For a d≥4 exact-proof claim, run the full independent certificate on that
file and bind its printed exact root to the current manifest:

<!-- shell:template -->
```sh
sic-certify-crt "$FILE" --threads "$T"
```

Require the terminal `[PROVED]` verdict for every Galois word. A bound-only,
prime-count override, estimate, incomplete result or resource refusal is not
a certificate. The norm method is a second route where its domain and
resources permit:

<!-- shell:template -->
```sh
sic-certify-norm "$FILE" --threads "$T"
```

It requires finalized rays/balls, a real quadratic base and supported
quadratic tower stages, with d and field degree at most 4096. Odd-level
estimation is not final certification. It can select a ray by numeric index;
CRT covers every Galois word. Do not run CRT mathematics at d2. The d2 proof
uses its exact positive radical identities; independent radical/stabilizer
and optional exact-JSON checks are described below and in verification.

## Per-multiplet commands

All blocks use the same canonical search settings and the generated ray list.
Set T and M first, and use a fresh OUT for the common checks.

### d2/m1: standard order

Recorded ray count: 1. Current publication requires all 1 checked root bindings.

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

Run the shared full verification, independent decode, all-ray numerical bound,
export/readback and applicable exact-certificate sequence above.

### d4/m1: standard order

Recorded ray count: 1. Current publication requires all 1 checked root bindings.

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

Run the shared full verification, independent decode, all-ray numerical bound,
export/readback and applicable exact-certificate sequence above.

### d8/m1: maximal order

Recorded ray count: 1. Current publication requires all 1 checked root bindings.

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

Run the shared full verification, independent decode, all-ray numerical bound,
export/readback and applicable exact-certificate sequence above.

### d8/m2: standard order

Recorded ray count: 1. Current publication requires all 1 checked root bindings.

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

Run the shared full verification, independent decode, all-ray numerical bound,
export/readback and applicable exact-certificate sequence above.

### d16/m1: standard order

Recorded ray count: 2. Current publication requires all 2 checked root bindings.

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

Run the shared full verification, independent decode, all-ray numerical bound,
export/readback and applicable exact-certificate sequence above.

### d32/m1: standard order

Recorded ray count: 2. Current publication requires all 2 checked root bindings.

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

Run the shared full verification, independent decode, all-ray numerical bound,
export/readback and applicable exact-certificate sequence above.

### d64/m1: standard order

Recorded ray count: 4. Current publication requires all 4 checked root bindings.

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

Run the shared full verification, independent decode, all-ray numerical bound,
export/readback and applicable exact-certificate sequence above.

### d128/m1: maximal order

Recorded ray count: 2. Current publication requires all 2 checked root bindings.

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

Run the shared full verification, independent decode, all-ray numerical bound,
export/readback and applicable exact-certificate sequence above.

### d128/m2: standard order

Recorded ray count: 10. Current publication requires all 10 checked root bindings.

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

Run the shared full verification, independent decode, all-ray numerical bound,
export/readback and applicable exact-certificate sequence above.

### d256/m1: standard order

Recorded ray count: 12. Current publication requires all 12 checked root bindings.

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

Run the shared full verification, independent decode, all-ray numerical bound,
export/readback and applicable exact-certificate sequence above.

### d512/m1: maximal order

Recorded ray count: 6. Current publication requires all 6 checked root bindings.

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

Run the shared full verification, independent decode, all-ray numerical bound,
export/readback and applicable exact-certificate sequence above.

### d512/m2: standard order

Recorded ray count: 18. Current publication requires all 18 checked root bindings.

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

Run the shared full verification, independent decode, all-ray numerical bound,
export/readback and applicable exact-certificate sequence above.

### d1024/m1: maximal order

Recorded ray count: 10. Current publication requires all 10 checked root bindings.

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

Run the shared full verification, independent decode, all-ray numerical bound,
export/readback and applicable exact-certificate sequence above.

### d1024/m2: standard order

**Not published.** This template constructs and finalizes all selected rays.

<!-- shell:template -->
```sh
FILE=d1024-m2.sicx
MULTIPLET=m2
D=1024
OUT=d1024-m2-checks
sic-construct 10 --out "$FILE" --rays-out "$FILE.rays.json" --order standard --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize --ray-list "$FILE.rays.json" \
  --threads "$T" --memory-mib "$M"
```

Do not finalize a sample or substitute an assumed zero word. No expected root,
SHA, completed timing, certified ray count or proof is available for this row.

### d2048/m1: standard order

**Not published.** This template constructs and finalizes all selected rays.

<!-- shell:template -->
```sh
FILE=d2048-m1.sicx
MULTIPLET=m1
D=2048
OUT=d2048-m1-checks
sic-construct 11 --out "$FILE" --rays-out "$FILE.rays.json" --order standard --threads "$T" --memory-mib "$M"
sic-decode "$FILE" "$OUT/finalize" --finalize --ray-list "$FILE.rays.json" \
  --threads "$T" --memory-mib "$M"
```

Do not finalize a sample or substitute an assumed zero word. No expected root,
SHA, completed timing, certified ray count or proof is available for this row.

## Dimension-two checks

The default S=1 source is the embedded pinned radical record. To use the
explicit identical input, add `--radicals data/d2/d2.sicrad`.
Its parsed coefficients must satisfy positivity, A+2B=1, 4AB=1/3 and
(A−2B)^2=1/3; these are exact identities in Q(√3), not residual tests.
An independent exact stabilizer check reads the radical input directly:

<!-- shell:template -->
```sh
sic-stabilizer --dim 2 --catalogue "$RELEASE_TREE/catalogue.json" --out d2_stabilizers.txt
sic-certify-stabilizer d2_stabilizers.txt --catalogue "$RELEASE_TREE/catalogue.json" \
  --d2-exact data/d2/d2.sicrad
sic-inspect --exact-json d2-m1.sicx d2-exact.json --multiplet m1
SIC_EXACT_JSON=d2-exact.json gp -q -s 2G cpp/checkers/gp/sicx_exact_verify.gp < /dev/null
```

The stabilizer proof concerns the listed symmetry elements. The GP script
checks polynomial SIC identities exactly but uses an uncertified floating
embedding for conjugation/positivity; its PASS alone is not an unconditional
SIC certificate. The radical identities supply the exact d2 SIC proof.

## Remote data and integrity replay

Use the current trusted parts manifest and roots; do not extract trust from
the same untrusted server whose data you are checking:

<!-- shell:template -->
```sh
sic-container fetch d8/m1 --data-root "$RELEASE_TREE" --out downloaded
sic-container repair d8/m1 --data-root "$RELEASE_TREE" --out downloaded
sic-container read-range --remote "$RELEASE_TREE/d8/m1/multiplet.sicx.parts.json" 0 16 > header.bin
sic-verify --remote "$RELEASE_TREE/d8/m1/multiplet.sicx.parts.json" --quick \
  --expect-exact-root CURRENT_EXACT_ROOT --expect-root CURRENT_FILE_ROOT
```

Fetch/repair verify each complete part and the whole file before atomic
publication. An authenticated range checks requested physical/logical units;
it does not establish a whole-file SHA or a part hash from a subset. A file
root authenticates finalization; an exact root alone does not. A cold complete
ray currently requires essentially all exact data. Coordinate/ratio subsets
can save transfer, but raw selected coordinates cannot be normalized without
the complete ray. See [data](data.md#remote-decoding) for cache/HTTP contracts.

## Recorded measurements with their conditions

These observations used a shared Linux host: two AMD EPYC 7763 processors,
128 physical cores/256 hardware threads, 503.7 GiB RAM, Ubuntu 22.04,
Linux 6.5.0-15, GCC 11.4 Release/LTO without fast-math, FLINT 3.6.0 with
Arb/GMP/MPFR and ZFS (ARC capped at 60 GiB). Other jobs were active. These
are single-run observations on different input presentations, not benchmarks
or promises for the current canonical recipe. Stored numerical data and
certificate status keep their separate meaning.

| construction | working bits | observed wall | peak RSS | workers | exact output bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| d8 standard | 128 | 0.17 s | 8–20 MiB (small-run range) | 2 in later rerun | not recorded here |
| d16 | 142 | 0.31 s | 8–20 MiB (range) | 2 in later rerun | not recorded here |
| d32 | 233 | 0.67 s | 8–20 MiB (range) | 4 in later rerun | not recorded here |
| d64 | 448 | 3.9 s | 8–20 MiB (range) | 4 in later rerun | not recorded here |
| d128 standard | 364 | 7.3 s | 83 MiB | 4 in later rerun | not recorded here |
| d256 standard | 1757 | 292.8 s | 4.91 GiB | not retained in summary | not recorded here |
| d512 maximal | 1356 | 684.8 s | 8.20 GiB | 4 | 998,517,452 |
| d512 standard | 3651 | 2531.9 s (producer 2530.2) | 19.85 GiB | 32 | 8,626,771,396 |
| d1024 maximal | 1914 | 3541.0 s (producer 3539.1) | 22.36 GiB | 32 | 9,999,667,499 |

RSS is GNU time’s KiB converted with 1 GiB=2^30 B; it is distinct from a
job/cgroup peak and from a requested budget. Recorded d512-standard used a
96 GiB job cap and 76000 MiB producer cap, after a 64 GiB admission refusal.
d1024-maximal used 120 GiB/92000 MiB after a 64 GiB refusal; its cgroup peak
was 24.1 decimal GB. Its producer WH bound was 1.26e-154. The current output
may differ in size, precision, root and time from these presentations.

A separate Apple arm64 observation (18 logical CPUs, 128 GiB RAM, macOS
26.5.2, AppleClang 21, FLINT 3.6.0) used nine workers: 1924.4 s, 59,819.6 MiB
RSS and about 24.2 GB exact output. It reproduced that recorded presentation’s
bytes. It was not rerun for the current recipe.

Recorded small replays reproduced d8–d128 inputs byte for byte
with certified Arb entries, 4096 MiB budget, two workers at d8/d16 and four at
d32–d128: d8/d16 below one second, d32 0.5 s/10 MB, d64 2.9 s/35 MB,
d128 5.9 s/101 MB. These timings describe the specified numerical computation; current SICX
identities and hashes follow the normative specification. These replays do not
establish results for larger dimensions, all-ray decode, PEC comparison or
numerical export.

## Collection and checker costs

Recorded d512-maximal decoding: six rays, 221.7 s decode, 286.5 s total,
about 0.23 GB peak; its independent partitioned identity-ray reference took
57 minutes at 34 MB peak, eight partitions. Conversion took about 66 s per
ray. d512-standard used batches of eight, a 10% owner forecast, 32 reference
partitions and a 31 min 31 s collection at about 1.35 GB. d1024-maximal used
batches of four, 10% owner forecast, 24 reference partitions and a 40000 MiB
cap: 1 h 18 min 45 s, about 4.9 GB RSS; numerical conversion was about
5.6 minutes for each of ten rays. A reference merge’s one-million-root-call
limit refused; a later limit of 100 million admitted it. These are operation
counts/resources of those separate inputs, not parameters of the installed
native decoder or a current admission guarantee.

The d256 twelve-ray collection took about 369 s total at about 140 MB cgroup
peak on one core; its 3072 coordinates and 786420 numerical bounds are
recorded in verification. d512 PEC has 15 maximal, 153 standard and 108
cross-order pairs; d1024 maximal has 45. Pending target sets would require
780 pairs at d1024 standard and 2016 at d2048, but those results do not exist.

The d2048 analytic table alone completed at 17300 bits with 64 classes,
6 h 25 min and about 138 GB peak. A table is not a completed exact export,
a decoded SIC or 64 published rays. Full current production and bindings
remain prerequisites. Component-kernel speed observations do not establish
an end-to-end construction speedup and are omitted from resource guidance.

CRT’s per-presentation prime counts, elapsed times, 120-worker configuration
and RSS through d256 are in [verification](verification.md#crt-measured-cost).
Those measurements are root-specific. Larger exact-certificate estimates
are about 4000 CPU-hours at d512 and 200000 CPU-hours at d1024; they have
not been run. Norm proof precision grows with field degree times height,
with quadratic power-basis preprocessing, so practical examples are small.
Do not report an estimate as a successful proof or a current release timing.

## Required publication evidence

Record tool version/source identities, dependency/compiler versions, complete
canonical flags and inputs; current ray word/anchor associations; exact/file
roots and whole/part hashes; every decoded coordinate and its rounding and
containment gates; numerical WH bounds for every nonidentity label; the full
independent-reader scope; and any required root-bound exact certificate.
Keep PEC separation, numerical consistency, construction premises and exact
proofs as separate statements. A changed generator presentation requires a
fresh certificate, not substitution of an old root.

The entry rule has been compared with all packed power-of-two entry lines
through d1024, including four nonidentity pure-Galois matches and a failing
wrong-word control. Generator rescaling and exact-JSON roundtrips preserved
decoded values/bytes in the exercised fixtures. These finite controls do not
prove general-d production, the field/group/kernel premises, uniform Newton
nonsingularity or a sharp transported-decoding precision bound. Reference
BALL overlap and binary64 agreement are weaker than exact-point containment;
the finalizer must construct its own outward BALLs. Independent large-input
ratio checks are sampled consistency results, not full exact certificates.

An accepted Newton inclusion certifies its own box/subsystem. Uniform full
Jacobian rank, conditioning, seed precision and efficient general-dimension
output remain open. PROV is informative source association; it is not an
exact SIC proof and it does not override EXMT. Real hosted release downloads,
new-root production certificates and the two unpublished large target sets
must be completed before making those corresponding release claims.

## Exceptional Hoggar data

Hoggar is separate from cyclic WH d8. Reproduce its exact closed form
`(-1+2i,1,1,1,1,1,1,1)/sqrt(12)` with the three-qubit Pauli action in
[data](data.md#data-8-the-hoggar-lines-datad8hoggar). Check with:

<!-- shell:template -->
```sh
gp -q cpp/checkers/gp/hoggar_verify.gp
ctest --test-dir build-exact -R hoggar --output-on-failure
```

The checks establish exact norm, 63 overlaps equal to 1/9 and 2016 pairwise
line relations. Preserve the published NPY/MAT/HDF5/CSV coordinate bytes and
explicitly update authenticated metadata checksums when metadata changes.
It is not a WH multiplet, has no SICX or native ray word, and is not one of
the 70 WH rays or the 174 classification targets.

## Bounded offline smoke recipe

This runnable block uses fresh d4 and d8 standard-order constructions. The
producer supplies the ray list for each presentation. The decoder finalizes the exact data and checks the full independent reader and
CRT certificate. `T` is the explicit thread count and `M` the memory budget in
MiB; the policy runner sets them to 1 and 2048 in an empty working directory.
Other shell blocks on this page are templates requiring the resource settings described
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

The following concrete example paths are produced by the export or download
recipes on this page; they need not be present in a source checkout.
