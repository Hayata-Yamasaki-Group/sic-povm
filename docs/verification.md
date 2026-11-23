<a id="verification-verification-and-status-of-the-claims"></a>
# Verification and claim scope

This page states, for every claim the project makes about its data, whether it is proved, certified,
conditional, empirical or conjectural, and how you can check it yourself. Interpret the numerical
files using the normative [mathematical conventions](sicx.md#mathematical-conventions). Status
attaches to a statement and identified input, not just a dimension or filename.

**Audience:** everyone; researchers who want to know exactly what is claimed.

<a id="verification-status-words"></a>
## Status words

| word | meaning here |
| --- | --- |
| **proved** | follows from a mathematical proof, or from a certificate whose success implies the statement: exact arithmetic, possibly with certified (outward-rounded) ball arithmetic, but no unverified floating point and no imported premise beyond those named |
| **certified** | established by rigorous ball (interval) arithmetic with outward rounding in the recorded run; a certified *numerical bound* bounds a quantity, it does not show that the quantity is exactly zero |
| **conditional** | holds relative to stated imported results and premises that this project has not independently proved |
| **empirical** | observed in every case checked, used as a rule and checked again on each file, but not proved |
| **conjectural** | rests on a published conjecture |

Byte-level checks (SHA-256, Merkle roots, canonical encoding) establish which data were saved and
that copies agree. They say nothing about the SIC property.

[SICX 1.0](sicx.md#spec-sicx-10-final-exact-weylheisenberg-sic-fiducial-format) accepts every
integer `2 ≤ d ≤ 2^20`; this is a format scope, while the current producer and collected data remain
restricted to powers of two. General-d support and synthetic reader conformance tests are
implementation work, not evidence of general-dimension SIC production.

The final [SICX 1.0
specification](sicx.md#spec-sicx-10-final-exact-weylheisenberg-sic-fiducial-format) separates two
identities. The **exact root** commits EXMT and canonical logical coordinate leaves: the embedded
tower, 2-adic shifts, Galois action, entry rule and representatives. It identifies an exact
presentation, not an isomorphism class or a list of published rays. Rechunking, codecs, PROV,
transport and finalization preserve it. RAYS selects words from that presentation and is bound by
the **file root**, together with BALL/PREC, provenance and the physical directory. The trusted
catalogue binds ray labels → words → exact root and the relevant file root. A certificate covering
all Galois words can therefore cover every catalogue selection with that exact root; the root itself
is no SIC proof. SICX 1.0 readers reject every undefined chunk type regardless of flags; extensions
require `required_features` bits in a later version.

Full **container** verification checks the chosen-base simple nonzero roots
and canonical branch rectangles, including exact singleton components. It
does not certify irreducibility, extension degrees, the declared Galois group
or action identities, or the representative kernel-fixity premise needed for
lift independence. Container checks derive the canonical unit generators from
`d`, validate divisor positions and word count/rank/residues, and check each
generator-order congruence. A separate algebraic verifier must certify the
field/group/actions and exact fixity under every reduction kernel (a kernel
generating set suffices; [spec §3.5](sicx.md#spec-35-entries-and-rays)). Without
those checks, decoding remains conditional on those explicit premises.
The CRT proof scope is recorded in §2.8; no general-d extension is claimed.
Nor does container success alone establish BALL containment of the exact
decoded coordinates or equality of their binary64 roundings. Certified
finalization and independent decode checks are separate results.

The rescaling undo/reapply check relates the stored data to the presentation
reconstructed from the recorded shifts. It does not authenticate recorded
producer inputs; that association needs independently authenticated provenance.

**Root binding.** Recorded certificates cover nine presentations through d256 (35 rays), and the d2
radical certificate gives 36 proved rays. The norm method also covered d4, both d8 orders and d16
(five rays). These computations do not certify a newly generated release file unless its exact root
and ray selection match the recorded proof. Current release-production bindings will be generated
into `catalogue.json` with the per-ray records. No current binding is supplied by this manual,
and no certificate at d512 or above is claimed.

<a id="verification-1-claims-and-their-status"></a>
## 1. Claims and their status

| claim | status | scope and qualification |
| --- | --- | --- |
| Existence of WH SICs in every `d ≥ 4` (existence part of Zauner's conjecture) | **conditional** (on RW26 Thm 3 at real τ only; not a project result) | Proof: "Stark units for real quadratic fields and reciprocity laws" (4 Oct 2026) Prop. 7 + RW26 Thm 7 + the argument of AFK25 Thm 1.46. This project checked every algebraic, arithmetic and analytic step (review scope: the explicit unresolved identities in this row) except the finite Fourier/five-term identities RW26 (6)–(7) for c ≥ 2: their written proof is incomplete, and a repaired residue argument needs the contour closure and real-τ continuation of RW26 Thm 3. Does not prove the count 174, exhaustiveness, Zauner symmetry, or that our decoded vectors are those SICs. |
| Exact SIC property of the recorded rays with `4 ≤ d ≤ 256`: the 9 multiplets d4, d8 maximal, d8 standard, d16, d32, d64, d128 maximal, d128 standard, d256 standard (35 rays; in fact every Galois word of each multiplet) | **proved** | By the CRT exactness certificate `sic-certify-crt` (§2.8; [checker description](verification.md#crt-what-a-proved-run-establishes)), an independent tool with its own SICX reader. No analytic input (no special values, RW26, AFK25, Artin) and no SICX premise; no integrality hypothesis. The statement is about the vectors defined by each multiplet's exact SICX data (identified by the exact root printed by the successful certificate) in the embedding fixed by the branch rectangles. The certificate does not establish BALL containment. Final 1.0 finalization must construct BALL outward from its own certified decode; recorded overlap checks alone do not establish containment (rows below). |
| The construction yields exact WH SIC fiducials for the 34 collected rays with `d ≥ 512` | **conditional** | Relative to the imported mathematical inputs (finite quantum dilogarithm identities; algebraicity, reciprocity and unitary-conjugate results of the Stark-reciprocity preprint; order class field theory) and the premises recorded with the run data (Artin, exact-recognition, field/trace-rank, action/branch and commuting-K premises). Their full proofs have not all been independently reviewed in this project. **A uniform Newton nonsingularity theorem and transported-decoding precision bounds remain open** (each accepted Newton inclusion certifies nonsingularity of its selected Jacobian on its own box). For `d ≥ 512` the CRT certificate is projected (estimate) at ≈ 4·10^3 CPU-h (d512) and ≈ 2·10^5 CPU-h (d1024); not run. |
| Exact SIC property of the 6 rays at `d = 2, 4, 8, 16` ("independently exact") | **proved** | By exact-arithmetic certificates that do not use the imported analytic SIC theorems (`d = 16` through certificate transport). Executed only on these small instances. Re-proved for the five `d ≥ 4` rays by two further independent methods: the high-precision norm-argument certificate `sic-certify-norm` ([checker description](verification.md#norm-independent-sicx-input-and-exact-certificate)) and the CRT certificate (row above). |
| Per-run identification of the exact data (quadrature enclosures, Newton inclusion tests, recognition, root isolation and branch choices) | **certified** | The current producer always uses certified Arb entries and enforces these acceptance conditions. A successful run identifies its output relative to the imported theorems. Recorded runs must be assessed from their recorded entry modes and evidence; no all-70 recorded entry-mode audit is claimed here. |
| WH overlap bound: `| |⟨ψ|D_p ψ⟩|² − 1/(d+1) | < 2^−100` for all `d² − 1` labels `p ≠ 0` | **certified** (numerical bound) | For every collected ray with `d ≥ 16` (d = 4 and 8: recorded constructor checks; d = 2 is exact, all overlaps equal 1/3, with no ball bound needed; see [reproducibility.md](reproduce.md#reproducing-the-published-results)), for every vector in its certified enclosure. An interval containing zero still permits a small nonzero value: this is not an exact-zero certificate and not the exactness proof. |
| Pairwise inequivalence of the decoded vectors under the extended Clifford group, within each dimension | **certified** | Disjoint certified enclosures of a moment invariant under nonzero scalars and the projective extended Clifford group, unitary and antiunitary elements included (PEC separation). Any two vectors inside the compared certified enclosures lie in distinct extended-Clifford orbits. This uses no SIC premise; that the enclosures contain SIC fiducials is subject to the applicable SIC status above. Compared: every same-dimension pair of collected rays (`d = 8, 16, 32`: 1 pair each; `d = 64`: 6; `d = 128, 256`: 66 each; `d = 1024` maximal: 45) including at `d = 512` the within-order pairs (maximal 15, standard 153) and the 6 × 18 = 108 maximal-against-standard pairs (all separated at moment 1; [reproducibility.md](reproduce.md#reproducing-the-published-results)). An undetermined (overlapping) result proves nothing. Separation does not prove novelty relative to published catalogues, exhaustion of the classification, or a class label. |
| The classification count: 174 rays for `d = 2^s ≤ 2048`, and the order ↔ multiplet correspondence | **conjectural** | Kopp–Lagarias, Conjecture 1.2 (`Kopp2024orders`), plus the base case `d = 2`. 70 rays are collected (36 proved: d = 2 by its radical certificate and the 35 rays with 4 ≤ d ≤ 256 by the CRT certificate; 34 conditional); `d = 1024` standard order (40 rays) and `d = 2048` (64 rays) are pending and no fiducial is claimed for them. |
| Numerical values in `c<N>.json`: `fp64`, `fp32` are correct roundings and `hp` balls contain the exact coordinate | **certified by conforming finalization and export** (final 1.0 requirement; not a newly reproduced run) | Finalizers construct BALL by outward rounding their own certified normalized/gauged decoded enclosures, then certify containment and identical binary64 rounding. Export checks unique binary32/binary64 rounding from BALL and rounds `hp` outward to contain BALL, hence the exact coordinate (§§5.2, 9 of the spec). Recorded overlap checks alone do not prove this containment chain. The SIC status of the exact data remains separate (conditional at `d ≥ 512`; proved through `d = 256` for the recorded certificate inputs). See [data-guide.md §2.2](data.md#data-22-numerical-levels-and-what-each-guarantees). |
| Consistency of the stored balls with the exact SICX data | **certified** (recorded overlap/rounding checks); stronger containment required for final 1.0 | The recorded reference decoder decoded every coordinate of every ray (`d ≥ 256`: recorded full-coordinate decode, [reproducibility.md](reproduce.md#reproducing-the-published-results)), required overlap with each stored ball and identical binary64 values; the independent second reader decoded small fixtures fully and spot-checked ratios on large files. These establish the reported consistency checks, not containment of exact points in arbitrary overlapping BALL intervals. Final 1.0 instead constructs BALL from the finalizer's own certified decode (§2.2); recorded consistency checks do not establish that gate. |
| Symmetry data (stabilizer tables): each listed element fixes the ray | **certified** (numerical bound); **proved** only at `d = 2` | Each non-identity element has a certified bound `|1 − |⟨φ|Uφ⟩|²| ≤ 3.5·10^−73` over the ray's enclosure (384-bit balls). The six `d = 2` elements are verified exactly (FLINT `qqbar`). No exact verification for `d ≥ 4`. |
| Symmetry data: the tables list the whole stabilizer | **empirical** (numerical) | Binary64 exhaustive search; exhaustive under an a priori floating-point error bound in the standard rounding model, not interval arithmetic. |
| Agreement of the SICX entry rule with producer coordinates, and pure-Galois ray records | **empirical** (recorded power-of-two exports only) | General-d divisor/unit-lift decoding is a format rule. Lift independence is proved assuming a genuine unit-group action and representative kernel fixity; those are separate algebraic checks/premises, not empirical consequences or container checks ([sicx-spec.md](sicx.md#spec-sicx-10-final-exact-weylheisenberg-sic-fiducial-format) §§1, 3.5). No general-d producer agreement is claimed. |
| Literature labels (`4a`, `8a`, `8b`, …) | see note | Matched ray by ray at `d = 2, 4, 8`; at `d = 16, 32, 64` matched at the multiplet level only (uses the conjectural correspondence), orbit letters open; no labels exist for `d ≥ 128`. |
| Which solutions are new | not claimed | "Not found in the literature" is an inference from a search that was not exhaustive. |

<a id="verification-2-how-to-check"></a>
## 2. How to check

Commands below use `build/`, configured from the repository root with
`cmake -S cpp -B build -DSIC_ENABLE_FLINT=ON`. Tool names and options follow
the [current CLI reference](library.md#command-argument-contract).

<a id="verification-21-hashes-and-merkle-roots-integrity"></a>
### 2.1 Hashes and Merkle roots (integrity)

- `(cd data && sha256sum -c sha256sums.txt)` checks every listed data file when the optional
  release checksum sidecar is present. `catalogue.json` records hashes of all release records;
  the checksum sidecar additionally covers the catalogue and excludes itself.
- `sic-container fetch d<d>/m<N>` (or the `exact-data` build target) verifies each Release part, the
  whole-file SHA-256 of the reassembled SICX file and its self-check, and fails closed on any
  mismatch ([data-download.md](data.md#transport-getting-the-exact-data)).
- `sic-verify FILE` checks the container: canonical encoding, LDIR/PDIR, both
  Merkle roots, chunk digests, canonical `BALL` records, the anchor gauge, that
  the enclosure of `Σ|w_j|²` contains 1, and `accuracy_bits`. It does not
  establish the whole-file SHA-256.
  Full verification also checks canonical leaf boundaries, rescaling and branch
  rectangles.

For authenticated partial access, read the file header/footer and complete LDIR/PDIR chunks,
recompute both roots, then authenticate EXMT and derive the logical layout. Defer each ordinary
chunk header until its chunk is read; directory validation already checks its declared geometry and
the finalization skeleton. Defer RAYS-dependent counts/widths and payload semantics until the
required payloads are read. A trusted exact root authenticates exact leaves; a trusted file root
with its LDIR binding verified also makes the recomputed exact root trusted transitively. A trusted
whole-file SHA-256 makes both roots trusted only after all bytes of the same snapshot have been
hashed. Finalization needs file-level trust. Hash each consumed encoded payload and all its logical
leaves before use
([sicx-spec.md](sicx.md#spec-sicx-10-final-exact-weylheisenberg-sic-fiducial-format) §6.2). Report
direct/transitive trust, unread headers/payloads and unchecked invariants explicitly. A root in the
same untrusted footer is self-consistency only. Exact-only files have zero rays and remain valid,
with distinct exact and file roots.

<a id="verification-22-decoding-and-comparing-with-the-stored-balls"></a>
### 2.2 Decoding and comparing with the stored balls

`sic-decode` decodes coordinates of chosen rays from the exact data with certified balls, normalizes
them with the ray's anchor, and requires overlap with every stored ball and identical binary64
values. This separate consistency result does not by itself prove containment. Final 1.0
finalization instead writes BALL from that decoder's own certified normalized/gauged enclosure,
rounded outward to the stored width, certifies enclosure containment, and checks all coordinates'
binary64 rounding before admission. External publication balls are not an input. This is the
finalizer contract; regenerating files and proving their current roots are separate production
gates.

A cheaper portable check compares one ratio `w_j / w_k` of decoded and stored values
([sicx-spec.md](sicx.md#spec-sicx-10-final-exact-weylheisenberg-sic-fiducial-format) §5.2a). It is
optional, including in full container mode; a performed check that proves inconsistency must reject
the file. It cannot replace the finalizer's all-coordinate checks.

<a id="verification-23-sic-check"></a>
### 2.3 SIC check

- **Diagnostic (binary64):** `sic-verify FILE --sic-check`, `sic-overlaps FILE`, or the NumPy recipe in
  [data-guide.md §5](data.md#data-5-checking-a-fiducial-yourself). These are floating-point diagnostics.
- **Certified bound:** the library component `sic::bounds` (`bound_wh_overlap_deviations`) returns
  rigorous enclosures of the squared norm and of the maximum WH overlap deviation over a ball
  vector. A small upper bound remains a numerical tolerance statement.

`sic-check-balls VECTOR.normalized.balls --bits 100` checks a certified
normalized ball vector and fails unless the rigorous deviation bound is below
`2^-100`. This is a numerical bound, with the exact SIC status assessed separately.

<a id="verification-24-independent-second-reader"></a>
### 2.4 Independent second reader

`cpp/checkers/verifier` is implemented independently from the specification, without production SICX
code. The independent reader implements the final EXMT/LDIR/PDIR layout and 112-byte footer, with
full and scoped partial verification, trusted-root binding and certified decoding. See its
[CLI](tools.md#sic-verify-independent) and [implementation
scope](verification.md#reader-independent-sicx-10-reader). Small fixtures are fully decoded;
large-file ratio checks have the limited scope stated below, and are not all-coordinate proofs.

<a id="verification-25-exact-checks-with-parigp-small-d"></a>
### 2.5 Exact checks with PARI/GP (small `d`)

After generating `exact.json` (`exact-data` target,
[data-download.md](data.md#transport-getting-the-exact-data)), the GP reference script, independent
of the C++ code, rebuilds the field and every coordinate and verifies exactly that `|⟨ψ|D_p ψ⟩|² =
1/(d+1)` for every `p ≠ 0`:

```
SIC_EXACT_JSON=build/data/d8/m1/exact.json gp -q -s 2G cpp/checkers/gp/sicx_exact_verify.gp < /dev/null
```

Recorded results: PASS for `d = 2, 4` and both `d = 8` multiplets. At `d = 16` only the shifts `a =
0` and `a = 1` (of 16) were checked before a time cap, with no failure; a complete `d = 16` run has
not been recorded. `exact.json` is practical only for `d ≤ 16`. The script's positivity check and
its identification of complex conjugation use a floating-point embedding and are not certified by
the script.

<a id="verification-26-inequivalence-certificates"></a>
### 2.6 Inequivalence certificates

The PEC separation records (certified moment enclosures per pair) are kept with the run provenance
(`provenance/inequivalence/` in the release). A disjoint pair of enclosures proves inequivalence of
any rays inside the given vector enclosures; an overlapping pair is undetermined.

These are recorded separation results; construction alone does not regenerate a PEC certificate.

<a id="verification-27-symmetry-tables"></a>
### 2.7 Symmetry tables

`sic-stabilizer --dim D --catalogue FILE` recomputes the stabilizer table of a dimension (binary64).
`sic-certify-stabilizer STABFILE --catalogue FILE` certifies each element with ball arithmetic, and with `--d2-exact`
verifies the `d = 2` elements exactly.

<a id="verification-28-unconditional-exactness-certificate-crt"></a>
### 2.8 Unconditional exactness certificate (CRT)

`sic-certify-crt FILE.sicx --threads T` (source `cpp/checkers/crt`, built with
`-DSIC_ENABLE_FLINT=ON`; [recorded run scope](reproduce.md#reproducing-the-published-results))
proves that every Galois word of the multiplet in `FILE.sicx`, hence every ray, is an exact WH SIC
fiducial. A run ending in `RESULT: EXACT SIC for every Galois word … [PROVED]` (exit code 0) means:
the tower algebra is a field `L`; the action records are automorphisms and `L/K` is Galois with the
stored group; the printed word `c*` is complex conjugation for the embedding fixed by the branch
rectangles; and every quartic residual `R_{a,k}` of every ray is exactly zero in `L`, the vector
being nonzero. Each identity is checked at all degree-one points modulo distinct completely split
primes `p > 2^61`, and a norm lemma with a **proved** conjugate bound and a **proved** denominator
bound turns these checks into exact equalities. Here **proved** means: a mathematical proof whose
computational steps are exact modular arithmetic and certified ball arithmetic, with no imported
premise. Trusted are only FLINT/Arb, the compiler and the tool. The tool has its own SICX reader
(written from the specification) and does not link the production code.

To rerun (example, `d = 16`, a few seconds; the recorded `d = 128`
standard run took 229.43 s on 120 threads):

```
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release -DSIC_ENABLE_FLINT=ON -DCMAKE_PREFIX_PATH=<flint-install>
cmake --build build --target sic-certify-crt
sic-certify-crt FILE.sicx --threads 16       # FILE.sicx: e.g. from `sic-container fetch d16/m1`
ctest --test-dir build -L crt                                                # includes negative controls
```

The certified data are identified by the printed `exact root` (finalization preserves it within a
given exact presentation). The recorded proof scope is nine multiplets: d4 (1 ray), d8 maximal (1),
d8 standard (1), d16 (2), d32 (2), d64 (4), d128 maximal (2), d128 standard (10), and d256 standard
(12). Proofs cover every Galois word of each checked presentation. Release production must bind the
current exact roots to a successful certificate; none of the prior roots is printed as a current
root here.

#### Recorded certificate input identities

These exact roots identify the successful CRT certificate inputs. They are **not current
release-production bindings**. The recorded proof applies to a supplied file only when its exact
root matches the checked input and the selected rays are covered. Release production must establish
and publish the current association in its generated manifest; a changed presentation requires a
fresh proof. The norm runs at d4, both d8 orders and d16 used the corresponding recorded roots
below.

| multiplet | recorded certificate input exact root | rays |
| --- | --- | ---: |
| d4 | `3cae1d4bdaaa4a2c7a66a634c112a3294aa9b3bcdeb0c386357c209d2cd5514d` | 1 |
| d8 maximal | `fc010f50bba52dedcf42f7faf27b6a2bcd727ff3e0b87781254948b58d7e6480` | 1 |
| d8 standard | `5db07b2ba35e84bc8a87700bbdde2bac55893aa2257a70b314e7a664c5f57772` | 1 |
| d16 | `1bbac1760fda36045860c0ebcb5447a107c145d0fa12bdba82dfc8bc6b2e3a81` | 2 |
| d32 | `b90ac0b11810a8d6965969a6a4791c9d13c1a41adf521bc1b70b60fa1a120450` | 2 |
| d64 | `75a764d6d482df7e67d6b834febb6e2126f908a1229a86a3173c2b7e9bb4f360` | 4 |
| d128 maximal | `5e4296344cd2ecf84b86446c30424f879026234689cf274123ddf27bd4acf374` | 2 |
| d128 standard | `5992c4a54f72aa781d9e3f04ef50b424dc31f82c526e834f083e6f94b0b1b8f5` | 10 |
| d256 standard | `bce51bd03834a83526928b71566fd8b2d4712c1e50b265bba4e0fe3af30184c8` | 12 |


`d ≥ 512`: no exact certificate was run (projected cost is given below).

<a id="verification-29-high-precision-exactness-certificate-norm-argument-second-method"></a>
### 2.9 High-precision exactness certificate (norm argument, second method)

`sic-certify-norm FILE.sicx --threads T` is a second, independent method with its own SICX reader
and exact tower arithmetic in `cpp/checkers/norm`, written from `docs/sicx.md`. It links only FLINT
3 (including Arb) and the platform thread library. The
`sic_test_support_independence_sic_certify_norm` CTest checks its include and link dependencies and
checks for copied production code. Certificate runs proved the five catalogue rays in d4, d8
standard, d8 maximal and d16, using their separately recorded exact roots. All four runs exited 0.
Current reader tests also include synthetic conformance fixtures, which are not SICs. The quartic
residuals are algebraic integers after an exact power-basis scaling `q`, all their conjugates are
bounded by `2^m`, and a ball computation shows `|R_{a,k}| < 2^{−(2[L:K]−1)m}` at the branch
embedding. Its working precision grows like `[L:K]·h` bits and its exact power-basis preprocessing
like `[L:K]²`; practical for `d ≤ 16` (executed: five rays, all agreeing with §2.8). Status of a
PASS: **proved** (certified computation of an exact statement).

Build with `cmake -S cpp -B cpp/build -DSIC_ENABLE_FLINT=ON`, then `cmake --build cpp/build --target
sic-certify-norm`; the binary is `cpp/build/sic-certify-norm`. Use
`-DCMAKE_PREFIX_PATH=FLINT_INSTALL` if FLINT is installed outside the system search path.
See the complete [norm checker options](tools.md#sic-certify-norm) for syntax and defaults.
`--estimate` performs exact preprocessing and a precision estimate without a SIC verdict;
`--leaf-estimate` gives a heuristic feasibility estimate without exact preprocessing or a SIC
verdict. Exit codes are 0 for all selected rays certified (or a completed estimate), 1 for an
inconclusive certificate or BALL mismatch, 2 for usage errors, and 3 for a refused input or
computation. Numeric CLI values must be full-string unsigned decimals with checked overflow;
malformed values, missing values and out-of-range options exit 2 before certification. `--max-bits`
accepts
1..9223372036854775807, `--ray` accepts a zero-based index, and `--threads` must be positive.
Input-only limits include 64 MiB per directory/JSON payload and 256 MiB per physical/logical
payload; see the checker input/admission contract below for the other limits. Mathematical
certification requires `d, [L:K] ≤ 4096`; these are resource limits, not feasibility guarantees.
Exact certification supports real quadratic base fields and quadratic tower levels at the final
high-precision stage. Odd levels can be used for estimates, but final certification refuses them.
Exact-only files can be read for estimates; certification requires finalized rays and balls.

<a id="verification-3-what-is-not-verified"></a>
## 3. What is not verified

- All steps of the imported mathematical inputs except one analytic lemma (RW26 Thm 3 at real τ)
  have been checked (the unresolved Fourier/five-term step stated in §1); construction-based
  exactness for `d ≥ 512` is relative to that lemma and to the recorded premises (the listed roots
  through `d = 256` are proved independently, §2.8).
- Newton nonsingularity is certified per accepted run (Krawczyk inclusion on the box `B0`: every
  Jacobian of the selected subsystem on `B0` is nonsingular and the subsystem has exactly one zero
  there), but not uniformly. Open: full Jacobian rank `2d − 2` at the ghost for every `d = 2^s` and
  class; a well-conditioned square subsystem (amplification `2^{O(P_1)}`) that the selection
  procedure finds; an a priori seed precision for the `B0` test. Transported-decoding precision
  bounds are not proved in general.
- The unconditional CRT certificate (§2.8) covers 9 multiplets through `d = 256` (35 rays); the 34 rays
  with `d ≥ 512` stay conditional on the imported results and premises until a certificate is run.
- The classification is not proved: the count 174 and the order ↔ multiplet correspondence are
  conjectural, and pairwise inequivalence of the collected rays does not show that the collection is
  exhaustive.
- Orbit letters at `d = 16, 32, 64` are not determined.
- The stabilizer tables are exact only at `d = 2`.
- `d = 1024` standard order and `d = 2048` are pending; no fiducial is claimed there.
- A real download from GitHub Releases has not been tested (only offline tests).

## Independent checker contracts

The checkers have separate parsers, arithmetic and readers. Their source/build paths are
[CRT](../cpp/checkers/crt/CMakeLists.txt), [norm](../cpp/checkers/norm/CMakeLists.txt),
[reader](../cpp/checkers/verifier/CMakeLists.txt),
[stabilizer](../cpp/checkers/stabilizer/CMakeLists.txt) and GP
([SICX](../cpp/checkers/gp/sicx_exact_verify.gp), [Hoggar](../cpp/checkers/gp/hoggar_verify.gp)).
The root FLINT build enrolls their tests; CMake and CTest enforce include, link and copied-source
independence. Help and CLI exit modes are in [tools](tools.md).

<a id="crt-sic-certify-crt-unconditional-crt-exactness-certificate"></a>
## CRT exactness method

`sic-certify-crt FILE.sicx` proves, with no unproved hypothesis, that every Galois word of the
multiplet stored in a SICX 1.0 file is an exact Weyl–Heisenberg SIC fiducial. Every published ray of
a multiplet is a Galois word of its file, so the proof covers every ray. Nothing analytic is used:
no special values, no RW26/AFK25 theorem, no Artin reciprocity. The SICX premises of the
specification (`docs/sicx.md` §1) are not used either; each one is either verified or not needed:
genuine automorphisms, commuting actions, integrality, the entry rule, pure Galois rays.

See the [recorded costs](#crt-measured-cost) and [certificate scope and
roots](#verification-28-unconditional-exactness-certificate-crt).

<a id="crt-what-a-proved-run-establishes"></a>
### What a PROVED run establishes

Let `A = K[g_0, …, g_{t−1}]/(f_j)` be the tower algebra of the file, with `K = Q(√D0)` and `d = 2^s
≥ 4`. The tool proves:

1. **(W)** `A` is a field `L`. Each level has an irreducibility witness: a degree-one prime of `F_j`
   modulo which `f_j` is irreducible.
2. **(E, G)** Every action record defines an automorphism of `L/K`. `L/K` is Galois, and `c ↦ σ_c`
   is an isomorphism `⊕ Z/n_l → Gal(L/K)`.
3. **(C)** For the embedding `ι` fixed by the branch rectangles, `ι ∘ σ_{c*} = conj ∘ ι`. Here `c*`
   is the printed conjugation word, an involution.
4. **(R)** For every word `c` and all `a, k ∈ Z_d`, the residual
   `R_{a,k} = (d+1) Σ_j x_j x_{j+a+k} C(x_{j+a}) C(x_{j+k}) − N² (δ_{a,0} + δ_{k,0})` vanishes in `L`.
   Here `x_j = σ_c σ_{word(j)}(η_{r(j)})` and `N = Σ_j x_j C(x_j)`.
5. The ray vector is nonzero.

Together these give: for every ray `v = ι(x)`, `v/‖v‖` is an exact WH SIC fiducial. "Proved" means
exactly this.

The certified vector is the one defined by the file's exact data and `ι`. Its agreement with the
published BALL records is the decoder's statement (`sic-decode`). It is not re-checked here.

**Method in one paragraph.** The tool runs modular checks at completely split primes `p > 2^61`, `p
≡ 1 (mod 2d)`, evaluating every element at all `2[L:K]` degree-one points. These checks cover:
- the action relations and the group relations;
- the stabiliser identities used by the `⟨σ_5, σ_{−1}⟩` orbit reduction;
- all residuals, checked row by row in the Fourier domain.

A norm lemma makes these checks exact: if `Σ log2 p_i > B + T/[L:Q]`, the element is 0. Here `2^B`
bounds every conjugate (certified ball arithmetic at all embeddings), and `2^T` bounds the norm of a
proved denominator ideal.
- **Lemma D** bounds `T` from the trace-dual leaf representation and needs no integrality assumption.
- **Lemma I** sharpens Lemma D level by level. It proves that `G_j = 2^{J_j} g_j` is an algebraic
  integer by an exact integer characteristic polynomial, reconstructed by CRT and verified against
  the proved denominator bound. The shift `J_j` is taken from authenticated EXMT
  `generator_rescaling` only as a candidate integral-generator hint. If any recorded shift
  exceeds 64, all shift hints are discarded. On quadratic levels, a failed integrality
  check retries `J + 1 … J + 4`.

Complex conjugation and the base embedding are certified by root isolation.

<a id="crt-usage"></a>
### CRT input, output and resource contract

See [generated CRT help](tools.md#sic-certify-crt) for options and exit codes. A bound-only success
is not a proof; a prime override is an incomplete diagnostic. Usage errors exit 2 and prove nothing.
Numeric options require full-string unsigned decimals with checked overflow: `--seed` accepts
0..18446744073709551615, `--integral-max-dim` accepts
0..2147483648 (0 disables Lemma I), `--primes` accepts 1..1048576, `--prec` accepts
64..1048576 bits, and `--threads` must be positive. A completed `--primes` run exits 3;
`--bound-only` skips residual certificate primes and exits 0 after its diagnostic checks.
`--primes` and `--bound-only` are mutually exclusive (usage error, exit 2).

**Input.** Final SICX 1.0 only, with logical leaf limit **L = 14** (16 MiB physical profile target).
The input layer implements the general-dimension grammar for every `2 ≤ d ≤ 2^20`, including the
imaginary quadratic base at d=2 and the rational base at d=3. It derives the squarefree base,
divisor positions, canonical CRT unit generators and their orders; it checks `unit_generator_words`
and uses the smallest positive unit lift for entries. The CRT mathematics and certificate remain
explicitly limited to `d = 2^s ≥ 4`; general-d container conformance is not a general-d SIC
certificate.

The certificate performs full container verification before mathematical work: header/features, the
112-byte footer, LDIR/PDIR and both RFC 6962 roots, all physical headers and payload digests,
domain-separated logical digests, EXMT and the closed grammar, canonical leaf encoding, element
primitivity/minimality/attained bit bounds, authenticated greedy rescaling, exact canonical branch
rectangles and their minimum precision, optional JSON, and finalized BALL gauge/norm/tight accuracy.
BALL agreement with exact decoding and the SIC identities remain separate mathematical checks.

Optional `--exact-root HEX`, `--file-root HEX` and `--sha256 HEX` bind the input to a trusted
external identity. The output distinguishes supplied trusted roots from roots merely recomputed from
the file. Full verification pins the whole-file hash and checks it again after loading; the
certificate repeats it after its mathematical work. Size and snapshot timestamp changes are also
detected on payload reads and whole-file hashing. Trust routes are printed explicitly: a trusted
file root binds the complete LDIR and therefore the exact root; a verified trusted whole-file hash
binds both roots. Every supplied identity must match.

The separate `sic-check-crt-input` executable exposes full or partial container verification:

<!-- shell:template -->
```sh
sic-check-crt-input FILE.sicx --exact-root HEX --file-root HEX
sic-check-crt-input FILE.sicx --partial-element 3 --exact-root HEX
sic-check-crt-input FILE.sicx --partial-chunk 5 --exact-root HEX --file-root HEX
sic-check-crt-input FILE.sicx --partial-metadata
```

`--partial-metadata` cannot be combined with `--partial-element` or `--partial-chunk`;
element and chunk requests may be combined and repeated. Malformed IDs, missing arguments and unknown
options are usage errors (exit 2); data errors still exit 1 and resource/precision refusals exit 3.

Partial bootstrap validates both complete directories, their headers, EXMT and the derived coverage.
Ordinary headers are deferred until their chunks are read. A requested element chunk is consumed
only after every constituent leaf checks. A BALL request first validates RAYS and every declared
BALL width/count; auxiliary/finalization payloads are checked when requested. An entire element is
needed for its canonical statistics. Once every exact chunk has been consumed, the partial reader
also completes rescaling and branch checks. Unread headers/payloads and their dependent semantics
remain explicitly unchecked. Exit codes are 0 for the stated verification scope, 1 for invalid
input, 2 for usage, and 3 for resource/precision refusal.

Resource admission is explicit in
[`Limits`](../cpp/checkers/crt/src/model/input_model.hpp): 64 MiB JSON, 256 MiB raw
physical/logical payloads, 512 MiB aggregate directory wire bytes, the format's directory count
bound, 4,194,304 nonzero coordinate pairs/constraints, 16,777,216 dense coordinates, branch
precision up to 1,048,576 bits, and coefficient height up to 1,048,576 bits for Qbar operations,
including the odd prefix. Axis-quadratic towers over the real base, optionally following one initial
odd-degree level, use a certified ball path: the odd root family, selected real root and real
relative dual are first proved exactly in Qbar, then enclosed by bounded certified real refinement;
proved zero components of earlier roots make the phase of every trace term exact; a real radicand
with certified sign determines the next axis. Nearest-integer rounding is certified by strict
outward bounds inside an open rounding cell; unresolved half ties retain exact rounding. Comparison
with the squared dyadic candidate proves a component is not a singleton. Exactly represented dyadics
use exact rounding. All root families are checked for the minimum common precision and canonical
strings. Non-axis or inconclusive cases retain the exact FLINT Qbar fallback, including singleton
equality, with a conservative algebraic degree admission of 4,096; the same Qbar limits apply to the
odd prefix. Larger valid inputs may be refused. Limits can be raised explicitly by an API caller; no
refusal is reported as conformance. The CRT hint ceiling remains 64 independently of authenticated
EXMT shifts, which may have 18 decimal digits and are checked by valuation arithmetic without
integer expansion.

**Output lines** (stdout):
- `sha256`, `exact root`: the identity of the certified data;
- `(G)`, `(C)` and `integral structure level j: …`: one line per step and level;
- `bounds …`: per-level dual bounds and per-representative denominator bounds;
- `residual: B = …, T/n = …; required sum log2 p_i > …`, then `certificate primes needed: k`;
- `prime i p=… ok (…)`: the first four primes and every 100th, with timings;
- `(W) irreducibility witnesses`, then `primes: … distinct, sum log2 p = …`;
- `ray LABEL word …: EXACT SIC [PROVED]`: one line per RAYS entry;
- the final line, either `RESULT: EXACT SIC for every Galois word of the multiplet (D rays …)
  [PROVED]` or one of `RESULT: FAILED`, `REFUSED: …`, `RESULT: INCOMPLETE`, `RESULT:
  NEGATIVE-CONTROL`.

Example, for a finalized file with a checked current exact-root binding: `sic-certify-crt
multiplet.sicx --threads 120 --seed 1`.

RAYS labels follow increasing numeric suffixes and increasing lexicographic numeric Smith tuples
(first coordinate first, §5.1a); `c0` may have a nonzero word. The top-level PROV
`generator_rescaling` key is unconditionally forbidden; nested recorded records are permitted. Both
simultaneous odd-action signs are permitted and authenticated by the exact root. Container
verification does not claim the producer’s private coset order or the Vandermonde identity. The
mathematical certificate verifies action quotients and requires a nonzero denominator before
division.

The optional BALL/exact ratio check is not performed; scope output says so. Outward BALL
construction and binary64 agreement are finalizer obligations, not container checks. Container
success and an exact SIC certificate do not by themselves establish BALL containment.

<a id="crt-trusted-components"></a>
### Trusted components

The proof is complete given the correctness of:
- FLINT 3 / Arb: ball arithmetic, including the certified root isolation of `acb_poly_find_roots`;
  `n_is_prime` (deterministic below `2^64`); `nmod_poly` root finding and irreducibility; `fmpz`
  CRT;
- the C++ compiler and standard library;
- this tool's code.

<a id="crt-independence"></a>
### Independence

The tool is a separate verification tool. Nothing in the library, the SICX decoder or `exact-data`
depends on it. It reads SICX with its own `sicx_input.hpp`, written from the specification; its
SHA-256, JSON and varint routines are implemented within this checker. The readers share no
implementations. It does not include or link the production SICX/exact code (`sic/…`).

Two checks enforce this:
- `CMakeLists.txt` refuses production link targets and include paths at configure time;
- the CTest `sic_test_support_crt_independent_sources` rejects `#include <sic/…>`.

The high-precision certificate `sic-certify-norm` (the norm argument; recorded proof examples
through `d = 16`) is a second, different method with its own reader.

<a id="crt-tests"></a>
### Tests

`ctest -L crt` exercises eleven input groups (valid, rejection mutations, boundaries/regrouping,
partial trust/scope, amendments, primitive guards, mixed odd/quadratic branches, targeted guards,
general dimensions, deterministic fuzz mutations, and public CLI), five fixture CRT proofs,
bound-only diagnostics, strict CLI option/error cases, help/parser defaults, help, source
independence, a shift retry, and three deliberate mathematical negative controls. The independent
executables are enrolled in the repository’s independence audit. Rehashed negative fixtures pin
rejection categories so a different later failure cannot silently substitute for the intended check.
Five additional `sic-test-crt-faults` cases interpose OS/FLINT failures to test otherwise rare
fail-closed paths; interposition is test-only.

`test_writer.hpp` is a test-only writer implemented from the final specification. It uses OpenSSL
SHA-256 independently of the reader's hash implementation and emits synthetic general-d exact-only
containers and d=4/d=8 finalized container controls. Tests include multi-leaf elements in different
physical groupings, unchanged exact-prefix/LDIR bytes on finalization, odd-degree/action grammar,
exact rounding ties of both signs, and a minimum-precision-256 branch construction. These are
container controls, not new SIC claims. d=2 balanced pairs are also covered.

`sic-test-crt-input --write-fixtures DIR` saves eight reviewable d=4/d=8 fixtures.
`sic-test-crt-external OUTDIR FILE...` verifies an executable-produced corpus, reproduces every byte
with the independent OpenSSL writer, checks both roots and whole-file hashes, rejects raw and
independently rehashed corruptions, and exercises partial trust/completion. `sic-test-crt-input
--write-general-fixtures DIR` saves the general-dimension fixtures for d=2,3,5,6,7,9,12,16.
Exhaustive unit-group/entry oracles include faithful C4/C6 factors, composite CRT lifts, full
exponent digits, both real integral bases, rational pair rejection and the first-coordinate-first
Smith label rule. Recorded CRT runs cover nine multiplets through d256; current release production
must bind any proof to its actual root. The CRT/input/independence selection passed 37 tests,
including 19 mixed-branch checks, signed initially nonexact half ties, unresolved singleton fallback
and low-degree admission controls. An external corpus of five finalized files passed 467 checks and
rejected 452 mutated copies.

<a id="crt-measured-cost"></a>
### Recorded CRT cost

**SICX 1.0 measurements.** Measurements on a shared AMD EPYC 7763 host with FLINT 3.6.0, GCC 11.4
and `-O3`. All runs used 120 threads, the default proved bound, no prime-count override, and their
recorded exact presentations. These are measurements of those inputs; they are not current
release-production certificates. Every run finished `[PROVED]`. Wall time and peak RSS are GNU
time's whole-process measurements, including input admission. Shared-host timings are approximate.
Lemma I covers levels with `2[F_j:K] ≤ 8192`.

| file | [L:K] | levels with Lemma I | primes | wall (s) | threads | peak RSS (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| d4 | 8 | 3/3 | 1 | 0.05 | 120 | 6.00 |
| d8 maximal | 16 | 4/4 | 2 | 0.06 | 120 | 6.00 |
| d8 standard | 64 | 6/6 | 8 | 0.10 | 120 | 4.00 |
| d16 | 512 | 9/9 | 25 | 0.30 | 120 | 6.00 |
| d32 | 2,048 | 11/11 | 95 | 1.34 | 120 | 65.93 |
| d64 | 16,384 | 13/14 | 349 | 12.61 | 120 | 1003.71 |
| d128 maximal | 32,768 | 13/15 | 627 | 20.70 | 120 | 1626.95 |
| d128 standard | 163,840 | 11/16 | 3,293 | 229.43 | 120 | 6370.90 |
| d256 standard | 786,432 | 12/19 | 17,210 | 5911 | 120 | 31129.82 |

Cost per prime is dominated by the residual tables, about `4[L:K]·d·log d` modular operations. Prime
counts depend on the exact generator presentation and its proved height/denominator bounds. These
are measurements for the checked presentations; a changed exact root requires a fresh certificate.

<a id="crt-limits"></a>
### Limits

- `d ≥ 4` only, real quadratic `K`; point indices are 32-bit (`2[L:K] ≤ 2^32`).
- The proved bound is pessimistic at levels where Lemma I is not run. There Lemma D grows by about
  ×1.5 per level, and the residual bound carries a factor `4d`. With Lemma I the prime count is `≈
  4d·Σ_j (1 + J_j + ½·avg log2|c_j|)/61`.
- Projections (estimates, not measurements): d512 ≈ 8·10^4 primes ≈ 4·10^3 CPU-h; d1024 ≈ 4·10^5
  primes ≈ 2·10^5 CPU-h; d2048 out of reach. A full d2048 certificate is not a release requirement.

<a id="norm-independent-sicx-input-and-exact-certificate"></a>
## Norm checker input and admission

The norm input reader independently implements SICX 1.0 sections 2–7a. It neither includes nor links
production SICX or another checker. Its container checks are distinct from the mathematical
certificate.

```
sic-certify-norm FILE --verify-input
sic-certify-norm FILE --verify-input --exact-root HEX --file-root HEX --sha256 HEX
sic-certify-norm FILE --partial-element ELEMENT_ID --exact-root HEX
sic-certify-norm FILE --partial-chunk PDIR_INDEX --exact-root HEX --file-root HEX
```

With a supplied trusted root, `--partial` authenticates the directories and EXMT
without requesting an element.
Roots supplied on the command line are assertions of **external trust** by the
caller; extracting a root from the same untrusted file is not authentication.
Full verification without these arguments reports self-consistency. The output
reports direct trust separately from transitive trust: a trusted file root binds
the complete LDIR and hence the exact root; a matched whole-file SHA-256 trusts
both roots. Every supplied identity must match, even if another matches.
A whole-file hash assertion scans all bytes, including during partial semantic
verification, and recomputes the digest. Keep the input immutable throughout
verification: this reader does not check source inode or timestamp changes.
A mathematical certificate remains a separate action:
omit the input-only options to run the certificate algorithm.

The C++ interface is `Reader(path, ReadOptions)`. `report()` records checked
physical chunks, complete elements, matched trust anchors, and whether global
branch/rescaling checks ran. `element_leaves()` refuses an incompletely verified
element. A partial physical-chunk request checks every logical leaf in that
chunk, including unrequested leaves. Defining and action elements below requested
field levels are derived from EXMT and loaded conservatively. Reading BALL also
loads RAYS and earlier BALLs. Optional/finalization bytes require a trusted file
root. Global rescaling and exact branch canonicality are deferred until all exact
element leaves have been consumed, including in partial mode.
A constructor failure exposes no partially consumed values.

The reader checks both directories before payload use, and each ordinary physical
header when its chunk is consumed. Unread ordinary headers remain unchecked in
partial mode. Directory geometry, types/flags/codecs, ownership and finalization
skeleton checks are never deferred. RAYS-dependent BALL counts/widths are checked
when RAYS is read; reading RAYS does not consume unused BALLs.
It seeks bounded ranges, keeps the directory data and dense exact coordinates,
and processes one physical payload at a time. Supplying a whole-file hash uses
an incremental 1 MiB scan; declared lengths never allocate a complete file.
`InvalidInput` denotes structural/encoding/mandatory semantic inconsistency.
`ResourceRefusal` denotes an implementation resource or exact-comparison limit.
The mathematical helper `require()` reports refusal.

Large pure-axis quadratic towers first use certified Arb contractions with
exact-zero preservation, strict nearest-even rounding proofs and workspace
admission. Uncertain singleton equalities and mixed complex squares fall back
to FLINT algebraic-number arithmetic (`qqbar` and its generic root interface),
with exact nearest-even comparisons,
singleton equality, all-root separation and the common minimum precision.
Rescaling verification symbolically undoes shifts in the coordinate valuations,
then checks the prescribed greedy result and zero residual shifts. It never
applies the shifts again to decoded stored coordinates.

Default input limits are `d <= 4096`, `D <= 65536`, 4,194,304 total dense
coordinates, 64 MiB per directory/JSON payload, 256 MiB per physical/logical
payload, one MiB per integer, JSON depth 32 and 1,048,576 JSON nodes. Branch
precision, branch mantissa bits and branch exponent magnitude are limited to
`2^20`; exact algebraic operations use degree 256 and height `2^20`.
`ReadOptions::algebraic_limits` can lower those two budgets. Component extraction,
trace contraction, trace-dual construction, integer/dyadic scaling and polynomial
root isolation have conservative pre-expansion degree/height bounds and result
checks. Admission may refuse a calculation whose eventual minimal polynomial
would fit; the reader never expands first and relies only on a later size check.
The two dimension
limits can be raised with `--max-dimension` and `--max-field-dimension`, and
allocation limits are explicit in `ReadOptions`. The mathematical certificate
requires `d,D <= 4096` and real-base-field restrictions; input-only
verification accepts `d=2`. No mathematical algorithm or precision policy is
changed by raising an input limit.

`sic-test-norm-reader SCRATCH_DIRECTORY [4|8]` writes its own fixtures and mutation corpus.
`test_writer.hpp` uses the SHA-256 primitive but implements its own JSON serialization, encodings,
directories, domain messages and RFC tree. Known SHA and an externally computed tagged-leaf hash
constrain that shared primitive. The generated `d4.sicx` and `d8.sicx` use the genuine quadratic
extension `K(sqrt(2))/K` and its nontrivial C2 action. Their representatives are constant, their
BALLs enclose the uniform unit vector, and they are **not SIC fixtures**. The larger grouping
fixtures deliberately exercise container checks with a synthetic tower; they do not establish the
imported field/group premises. These synthetic fixtures make no SIC claim. Separate norm-certificate
computations succeeded on d4, both d8 orders and d16; the scope is five rays and their recorded
exact presentations, subject to current-root binding before release.

Full container verification checks the mandatory input invariants, not the
imported irreducibility/group premises or exact SIC identities. BALL-to-exact
comparison is separate, as specified by section 6.1. Text interchange, release
catalogues, parts manifests, network transport and persistent caches are not
inputs of this local binary reader. A transport implementation must enforce its
own conditional requirements before supplying a stable local file.

The optional BALL/exact ratio spot check is not performed by this input
layer, and the verification summary says so. Container success establishes no
BALL/exact agreement, binary64 agreement or SIC property. Exact singleton branch
components are decided algebraically, including zero and nonzero dyadics, without
waiting for a numerical enclosure to become a singleton.

When PROV is consumed, its parser rejects the top-level `generator_rescaling`
key; nested occurrences are permitted provenance and have no interpretation effect.
Unread PROV remains unchecked in partial mode.
The reader uses the frozen leaf limit L=14 and raw profile target 16 MiB;
other physical groupings are valid and the canonical raw profile is not imposed
as an extra container restriction.

Ray labels follow increasing words (lexicographic Smith tuples); c0 may have a
nonzero word. The signed odd denominator and numerators are authenticated exact
data. Container checks do not prove a private Vandermonde order or the action
quotient identities. Outward BALL construction is a finalizer obligation;
this input layer verifies the stored BALL-only invariants and reports its scope.

Only the EXMT schema specified in [SICX 1.0](sicx.md#spec-51-exmt-required-exact-metadata) is accepted. The
reader derives the base field for every dimension, including rational K at d=3, the divisor
representatives and the prime-power unit generators with their canonical CRT lifts.
`galois.unit_generator_words` must have the derived count, canonical Smith residues and the
specified order relations. Entry lookup uses local inverse tables and the smallest positive unit
lift. Nonzero second components are rejected in the rational case. The summary identifies
field/group/action and representative kernel fixity as premises; it does not certify them.

General-d support here is container and branch validation. The norm-certificate
implementation supports power-of-two dimensions
and explicitly refuses other dimensions before constructing its exact algebra.
Finalization constructs BALL from its own certified decode; this tool requires
no external ball input. Synthetic fixtures are conformance data, not SICs.

The quadratic path admits at most 512 MiB of estimated live ball workspace
and precision at most 2^20 before allocation. Rounding-to-integer admission
separately bounds the exponent before materializing an integer midpoint.

<a id="reader-independent-sicx-10-reader"></a>
## Independent reader behavior

The independent reader implements SICX 1.0 for all integer dimensions 2 through 2^20, divisor
representatives, canonical prime-power unit generators with CRT lifts and generator words. The
frozen constants are logical exponent L=14 and canonical raw payload target 16 MiB. The target is a
writer profile choice; other physical groupings remain valid. Numeric ray labels and their Smith
words increase together in lexicographic numeric Smith-tuple order (first coordinate first); c0 may
have a nonzero word. It includes no production SICX code and links only external libraries.
Undefined META/INDX chunks are rejected.

Build via the root FLINT configuration or the standalone [reader CMake
project](../cpp/checkers/verifier/CMakeLists.txt). Complete command syntax is in
[tools](tools.md#sic-verify-independent).

Choose the build parallelism from the CPUs assigned to the task. The standalone
tests additionally require OpenSSL Crypto; the reader itself uses its own SHA-256.
Both targets are also available in the main FLINT-enabled CMake build.
`--help` prints usage to stdout and exits 0; usage errors exit 2 and prove nothing.

`--decode full` checks all coordinates of every requested ray against BALL;
`--decode ratio` checks the specified ratio for every requested ray. `--ray`
restricts the requested rays. If the step budget prevents either explicit mode
from checking one or more requested rays, the reader reports the required `--max-steps`
and refuses with exit 3. `--decode auto` is a budget-limited partial spot check
(with a release-mode fallback to ensure at least one ray is checked); it can
exit 0 after its selected checks pass. `--decode off` performs no decode check.
Exit 0 means the requested checks passed, exit 2 rejects invalid data, exit 3
indicates a precision or resource refusal, and usage errors also exit 2.
`--extra-bits N` requires an unsigned decimal of at most 18 digits; accepted values
are clamped to 1048576 (2^20) extra bits. Missing, malformed and overflowing values exit 2.

Full verification checks both directories and RFC 6962 roots, every physical
payload digest, every domain-separated logical digest, EXMT-derived leaf layout,
closed chunk grammar, codec/feature/flag rules, canonical JSON/integers/runs,
original-basis pairs, re-encoding, attained bit bounds, primitive scales,
minimal support, inverse and residual generator rescaling, canonical exact
branch rectangles, RAYS, BALL gauge/norm/accuracy, PREC and PROV. It always
streams the whole-file SHA-256. No mandatory check can be disabled.

`units.hpp` derives the unit decomposition and local logarithm tables. Entry
words use the smallest positive unit lift and the ordered generator words;
Nonconforming metadata is rejected. Both numerical and exact
evaluators derive the rational or quadratic integral basis from d. Rational
coordinates require y=0. Kernel fixity, the field, group and action identities
remain explicit algebraic premises. Ratio mode tries proper divisor positions.

Inverse rescaling verifies greedy tightness in reverse gain order. It caps
future slack at 512 before multiplication, since every weight is at most 511.
This proves the recorded shifts without materializing source valuations that
can exceed `uint64_t`, and preserves the stored valuation table.

`branches_numeric.hpp` first evaluates all defining roots with certified Arb
arithmetic. Strict grid-cell comparisons prove nearest-even choices; an exact
zero-radius component proves singleton equality. An unresolved singleton or
tie uses the algebraic fallback, never approximate equality.

`branches.hpp` independently evaluates the tower coefficients using FLINT's exact algebraic numbers.
It checks squarefreeness before root enumeration, selects the exact root by exact rectangle
comparisons, and reconstructs every root rectangle at each candidate precision. Nearest-even ties
and singleton components are exact comparisons. This verifies the global minimum precision, not just
isolation of the stored selected root. The decoder uses the certified base roots, with exact
singleton components and full branch containment. Transported roots use a strictly contained initial
Newton image and certified nonzero action denominators.

The default algebraic-fallback resource limit is tower degree 256; the certified interval path does
not require a full compositum. The limit can be raised explicitly with `--branch-degree-limit`
(maximum 2^20). Further reader resource limits are: branch precision and absolute exponents 2^20;
branch strings 1 MiB; exact coefficient support 2^18 and bit height 16,384; conservative retained
BALL admission 1 GiB, including declared midpoint width and allocation headroom; rescaling weight
keys 2^24; text lines 256 MiB. The spec's 64 MiB JSON, 256 MiB physical payload, 2^24 directory
entries, 2^20-byte integer magnitudes and nesting 32 limits also apply. Files beyond a resource
limit return refusal, never PASS. Large actual presentations can therefore require a higher limit or
be refused.

Partial mode reads the complete directories and EXMT, checks both roots and all derived coverage,
and verifies every leaf of each requested element, including all leaves sharing its physical chunks.
It checks those elements' primitivity, attained bounds and minimality. It does not decode complex
values or inspect other payloads. Ordinary physical headers are checked only when their chunks are
consumed. Its final line names deferred checks; once all exact elements have been consumed, global
rescaling and canonical branches are mandatory even in partial mode. A matching trusted file root
also transitively authenticates the exact root through the complete LDIR binding. Whole-file hashes
and text checks require full mode. Expected roots must come from a trusted external source.

Normal full-mode output states which identities were recomputed and which matched supplied trusted
values or became trusted transitively. Whole-file SHA-256 trust requires hashing all bytes of the
same stable snapshot; source inode, size and modification/change timestamps are checked for changes.
Full container verification is not a field, automorphism, classification or exact SIC proof. BALL
versus exact decoding is a separate check: `full` checks overlap and certified binary64 agreement
for all normalized coordinates. Binary64 uniqueness compares IEEE encodings, including the sign of
underflowed zero. This does not prove containment of an independent enclosure in arbitrary stored
BALL. Section 5.2 requires the finalizer to construct BALL outward from its own certified decode and
prove rounding agreement. The independent reader consumes the resulting snapshot and accepts no
external BALL input. `ratio` is the optional §5.2a spot check and must reject proven inconsistency;
`auto` uses the step-budget policy. `--release` requires a finalized file, decoder source record and
at least one decoded ray.

Exit status is 0 for `RESULT: PASS` or explicitly scoped `RESULT: PARTIAL PASS`,
2 for `RESULT: REJECT`, 3 for `RESULT: REFUSED` (precision/resource limit), and
2 for usage errors. Partial PASS is never full-file verification.

`cpp/checkers/verifier/tests/writer.cpp` contains a spec-derived test-only writer with separate byte, integer, hash-domain
and RFC tree encoders; its hashes use OpenSSL. The fixture suite in `cpp/checkers/verifier/tests/tests.cpp` and
`cpp/checkers/verifier/tests/conformance_*.cpp` uses it to create small synthetic containers at d=4 and d=8, finalization
pairs, a 65,536-coordinate support for physical regrouping, odd cyclic towers, negative-D0 pairs and
mutations. These are format/decoder controls, not newly constructed SICs. The fixture suite uses no
production producer or production fixtures. `cpp/checkers/verifier/tests/general_tests.cpp` compares entries with
exhaustive direct-product enumeration and smallest-lift scans, covering d=2,3,5,6,7,9,12 and powers
of two, plus additional CRT cases. `cpp/checkers/verifier/tests/guard_tests.cpp` checks allocation admission, protected
JSON input spans and test-only one-shot libc/FLINT failures. These are specification/conformance
controls, separate from actual SIC proof runs.

<a id="clifford-3-identification-sic-identify"></a>
## Identifying a numerical vector

```
sic-identify FILE --catalogue JSON|TSV [--repo DIR] [--tol T=1e-6] [--entry-error E=2^-50] [--moments K=4] [--unitary-only]
```
Input in the repository WH convention (the Gabor-frame section of `docs/sicx.md` converts Gabor
notation: the time-frequency shift `M_b T_a` equals `D_(a,b)` up to a phase, so a fiducial in that
convention is used as is). An explicit JSON catalogue uses `sic-release-catalogue/1.0`,
generated from the available release tree. The source tree contains the d2 base-case
ray and the exceptional Hoggar object; Hoggar is excluded from WH searches.
Explicit TSV input (`d label order status csv_path`, paths relative to `--repo`)
is supported explicitly for independent reference fixtures. Catalogue
metadata alone does not certify any newly generated ray.

Output: input check, fingerprint `M_1..M_K`, one line per catalogued ray of the same dimension, then
`MATCH label (...) psi = c D_chi U_F phi, F = [[a,b],[c,e]] mod dbar (det +-1), chi = (x, y),
residual, z_deviation` or `NO_MATCH among N catalogued rays`. Exit 0 match, 1 no match or not a SIC
(`max deviation > 1e-8`) or input/runtime error, 2 usage error. A reported element is a numerical
equivalence witness at the stated tolerance.

<a id="clifford-invariant-and-its-status"></a>
### Clifford fingerprint and equivalence-search scope

* **Fingerprint (proved).** `z_p = (a_p/|a_p|)^(2d)` with `a_p = <psi|D_p psi>`, a function of `p
  mod d`; `M_k = sum_{p != 0} Re(z_p^k)/(d^2-1)`. For any extended Clifford `U`, `U D_p U^dagger =
  mu_p D_(Gp)` with `mu_p^(2d) = 1` and `G` symplectic mod d, so `z` is permuted (and conjugated for
  antiunitaries). This is the moment invariant used for certified PEC separation; here it is
  computed in binary64 and used only for display. Equal fingerprints do not prove equivalence.
* **Explicit equivalence (numerical, exhaustive search).** If `psi = c U phi` then
  `z_psi(p) = z_phi(G p)` for all p, with `G = F^(-1) mod d` in SL(2, Z_d) (after replacing `phi` by
  `conj(phi)` for antiunitaries). `G` is fixed by `G e1`, `G e2`, which must lie in the level sets
  of `z_phi` at the values `z_psi(e1)`, `z_psi(e2)`; enumerating those pairs (det 1) and testing all
  d^2 labels is exhaustive over the extended Clifford group **provided `z_tol` exceeds the sum of
  the z errors of both inputs**. `z_error_bound(d, e)` gives an a priori bound in the standard
  floating-point rounding model (not interval arithmetic): for the released data (`e = 2^-50`)
  it is 6.4e-9 at d = 1024, against `z_tol = 1e-6`; observed deviations of accepted elements are
  <= 5e-12. Each surviving `G` is then confirmed on the vectors: the canonical lift `F`, `u = U_F phi`,
  and `chi = argmax |<D_chi u|psi>|` over all `chi` (d FFTs); a match requires
  `1 - |<psi|D_chi U_F phi>| <= 1e-6`. When the ray has a nontrivial stabilizer, several elements
map `phi` to `psi`; the first one found (unitary first) is printed. The bound is derived only in the
comment of `z_error_bound` and assumes the documented entry error (2^-50 for the released CSVs).
A match is a numerical statement about the binary64 points;
  `NO_MATCH` means that no available catalogued ray of that dimension matched within
  these tolerances. Missing catalogue files are reported as unavailable and skipped.
  It does not establish non-equivalence to those unsearched rays or a new orbit.

The Clifford fixture tests identify unitary and antiunitary images of the enrolled numerical
fixtures through d256, using random `F` in ESL(2, Z_dbar), random `chi`, a varying phase and
fixed scale 1.7. The returned element reproduces the image. The recorded same-dimension pair
checks distinguished all 1 + 1 + 1 + 6 + 66 + 66 = 141 pairs at d = 8..256.

<a id="recorded-numerical-evidence"></a>
## Recorded numerical evidence

| dimension | rays | numerical scope |
| --- | ---: | --- |
| 2,4,8,16,32,64 | 12 distinct (15 records with aliases) | complex FP64 error at most 2^-50 in pinned numerical fixtures |
| 128 | 12 | 1,536 coordinates, 66 PEC pairs separated |
| 256 | 12 | 3,072 coordinates, 786,420 WH bounds, 66 PEC pairs separated |
| 512 | 24 | 6 maximal and 18 standard; 15 + 153 within-order and 108 cross-order PEC pairs separated |
| 1024 maximal | 10 | 10,485,750 WH bounds below 2^-100 at 2210 bits; 45 PEC pairs separated |

Recorded field meanings remain distinct: `strict_deviation_bound 2^-100` is a numerical WH bound
over `d^2-1` nonidentity labels; `reference_mode raw-intersection` and reference-intersection counts
indicate overlapping raw reference decodes, not exact-point containment; `PEC_INEQUIVALENT
separating_moment=K` records disjoint K-th invariant-moment enclosures. `basis` and `scope` fields
retain the premises and the bound’s limits. A metadata status string cannot replace a root-bound
proof. At d512 cross-order all 108 pairs separated at moment 1.

Textual format, algorithm and checkpoint identities use `sic-<name>/1.0` or
`sicx-<name>/1.0`. Readers require the identities in the
[identity registry](sicx.md#identity-registry). Fixed-width binary signatures
and record tags retain their specified widths; the header carries the container version.
