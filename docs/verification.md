<a id="verification-verification-and-status-of-the-claims"></a>
# Verification and claim scope

This page states, for every claim about the released data, whether it is proved, certified,
conditional, empirical or conjectural, on what basis, and how to check it. Interpret the numerical
files with the normative [mathematical conventions](sicx.md#mathematical-conventions). A status
attaches to a statement about identified input data, not to a dimension or a file name alone.

<a id="verification-status-words"></a>
## Status words

| word | meaning here |
| --- | --- |
| **proved** | follows from a mathematical proof, or from a certificate whose success implies the statement: exact arithmetic, possibly with certified (outward-rounded) ball arithmetic, but no unverified floating point and no imported premise beyond those named |
| **certified** | established by rigorous ball (interval) arithmetic with outward rounding in the recorded run; a certified *numerical bound* bounds a quantity, it does not show that the quantity is exactly zero |
| **conditional** | rests on a premise that is not proved in this documentation or in a cited source |
| **empirical** | observed in every case checked, used as a rule and checked again on each file, but not proved |
| **conjectural** | rests on a published conjecture |

Byte-level checks (SHA-256 digests, Merkle roots, canonical encoding) establish which data were
saved and that copies agree. They say nothing about the SIC-POVM property.

The [SICX 1.0 format](sicx.md#spec-sicx-10-final-exact-weylheisenberg-sic-fiducial-format) accepts
every integer dimension `2 ≤ d ≤ 2^20`; this is the scope of the format. The producer and the
released data concern the dimensions `d = 2^s`. The reader paths for other dimensions are tested with
synthetic conformance fixtures, which are not SIC-POVM data.

The SICX specification separates two identities. The **exact root** commits EXMT and the canonical
logical coordinate leaves: the embedded tower, the 2-adic shifts, the Galois action, the entry rule
and the representatives. It identifies an exact presentation, not an isomorphism class or a list of
published fiducial vectors. Rechunking, codecs, PROV, transport and finalization preserve it. RAYS
selects words from that presentation and is bound by the **file root**, together with BALL, PREC,
the provenance and the physical directory. The catalogue binds the labels of the fiducial vectors to
their words, the exact root and the file root. A certificate covering all Galois words therefore
covers every fiducial vector selected from a file with that exact root; the root itself is no
proof. SICX 1.0 readers reject every undefined chunk type regardless of flags; extensions require
`required_features` bits in a later version.

Full **container** verification checks the simple nonzero roots of the chosen base field and the
canonical branch rectangles, including exact singleton components. It does not certify
irreducibility, extension degrees, the declared Galois group or the action identities, or the
kernel-fixity premise of the representatives needed for lift independence. Container checks derive
the canonical unit generators from `d`, validate the divisor positions, the word count, rank and
residues, and check each generator-order congruence. A separate algebraic verifier certifies the
field, the group, the actions and exact fixity under every reduction kernel (a generating set of the
kernel suffices; [specification section 3.5](sicx.md#spec-35-entries-and-rays)). The exact
certificates of section 2.8 verify these premises for the files they check. Container success alone
does not establish that the stored balls contain the exact decoded coordinates or that their
binary64 roundings agree; certified finalization and independent decoding are separate results.

The rescaling check relates the stored data to the presentation reconstructed from the recorded
shifts. It does not authenticate recorded producer inputs; that association needs independently
authenticated provenance.

**Root binding.** The catalogue records the exact root and the file root of every released SICX
file. The exact certificates of section 2.8 were computed on the released files of dimensions 4 to
256, and the exact root printed by each successful certificate equals the exact root in the
catalogue. A certificate applies to another file only if its exact root matches.

<a id="verification-1-claims-and-their-status"></a>
## 1. Claims and their status

The release contains 174 Weyl–Heisenberg fiducial vectors in 15 multiplets (dimensions 2 to 2048)
and the Hoggar lines ([data guide](data.md)). The checks below were run on the released files,
except where a row cites other evidence.

| claim | status | scope and qualification |
| --- | --- | --- |
| Exact SIC-POVM property of the fiducial vector in dimension 2 | **proved** | By the exact identities of the radical input in `Q(√3)` (positivity, `A+2B=1`, `4AB=1/3`, `(A−2B)²=1/3`; [radical input](data.md#dimension-two-radical-input)), independently of the [construction theorem](#verification-construction-theorem). The six stabilizer elements in dimension 2 are also verified exactly. |
| Exact SIC-POVM property of the fiducial vectors with `4 ≤ d ≤ 256`: the 9 multiplets `d4-m1`, `d8-m1`, `d8-m2`, `d16-m1`, `d32-m1`, `d64-m1`, `d128-m1`, `d128-m2`, `d256-m1` (35 fiducial vectors; in fact every Galois conjugate of each multiplet) | **proved** | By the Chinese remainder theorem certificate `sic-certify-crt` (section 2.8; [what a proved run establishes](#crt-what-a-proved-run-establishes)), an independent tool with its own SICX reader, run on the released files. It uses no analytic input (no special values of L-functions, no theorem of Radchenko and Wheeler or of Appleby, Flammia and Kopp, no Artin reciprocity) and no premise of the SICX format, and no integrality hypothesis. The statement concerns the vectors defined by each multiplet's exact SICX data, identified by the exact root printed by the certificate, in the embedding fixed by the branch rectangles. |
| Exact SIC-POVM property of the fiducial vectors with `d = 512, 1024, 2048`: the 5 multiplets `d512-m1`, `d512-m2`, `d1024-m1`, `d1024-m2`, `d2048-m1` (138 fiducial vectors) | **proved** (by the construction theorem; not by exact computation) | By the [construction theorem](#verification-construction-theorem) of section 1.1. For the standard order (`d512-m2`, `d1024-m2`, `d2048-m1`) it follows from Radchenko and Wheeler, arXiv:2609.21892v2, Theorems 1 and 7, and Radchenko and Wheeler, Hexagon 2610.00186v1, Proposition 7. For the maximal order (`d512-m1`, `d1024-m1`) the quadratic relation is derived in section 1.1 from the statements of Section 3 of the latter, which hold for every lattice. The stored exact data, including the action records and the entry rule, are identified with the constructed vectors by the certified computations of each run (row below); the empirical row on the entry rule is not a premise of this claim. No exact certificate of the stored data was computed for these multiplets; the certified numerical bound below holds for each of them. |
| Identification of the exact data in each construction run (quadrature enclosures, Newton inclusion tests, recognition of the entries, tower polynomials and action records, root isolation and branch choices) | **certified** | The producer evaluates the special values by the [formulas of section 1.1](#verification-construction-evaluation) with certified ball arithmetic and enforces these acceptance conditions; a successful run identifies its output with the [stored vectors](#verification-construction-stored-data) of the construction. Each accepted Newton inclusion certifies that the selected subsystem has exactly one zero in its box; the analytic values are such a zero, since the conjugate ghost gives an idempotent of rank one (the relation of section 1.1) and its coefficients satisfy `E(x)E(−x) = ⟨x⟩^{−1}` for `x ∉ I` (Hexagon 2610.00186v1, Proposition 1). Recognition accepts an exact coefficient, of an entry, a tower polynomial or an action record, only under the integrality of the trace coordinates, which holds because the special values are algebraic units (ibid., Theorem 1), and under bounds on their conjugates at the embeddings that reverse `K`, where the special values have modulus one (ibid., Proposition 7); the conjugates over `K` of the special values are the special values of the other lattice classes under the Artin automorphisms of ibid., Theorem 2. |
| Overlap bound: `| |⟨ψ|D_p ψ⟩|² − 1/(d+1) | < 2^−129` for all `d² − 1` labels `p ≠ 0` | **certified** (numerical bound) | For each of the 174 fiducial vectors and every vector in its certified enclosure decoded from the exact data (`sic-check-balls`, threshold `2^−100`; the largest certified bound in the release is below `2^−129.5`, in `d1024-m2`). An interval containing zero still permits a small nonzero value: this bound is not an exact-zero certificate. |
| Numerical values in `c<N>.json`: `fp64` and `fp32` are correct roundings and the `hp` balls contain the exact coordinate | **certified** (finalization and export) | The finalizer constructs BALL by outward rounding of its own certified normalized enclosures with the phase fixed, then certifies containment and identical binary64 rounding. The export checks unique binary32 and binary64 rounding from BALL and rounds `hp` outward to contain BALL, hence the exact coordinate (specification sections [5.2](sicx.md#spec-52-ball-one-required-chunk-per-ray) and [9](sicx.md#spec-9-ray-files-sic-fiducial-json10-cjson)). See [numerical levels](data.md#data-22-numerical-levels-and-what-each-guarantees). The SIC-POVM status of the exact data is stated in the rows above. |
| Container integrity of every released SICX file | **certified** (format checks) | Full container verification (`sic-verify --rescaling`) passed for all 15 files: canonical encoding, both directories and roots, all digests, rescaling, branch rectangles, BALL normalization and phase. It is not a statement about the SIC-POVM property. |
| Stored balls contain the exact coordinates, with the stored binary64 values | **certified** (finalization) | For all 15 files the finalizer (`sic-decode --finalize`) decoded every coordinate of every fiducial vector from the exact data and constructed BALL by outward rounding of its own certified enclosures, with certified binary64 agreement. |
| Independent decoding agrees with the stored balls | **certified** for the stated scope | The second reader (`sic-verify-independent --release`) decoded every coordinate of every fiducial vector of the 10 multiplets with `d ≤ 256` and compared it with BALL, and passed the ratio spot check for every fiducial vector of `d512-m1`, `d512-m2` and `d1024-m1`. For `d1024-m2` and `d2048-m1` it refused at its fixed resource limit of `2^24` rescaling weight keys (exit status 3, `RESULT: REFUSED: rescaling weight-table resource limit`), so these two files have no independent decoding result. |
| Pairwise inequivalence of the fiducial vectors under the extended Clifford group, within each dimension | **certified** | Every same-dimension pair was compared: 1 pair each in dimensions 8, 16 and 32, 6 in dimension 64, 66 each in dimensions 128 and 256, 276 in dimension 512, 1,225 in dimension 1024 and 2,016 in dimension 2048 (3,658 pairs). Each pair is separated by disjoint certified enclosures of a moment of `z_p = (a_p/|a_p|)^(2d)` ([fingerprint](#clifford-invariant-and-its-status)), which is invariant under nonzero scalars and the projective extended Clifford group, unitary and antiunitary elements included. The comparison program used for the release is not part of this repository. Separation uses no SIC-POVM premise; that the enclosures contain SIC-POVM fiducial vectors is subject to the rows above. |
| Symmetry data (`stabilizers.txt`): certified bound on the stabilizer residual of each listed element | **certified** (numerical bound); **proved** in dimension 2 | `sic-certify-stabilizer` encloses the residual `1 − |⟨φ|Uφ⟩|²/⟨φ|φ⟩²` with 384-bit balls for every non-identity element ([table format](data.md#stab)); every bound in the release is at most `1.9·10^−37`, below the threshold `2^−100`. A small residual bound is not an exact proof that `U` fixes the ray of `φ`. The elements in dimension 2 are verified exactly with FLINT algebraic numbers (`qqbar`). |
| Symmetry data: the tables list the whole stabilizer | **empirical** (numerical) | Exhaustive binary64 search, exhaustive under an a priori floating-point error bound in the standard rounding model (not interval arithmetic). |
| Agreement of the SICX entry rule with the producer coordinates, and pure Galois records of the fiducial vectors | **empirical** | Compared on every entry line of the packed power-of-two exports in dimensions 4 to 1024; the producer checks the representative indices of every file it writes ([specification section 1](sicx.md#spec-1-status-of-the-mathematical-content-premises)). Lift independence is proved assuming a genuine unit-group action and kernel fixity of the representatives, which the exact certificates verify for the files they check. For `d ≥ 512` this row is not a premise: [section 1.1](#verification-construction-stored-data) derives the entry rule and kernel fixity from the Artin action. |
| The count of 174 extended Clifford orbits for `d = 2^s ≤ 2048`, and the correspondence of real quadratic orders and multiplets | **conjectural** | Kopp and Lagarias, "SIC-POVMs and orders of real quadratic fields" (2024), Conjecture 1.2, with the base case `d = 2`. The release contains 174 pairwise inequivalent fiducial vectors, the number the conjecture predicts; pairwise inequivalence does not show that the list of orbits is complete. |
| SIC-POVM property of the Hoggar lines (`data/d8/hoggar/`, three-qubit Pauli group, not Weyl–Heisenberg covariant) | **proved** | Exact integer arithmetic over `Z[i]` ([Hoggar lines](data.md#data-8-the-hoggar-lines-datad8hoggar)). |
| Which fiducial vectors are new | not claimed | "Not found in the literature" would be an inference from a search that was not exhaustive. |

<a id="verification-construction-theorem"></a>
### 1.1 The construction theorem for `d ≥ 512`

The multiplets with `d ≥ 512` have no exact certificate. Their exact SIC-POVM property rests on the
construction below and on the certified computations of each construction run (section 1). The
same producer constructed the multiplets with `4 ≤ d ≤ 256`, whose exact certificates
(section 2.8) use neither this construction nor the results cited here. The sources are:

- RW-I: D. Radchenko and C. Wheeler, "Real quadratic fields and finite quantum dilogarithms I",
  arXiv:2609.21892v2;
- RW-S: D. Radchenko and C. Wheeler, "Stark units for real quadratic fields and reciprocity laws",
  Hexagon 2610.00186v1, <https://hexagonmath.org/2610.00186>;
- AFK25: M. Appleby, S. T. Flammia and G. S. Kopp, "A constructive approach to Zauner's conjecture
  via the Stark conjectures", arXiv:2501.03970v2;
- AFK26: M. Appleby, S. T. Flammia and G. S. Kopp, "The twisted convolution identity and ghost
  r-SICs from finite quantum dilogarithms", arXiv:2609.39192v1.

**Data.** Let `d = 2^s ≥ 4`, let `K = Q(√D0)` be the
[base field](sicx.md#spec-2-fixed-conventions-never-stored), and write `(d − 3)(d + 1) = f²·D0`
with an integer `f ≥ 1`, which is odd. Put `λ_d = (d − 1 + f√D0)/2`, a totally positive unit of
`K` with `λ_d + λ_d^{−1} = d − 1`, `r_d = √λ_d > 0` and `ε = λ_d³`. A multiplet whose order is
listed as standard uses the lattice `I = Z[λ_d]` and `φ = 1`; one listed as maximal uses the ring
of integers `I = O_K` and `φ = f`. In the release `f = 3` for `d = 512` and `f = 5` for
`d = 1024`; for `d = 2048`, `f = 1`, the two orders coincide and only the standard multiplet
exists.

**Special values.** For `x ∈ K` with `(ε − 1)x ∈ I`, `E(x) = E_{I,ε}(x)` is the value of RW-S,
Definition 1, which is the function `F_γ` at the end of RW-I, Section 4.3. Choose a basis
`(β1, β2)` of `I` with `β1β2' − β1'β2 > 0` and `β2` totally positive (the prime denotes the
conjugate in `K`), let `γ = [[γ11, γ12], [γ21, γ22]] ∈ SL(2, Z)` be the matrix of multiplication by `ε` in
this basis (RW-S, Section 3) and put `τ = β1/β2`. Then `E(x) = μ_γ Φ_{γ,u,0}(x/β2; τ)` with integers
`u, v` such that `(ε^{−1} − 1)x = uβ1 + vβ2`, and `E(x) = √ε > 0` for `x ∈ I`. Here `Φ_{γ,m,n}` is
Faddeev's modular quantum dilogarithm (RW-I, Section 2.2) and `μ_γ = η(γz)/(η(z)√(γ21·z + γ22))` is
the multiplier of the Dedekind eta function `η`. The value does not depend on the basis (RW-S,
Appendix B). We use the Gaussian `⟨x⟩`, the pairing `⟨x; y⟩` and the alternating form `ω_I` of
RW-S, Section 3, on the group `G = (ε − 1)^{−1}I/I` of order `|N(ε − 1)| = d²(d − 3)`.

**Ghost matrix.** With `e(t) = exp(2πit)`, the shift `X` and clock `Z` of the
[conventions](sicx.md#conventions-2-shift-and-clock), and scalars standing for multiples of the
identity matrix,

```text
P = (1/d) Σ_{i,j=0}^{d−1} E((i·λ_d + j)/d) · (−1)^(i+j) · e(φ(i² + j²)/(2d)) · Z^(φi) X^(−j),
Q = (r_d − P)/√(d+1).
```

**Theorem.** For both lattices, `(P − r_d)(P + r_d^{−1}) = 0` and `tr P = r_d³`, so `Q` is an
idempotent of rank one and trace one. Let `L` be a number field that contains `K`, `r_d`,
`e(1/(2d))` and the values `E((iλ_d + j)/d)`, and let `σ: L → C` be an embedding with
`σ(√D0) = −√D0`. Then `Π = σ(Q)` is a Hermitian projector of rank one with
`|tr(Π D_p)|² = 1/(d+1)` for every `p ≢ 0 (mod d)`. Hence every nonzero column `Π e_k` spans the
image of `Π`, and `Π e_k/‖Π e_k‖` is a Weyl–Heisenberg SIC-POVM fiducial vector.

**Trace.** Only the identity matrix has a nonzero trace among the `Z^{φi} X^{−j}`, and the argument
`(iλ_d + j)/d` lies in `I` only for `i ≡ j ≡ 0 (mod d)` because `f` is odd, so
`tr P = E(0) = λ_d^{3/2} = r_d³`.

**Quadratic relation, standard order.** For `φ = 1` the relation is RW-I, Theorem 7, with `M = d`
and its `ε` equal to `λ_d`; its operators are `q = Z` and `p = X^{−1}`.

**Quadratic relation, maximal order.** For `φ = f` the proof of RW-I, Theorem 7, carries over to
`I = O_K` when the averaging over a cyclic group in that proof is replaced by a sum over the whole
annihilator of `H = d^{−1}O_K/O_K`. Every statement of RW-S, Section 3, used here holds for every
lattice and every period.

1. With the oriented bases `(λ_d, 1)` of `Z[λ_d]` and `(τ_0, 1)` of `O_K`, `λ_d = fτ_0 + c` for an
   integer `c`, so `ω_{O_K} = f·ω_{Z[λ_d]}`. Since the index `[O_K : Z[λ_d]] = f` is odd,
   `Z[λ_d] ∩ 2O_K = 2Z[λ_d]`, so the signs `s_{O_K}` and `s_{Z[λ_d]}` of the Gaussian agree on
   `Z[λ_d]`.
2. Because `f` is odd, the labels `(kλ_d + l)/d`, `k, l` modulo `d`, run once through `H`, and
   `(ε − 1)u ∈ Z[λ_d]` for `u ∈ H`, since `ε − 1 = dλ_d(λ_d − 1)`. By (1), the Gaussian of `O_K`
   on `H` and its pairing are the `f`-th powers of those of `Z[λ_d]` computed in RW-I, Section 4.4:
   `⟨(kλ_d + l)/d⟩ = e(f(k² − kl + l²)/d)` and `⟨w; (iλ_d + j)/d⟩ = e(f(a_1 j − a_2 i)/d)` for
   `w = (a_1λ_d + a_2)/(λ_d^{−3} − 1)`.
3. Since `X^{−j} Z^{φk} = e(φjk/d) Z^{φk} X^{−j}` and `r_d − r_d^{−1} = √(d − 3)`, the
   coefficient of `Z^{fr} X^{−s}` in `(P − r_d)(P + r_d^{−1})` vanishes exactly when, for
   `v = (rλ_d + s)/d` and `w = ((r − s)λ_d + r)/(λ_d^{−3} − 1)`,
   `(1/d) Σ_{u∈H} E(u)⟨u⟩E(v − u)⟨w; u⟩ = √(d − 3)·E(v) + d·δ(v)`, as in RW-I's proof.
4. The annihilator `H^⊥ = (λ_d − 1)^{−1}O_K/O_K ⊂ G` has `|N(λ_d − 1)| = d − 3 = |G/H|`
   elements. For `h = ξ/(λ_d − 1)`, `ξ ∈ O_K`, `(1 − ε)h = −dλ_d ξ`, so
   `⟨h; u⟩ = e(−d·ω_{O_K}(λ_d ξ, u))`, which is 1 for all `ξ` exactly when `du ∈ O_K`, because
   `λ_d O_K = O_K` and `ω_{O_K}` is unimodular on `O_K`. The pairing is nondegenerate on `G`
   (RW-S, Section 3), so `h ↦ ⟨h; ·⟩` identifies `H^⊥` with the characters of `G/H`, and
   `Σ_{h∈H^⊥} ⟨h; u⟩ = (d − 3)·1_H(u)` for `u ∈ G`. The left side of (3) is therefore
   `(1/(d(d − 3))) Σ_{h∈H^⊥} Σ_{u∈G} E(u)⟨u⟩E(v − u)⟨w + h; u⟩`.
5. Let `v ∉ O_K` and `y = w + h`. The product form of the pentagon relation (RW-S, Section 3,
   which is RW-I, eq. (36)) gives `Σ_{u∈G} E(u)⟨u⟩E(v − u)⟨y; u⟩ = d√(d − 3)·⟨v + y⟩E(−v − y)E(v)E(y)`.
   RW-I's computation gives `(λ_d − 1)w ≡ v` modulo `Z[λ_d] ⊂ O_K`, and `(λ_d − 1)h ∈ O_K`, so
   `λ_d y ≡ y + v` and `E(y) = E(λ_d y) = E(v + y)` (RW-S, Proposition 2, for the period `λ_d`
   and `r = 3`). If `v + y ∈ O_K`, then `λ_d v ≡ (λ_d − 1)(v + y) ∈ O_K` and `v ∈ O_K`, which is excluded; so the
   reflection formula `E(z)E(−z) = ⟨z⟩^{−1}` for `z ∉ O_K` (RW-S, Proposition 1) makes every
   term equal to `d√(d − 3)·E(v)`, and the left side equals `√(d − 3)·E(v)`.
6. For `v = 0`, `w = 0` and the reflection formula give `(1/d)(ε + d² − 1)` on the left, and the
   identity reduces to `(ε − 1)/d = λ_d² − λ_d = √(d − 3)·λ_d^{3/2}`, which holds because
   `(λ_d − 1)² = (d − 3)λ_d` and `λ_d > 1`.

**Projector.** The values are algebraic (RW-I, Theorem 1), so `L` exists. Since
`d·r_d − r_d³ = r_d + r_d^{−1} = √(d+1)`, the coefficient of the identity matrix in `Q` is `1/d`. By
RW-S, Proposition 7, `|σ(E(x))| = 1` for every `x ∉ I`, for both lattices. The embedding `σ` maps
each matrix `Z^a X^b` to some `Z^{a'} X^b`, and `(i, j) ↦ (φi, −j)` is a bijection modulo `d`
because `φ` is odd, so the other `d² − 1` coefficients of `Π` in the trace-orthogonal basis
`Z^a X^b` have modulus `1/(d√(d+1))`. Then `‖Π‖_F² = 1/d + (d² − 1)/(d(d + 1)) = 1`. A rank-one
idempotent `uv*` with `v*u = 1` has Frobenius norm `‖u‖‖v‖ ≥ 1`, with equality only for
`v = u/‖u‖²`; so `Π` is a Hermitian projector, and its coefficients give the overlaps. RW-S draws
this conclusion for the ghost of RW-I, Theorem 7, in the paragraph after Proposition 7, by the
argument of AFK25, Theorem 1.46. AFK26, Theorem 1.2, proves the twisted convolution identity, the
counterpart of the quadratic relation, for every admissible tuple of AFK25, including the tuple of
`O_K`, in the normalization of AFK25.

<a id="verification-construction-evaluation"></a>
**Evaluation of the special values.** The producer evaluates `E` at all lattice classes by
formulas printed in RW-I. With `b = √t` for `t > 1`,

```text
Φ_S(z; t) = exp ∫_{R + ih} exp(2νw) / (4 sinh(bw) sinh(w/b) w) dw,   ν = (z + (1 − t)/2)/b,
```

for `−1 < z < t` and `0 < h < π·min(b, 1/b)`. This is Faddeev's integral of RW-I, Section 2.1,
under the substitution `Φ_{S,0,0}(z; t) = φ_b(iz/b − ib/2 + i/(2b))` given there; the shift of
the contour `R + i0` to the height `h` crosses no pole. Other real `z` are reached by
`Φ_S(z + 1; t) = Φ_S(z; t)/(1 − e((z + 1)/t))` and by the reflection formula of the same section.
A lattice class has a reduced continued-fraction cycle `(b_1, …, b_n)`, all `b_l ≥ 2`, with reduced
generators `τ_1, …, τ_n`; the cycle is repeated until the product `γ` of the matrices
`[[b_l, −1], [1, 0]]` is the matrix of multiplication by `ε`. For the argument
`x = (x_1 τ_1 + y_1)/d`, with integers `1 ≤ x_1 ≤ d` and `0 ≤ y_1 < d`, the labels follow
`(x_{l+1}, y_{l+1}) = (b_l x_l + y_l, −x_l)`, reduced to `1 ≤ x_{l+1} ≤ d`, `0 ≤ y_{l+1} < d`, and
`E(x)` is `μ_γ` times the product of the values `Φ_S((x_l τ_l + y_l)/d − 1; τ_l)` along the
repeated cycle: RW-I, Proposition 2(ii), with the integers `m_j` chosen as in the proof of
Proposition 5 and the periodicity relation of Section 2.2. The producer uses
`μ_γ = exp(πi Σ_l (b_l − 3)/12)`, summed over the repeated cycle: `η(z + 1) = e^{πi/12} η(z)` and
`η(−1/z) = √(−iz) η(z)` give `exp(πi(b − 3)/12)` for one factor `[[b, −1], [1, 0]]`, and since every
partial product has a positive lower-left entry, the principal square roots in the definition of
`μ` compose and the multipliers of the factors multiply. The quadrature carries explicit error
bounds in ball arithmetic. The producer evaluates no instance of the integral identities of RW-I,
Theorem 3, and no distribution relation.

<a id="verification-construction-stored-data"></a>
**What the stored data are.** For a run with lattice `I`, the producer computes the entries
`κ_m = d√(d+1)·Q_{m,0}` of column 0 of `Q`, at all conjugates of `Q` over `K`, at the
representative positions `m` of the [entry rule](sicx.md#spec-35-entries-and-rays): one position
for each divisor of `d`. These entries are sums of special values times roots of unity, plus
`√(d+1)` for `m = 0`, hence algebraic integers (RW-S, Theorem 1).

- *Galois group.* The run generates its group `G'` by the Artin symbols of totally positive
  principal ideals and of split prime ideals prime to the conductor. By RW-S, Theorem 2, each acts
  on the special values by the corresponding homothety or change of lattice; it acts on
  `e(1/(2d))` by the power `N(𝔞)` and on `√(d+1)` by the Kronecker symbol of `N(𝔞)`.
- *Tower and action records.* The stored data are the exact trace coordinates of the entries, the
  tower generators with their polynomials, and the action records, which give the images of the
  tower generators under the Smith generators of `G'`. All are recognized from certified
  enclosures computed from the special values of all lattice classes, under the bounds of the
  identification row of section 1. Each tower generator is certified to differ from one of its
  conjugates. Before it builds the tower, the run requires `|G'| = h·d²/2`, where `h` is the
  narrow class number of the order `O` of `I` (`Z[λ_d]` or `O_K`), and requires the characters of
  `G'` certified to occur in the entries to generate its dual group. The number `h·d²/2` is the
  degree over `K` of the narrow ray class field of `O` modulo `2dO`: 2 is inert in `K`, and `λ_d`
  generates the totally positive units of `O` (for `O_K` with `d = 512, 1024`, no unit of `O_K`
  has a smaller coefficient of `√D0`) and has order 6 modulo `2dO`. By RW-S, Theorem 2, that
  field contains the special values at the labels of `H` divided by `√ε`; it also contains
  `e(1/(2d))` and `√(d+1)`. The certified support and the certified distinctness make `G'` act
  faithfully, so `G'` is the Galois group over `K` of the field `F` of the entries. Every
  `G'`-invariant quantity lies in `K`, which recognition requires, and each recorded polynomial
  (the product of `X − v` over the conjugates of a tower generator) is a minimal polynomial.
- *Entry rule.* The unit words of the entry rule are the Artin symbols `σ_(a)` of the principal
  ideals `(a)`, for the odd residues modulo `2d` represented by positive integers `a` prime to the
  discriminant of the order (`(d − 3)(d + 1)` for `Z[λ_d]`, `D0` for `O_K`). This contains the
  condition "prime to `cd`" of RW-S, Theorem 2, with `c = f` for `Z[λ_d]` and `c = 1` for `O_K`,
  and the conductor of `Q(√(d+1))` divides `D0`. Then `σ_(a)` acts on `e(1/(2d))` by `a² = N((a))`, and its
  restriction to `Q(√(d+1))` is the Artin symbol of `a²` over `Q`, which is trivial, so it fixes
  `√(d+1)`, `√(d−3)`, `r_d` and `√ε = r_d³`. With RW-S, Theorem 2, and the homothety relation
  `E_{cI}(cx) = E_I(x)` of RW-S, Section 3, it maps `E(x)` to `E(ax)`, hence `P` to
  `U_a P U_a^{−1}` and `Q` to `U_a Q U_a^{−1}`, where `(U_a v)(m) = v(am)`. Therefore
  `σ_(a)(κ_m) = κ_{am}`. This gives the kernel fixity of the representatives and the agreement of
  the entry rule with column 0 for every Galois word; the empirical row on the entry rule in
  section 1 is not a premise of the claims for `d ≥ 512`.

A Galois word `c` of the file thus gives the vector with coordinates
`ι(σ_c(κ_m)) = d·σ(√(d+1))·σ(Q)_{m,0}`, where `ι` is the embedding fixed by the branch rectangles,
which maps `√D0` to `−√D0`, and `σ = ι ∘ σ_c`. The column `Π e_0` of `Π = σ(Q)` is nonzero, since
the certified enclosure of its anchor coordinate excludes zero, and the fiducial vector of the word
is `Π e_0/‖Π e_0‖` up to a phase, to which the theorem applies.

**Machine-checked proofs.** Two public Lean 4 formalizations, checked by the Lean kernel with only
the standard axioms `propext`, `Classical.choice` and `Quot.sound`, prove that SIC-POVMs exist in
every dimension; their constructions are Weyl–Heisenberg covariant. They are M. Ozols, "SIC
existence in Lean", <https://github.com/marozols/SIC-existence>, commit 91e3a43 (theorem
`SIC.exists_SIC` in `MainResults.lean`), and Y. Yang, "SIC-POVMs exist in every dimension (Open
Quantum Problem 23): a Lean 4 formalization", <https://github.com/Hertaspace/sic-povm-lean>,
release v1.1 (theorem `SICLean.FinalAssembly.zaunerExistence`). Both build on RW-I, RW-S and
AFK25 and check the imported identities: the first proves the identity of RW-I, Theorem 7, in the
principal case (`principalRealShiftTCC` in `SICs/Principal/Dilogarithm/TwistedConvolution.lean`,
in the finite form of `SICs/Principal/Construction/TCCFiniteForm.lean`) and RW-S, Proposition 7,
for every lattice and every period (`pseudolatticeDilog_norm_map_eq_one` in
`SICs/Dilogarithm/Reciprocity/UnitaryConjugates.lean`); the second proves the Fourier and
five-term identities of RW-I, Theorem 2 (`attracting_fourier` and `attracting_pentagon` in
`SICLean/L4/G2AttractingIdentities.lean`, with the hypothesis `G1RealConclusion`, proved in
`SICLean/L4/G1RealConclusion.lean`). The first obtains the fiducial projector of `SIC.exists_SIC`
as the image, under an automorphism of `C` that maps `√D0` to `−√D0`, of the candidate ghost of
AFK25, Definition 1.43, for the form `x² + (1 − d)xy + y²` of the standard order, and it also
proves, through AFK26, that the candidate of every admissible tuple of AFK25
with a positive leading coefficient gives a fiducial projector (`SIC.AdmissibleTuple.exists_liveFiducial`
in `SICs/Construction.lean`); the tuples include forms of discriminant `D0`, so this covers the
maximal-order construction in the normalization of AFK25. Its theorem `SIC.exists_RSIC` with
`r = 1` uses the form of conductor one, so it is a kernel-checked existence result through the
maximal order. Neither formalization states the
theorem above for the matrix `P`, the evaluation of the special values or the stored data.

<a id="verification-2-how-to-check"></a>
## 2. How to check

The commands below use `build/`, configured from the repository root with
`cmake -S cpp -B build -DSIC_ENABLE_FLINT=ON`. Tool names and options follow the
[command reference](library.md#command-argument-contract).

<a id="verification-21-hashes-and-merkle-roots-integrity"></a>
### 2.1 Digests and Merkle roots (integrity)

- `(cd data && sha256sum -c sha256sums.txt)` checks every file of the numerical data tree.
  `catalogue.json` records the digests of all listed files; `sha256sums.txt` additionally covers the
  catalogue and excludes itself.
- `sic-container fetch d<d>/m<N> --data-root data` (or the `exact-data` build target) verifies each
  release part, the whole-file SHA-256 of the reassembled SICX file and its container checks, and
  fails closed on any mismatch ([exact data downloads](data.md#transport-getting-the-exact-data)).
- `sic-verify FILE` checks the container: canonical encoding, LDIR and PDIR, both
  Merkle roots, chunk digests, canonical `BALL` records, the anchor phase, that
  the enclosure of `Σ|w_j|²` contains 1, and `accuracy_bits`. It does not
  establish the whole-file SHA-256.
  Full verification also checks canonical leaf boundaries, rescaling and branch
  rectangles.

For authenticated partial access, read the file header and footer and the complete LDIR and PDIR
chunks, recompute both roots, then authenticate EXMT and derive the logical layout. Each ordinary
chunk header is checked when its chunk is read; directory validation already checks its declared
geometry and the finalization skeleton. Checks that depend on RAYS and on payload semantics wait
until the required payloads are read. A trusted exact root authenticates exact leaves; a trusted
file root with its LDIR binding verified also makes the recomputed exact root trusted transitively.
A trusted whole-file SHA-256 makes both roots trusted only after all bytes of the same snapshot have
been hashed. Finalization needs file-level trust. Hash each consumed encoded payload and all its
logical leaves before use ([specification section 6.2](sicx.md#spec-62-partial-verification)).
Report direct and transitive trust, unread headers and payloads and unchecked invariants explicitly.
A root in the same untrusted footer is self-consistency only. Exact-only files have zero fiducial
vectors and remain valid, with distinct exact and file roots.

<a id="verification-22-decoding-and-comparing-with-the-stored-balls"></a>
### 2.2 Decoding and comparing with the stored balls

`sic-decode` decodes coordinates of chosen fiducial vectors from the exact data with certified
balls, normalizes them with the anchor rule, and requires overlap with every stored ball and
identical binary64 values. This consistency result does not by itself prove containment.
Finalization (`sic-decode --finalize`) writes BALL from the decoder's own certified normalized
enclosure, rounded outward to the stored width, certifies enclosure containment and checks the
binary64 rounding of all coordinates before the file is published. External balls are never an
input.

A cheaper portable check compares one ratio `w_j / w_k` of decoded and stored values
([specification section 5.2a](sicx.md#spec-52a-portable-spot-check-of-ball-against-the-exact-data)).
It is optional, including in full container mode; a performed check that proves inconsistency must
reject the file. It cannot replace the finalizer's all-coordinate checks.

<a id="verification-23-sic-check"></a>
### 2.3 SIC-POVM overlap check

- **Diagnostic (binary64):** `sic-verify FILE --sic-check`, `sic-overlaps FILE`, or the NumPy recipe
  in the [data guide](data.md#data-5-checking-a-fiducial-yourself). These are floating-point
  diagnostics.
- **Certified bound:** the library component `sic::bounds` (`bound_wh_overlap_deviations`) returns
  rigorous enclosures of the squared norm and of the maximum overlap deviation over a ball vector.
  A small upper bound remains a numerical statement.

`sic-check-balls VECTOR.normalized.balls --bits 100` checks a certified normalized ball vector and
fails unless the rigorous deviation bound is below `2^−100`. This is a numerical bound; the exact
SIC-POVM status is stated separately (section 1).

<a id="verification-24-independent-second-reader"></a>
### 2.4 Independent second reader

`cpp/checkers/verifier` is implemented independently from the specification, without production SICX
code. It implements the EXMT, LDIR and PDIR layout and the 112-byte footer, with full and scoped
partial verification, trusted-root binding and certified decoding. See its
[command line](tools.md#sic-verify-independent) and [implementation
scope](#reader-independent-sicx-10-reader). `--decode full` compares every coordinate of every
requested fiducial vector with BALL; `--decode ratio` is a spot check and not an all-coordinate
comparison. The release results are stated in section 1.

<a id="verification-25-exact-checks-with-parigp-small-d"></a>
### 2.5 Exact checks with PARI/GP (small `d`)

After generating `exact.json` (`exact-data` target,
[exact data downloads](data.md#transport-getting-the-exact-data)), the PARI/GP reference script,
independent of the C++ code, rebuilds the field and every coordinate and verifies exactly that
`|⟨ψ|D_p ψ⟩|² = 1/(d+1)` for every `p ≠ 0`:

```
SIC_EXACT_JSON=build/data/d8/m1/exact.json gp -q -s 2G cpp/checkers/gp/sicx_exact_verify.gp < /dev/null
```

`exact.json` is practical only for `d ≤ 16`; the complete check in dimension 16 is the opt-in test
`sic_test_sicx_exact_gp_d16` (`SIC_SICX_GP_D16=ON`), and one autocorrelation shift takes several
minutes there. The script's positivity check and its identification of complex conjugation use a
floating-point embedding and are not certified by the script.

<a id="verification-26-inequivalence-certificates"></a>
### 2.6 Inequivalence certificates

The release pairs were compared with certified enclosures of the moments of the fingerprint `z_p`
(section 1). A disjoint pair of enclosures proves inequivalence of any vectors inside the two
vector enclosures; an overlapping pair is undetermined. `sic-identify` computes the same invariant in
binary64 and searches explicitly for an equivalence ([identification](#clifford-3-identification-sic-identify)).

<a id="verification-27-symmetry-tables"></a>
### 2.7 Symmetry tables

`sic-stabilizer --dim D --catalogue data/catalogue.json` recomputes the symmetry table of a dimension
(binary64). `sic-certify-stabilizer STABFILE --catalogue data/catalogue.json --dim D` certifies each
element with ball arithmetic, and with `--d2-exact data/d2/d2.sicrad` verifies the elements in
dimension 2 exactly ([table format](data.md#stab)).

<a id="verification-28-unconditional-exactness-certificate-crt"></a>
### 2.8 Exact certificate by the Chinese remainder theorem method

`sic-certify-crt FILE.sicx --threads T` (source `cpp/checkers/crt`, built with
`-DSIC_ENABLE_FLINT=ON`) proves that every Galois word of the multiplet in `FILE.sicx`, hence every
fiducial vector, is an exact Weyl–Heisenberg SIC-POVM fiducial vector. A run ending in
`RESULT: EXACT SIC for every Galois word … [PROVED]` (exit code 0) means: the tower algebra is a
field `L`; the action records are automorphisms and `L/K` is Galois with the stored group; the
printed word `c*` is complex conjugation for the embedding fixed by the branch rectangles; and every
quartic residual `R_{a,k}` of every fiducial vector is exactly zero in `L`, the vector being nonzero.
Each identity is checked at all degree-one points modulo distinct completely split primes
`p > 2^61`, and a norm lemma with a **proved** conjugate bound and a **proved** denominator bound
turns these checks into exact equalities. Here **proved** means a mathematical proof whose
computational steps are exact modular arithmetic and certified ball arithmetic, with no imported
premise. Only FLINT with Arb, the compiler and the tool are trusted. The tool has its own SICX reader
(written from the specification) and does not link the production code.

To run it with the exact tools installed and on `PATH` ([installation](install.md)), for example in
dimension 16 (well under a second):

```
sic-container fetch d16/m1 --data-root data   # writes build/data/d16/m1/multiplet.sicx
sic-certify-crt build/data/d16/m1/multiplet.sicx --threads 16
```

Its own tests, including negative controls, run with `ctest --test-dir BUILD -L crt` in a
FLINT-enabled build with tests.

The certified data are identified by the printed `exact root`. The certificates of the release cover
the nine multiplets with `4 ≤ d ≤ 256` and every Galois word of each:

| multiplet | exact root (equal to the catalogue) | Galois words `[L:K]` | fiducial vectors |
| --- | --- | ---: | ---: |
| `d4-m1` | `616f89361d9a0646ddcd169e7a59a63cdcc7adead9990ca8882ae424f5b846a1` | 8 | 1 |
| `d8-m1` | `24a5215393dc338c412c2ce9645e93d3985fea868b624e85e63c1a4db7f9e463` | 16 | 1 |
| `d8-m2` | `71e665d53173ce0a692dd13d5dc274a39741c7360895cd23dd28aad8f848674a` | 64 | 1 |
| `d16-m1` | `4512ae607c93f442b785ff1aa90c557f2661ad67c68a720a88bf65a393271359` | 512 | 2 |
| `d32-m1` | `9592d2782badfaad160450456868322bf0454c8d7523d21208f0600c33e5d053` | 2,048 | 2 |
| `d64-m1` | `4dc3c9a2ffd642ca3403817151b33758c4926f2b12212daea02a537dcb52c162` | 16,384 | 4 |
| `d128-m1` | `39d53a036a3715097a19b5a753a5a7267934a27578075130e4f5441489471cda` | 32,768 | 2 |
| `d128-m2` | `5d2d6ace4b175889306f6e9d2930c3dde35a95e2490ef87a6b22aefbea1972a9` | 163,840 | 10 |
| `d256-m1` | `bb20c2bcaaffc0818709be72be713c6371be9a3f8f28ecbc61099c9348846bdd` | 786,432 | 12 |

The measured cost is given in [cost of the certificates](#crt-measured-cost).

<a id="verification-29-high-precision-exactness-certificate-norm-argument-second-method"></a>
### 2.9 Exact certificate by the norm method (second method)

`sic-certify-norm FILE.sicx --threads T` is a second, independent method with its own SICX reader
and exact tower arithmetic in `cpp/checkers/norm`, written from `docs/sicx.md`. It links only FLINT
3 (including Arb) and the platform thread library. The
`sic_test_support_independence_sic_certify_norm` CTest checks its include and link dependencies and
checks for copied production code. Its reader tests also include synthetic conformance fixtures,
which are not SIC-POVM data. The quartic residuals are algebraic integers after an exact power-basis
scaling `q`, all their conjugates are bounded by `2^m`, and a ball computation shows
`|R_{a,k}| < 2^{−(2[L:K]−1)m}` at the branch embedding. Its working precision grows like `[L:K]·h`
bits and its exact power-basis preprocessing like `[L:K]²`, so it is practical for `d ≤ 16`. Status
of a PASS: **proved** (certified computation of an exact statement).

Build with `cmake -S cpp -B cpp/build -DSIC_ENABLE_FLINT=ON`, then `cmake --build cpp/build --target
sic-certify-norm`; the binary is `cpp/build/sic-certify-norm`. Use
`-DCMAKE_PREFIX_PATH=FLINT_INSTALL` if FLINT is installed outside the system search path.
See the complete [norm checker options](tools.md#sic-certify-norm) for syntax and defaults.
`--estimate` performs exact preprocessing and a precision estimate without a SIC-POVM verdict;
`--leaf-estimate` gives a heuristic feasibility estimate without exact preprocessing or a SIC-POVM
verdict. Exit codes are 0 for all selected rays certified (or a completed estimate), 1 for an
inconclusive certificate or BALL mismatch, 2 for usage errors, and 3 for a refused input or
computation. Numeric command-line values must be full-string unsigned decimals with checked overflow;
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
## 3. Scope of the claims

- In dimensions 512, 1024 and 2048 the exact SIC-POVM property rests on the
  [construction theorem](#verification-construction-theorem) and on the certified computations of
  each run; the release contains no exact certificate of the stored data for these multiplets.
  Their overlap bound is certified.
- The second reader has no result for `d1024-m2` and `d2048-m1` (resource refusal, section 1).
- The symmetry tables are certified numerical bounds; they are exact only in dimension 2, and their
  completeness is empirical.
- The classification is not proved: the count 174 and the correspondence of orders and multiplets
  are conjectural, and pairwise inequivalence does not show that the list of orbits is complete.
- A floating-point overlap deviation, a digest or a root is never an exact SIC-POVM proof.

## Independent checker contracts

The checkers have separate parsers, arithmetic and readers. Their source/build paths are
[Chinese remainder theorem certificate](../cpp/checkers/crt/CMakeLists.txt), [norm](../cpp/checkers/norm/CMakeLists.txt),
[reader](../cpp/checkers/verifier/CMakeLists.txt),
[stabilizer](../cpp/checkers/stabilizer/CMakeLists.txt) and PARI/GP
([SICX](../cpp/checkers/gp/sicx_exact_verify.gp), [Hoggar](../cpp/checkers/gp/hoggar_verify.gp)).
The root FLINT build enrolls their tests; CMake and CTest enforce include, link and copied-source
independence. Help texts and exit codes are in [command-line tools](tools.md).

<a id="crt-sic-certify-crt-unconditional-crt-exactness-certificate"></a>
## Chinese remainder theorem certificate

`sic-certify-crt FILE.sicx` proves, with no unproved hypothesis, that every Galois word of the
multiplet stored in a SICX 1.0 file is an exact Weyl–Heisenberg SIC-POVM fiducial vector. Every
published fiducial vector of a multiplet is a Galois word of its file, so the proof covers every
fiducial vector. Nothing analytic is used: no special values of L-functions, no theorem of
Radchenko and Wheeler or of Appleby, Flammia and Kopp, and no Artin reciprocity. The SICX premises
of the specification (`docs/sicx.md` section 1) are not used either; each one is either verified or
not needed: genuine automorphisms, commuting actions, integrality, the entry rule and pure Galois
records.

See the [measured cost](#crt-measured-cost) and the [certificates of the
release](#verification-28-unconditional-exactness-certificate-crt).

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

Together these give: for every ray `v = ι(x)`, `v/‖v‖` is an exact Weyl–Heisenberg SIC-POVM fiducial vector. "Proved" means
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
  integer by an exact integer characteristic polynomial, reconstructed by the Chinese remainder theorem and verified against
  the proved denominator bound. The shift `J_j` is taken from authenticated EXMT
  `generator_rescaling` only as a candidate integral-generator hint. If any recorded shift
  exceeds 64, all shift hints are discarded. On quadratic levels, a failed integrality
  check retries `J + 1 … J + 4`.

Complex conjugation and the base embedding are certified by root isolation.

<a id="crt-usage"></a>
### Input, output and resource contract of the Chinese remainder theorem certificate

See the [generated help of `sic-certify-crt`](tools.md#sic-certify-crt) for options and exit codes. A bound-only success
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
divisor positions, the canonical unit generators lifted by the Chinese remainder theorem and their orders; it checks `unit_generator_words`
and uses the smallest positive unit lift for entries. The mathematics of the certificate remains
explicitly limited to `d = 2^s ≥ 4`; general-dimension container conformance is not a general-dimension SIC-POVM
certificate.

The certificate performs full container verification before mathematical work: header/features, the
112-byte footer, LDIR/PDIR and both RFC 6962 roots, all physical headers and payload digests,
domain-separated logical digests, EXMT and the closed grammar, canonical leaf encoding, element
primitivity/minimality/attained bit bounds, authenticated greedy rescaling, exact canonical branch
rectangles and their minimum precision, optional JSON, and finalized BALL gauge/norm/tight accuracy.
BALL agreement with exact decoding and the SIC-POVM identities remain separate mathematical checks.

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
refusal is reported as conformance. The hint ceiling of the certificate remains 64 independently of authenticated
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

Example, as run for the release: `sic-certify-crt multiplet.sicx --threads 188 --seed 1`.

RAYS labels follow increasing numeric suffixes and increasing lexicographic numeric Smith tuples
(first coordinate first, §5.1a); `c0` may have a nonzero word. The top-level PROV
`generator_rescaling` key is unconditionally forbidden; nested recorded records are permitted. Both
simultaneous odd-action signs are permitted and authenticated by the exact root. Container
verification does not claim the producer’s private coset order or the Vandermonde identity. The
mathematical certificate verifies action quotients and requires a nonzero denominator before
division.

The optional BALL/exact ratio check is not performed; scope output says so. Outward BALL
construction and binary64 agreement are finalizer obligations, not container checks. Container
success and an exact SIC-POVM certificate do not by themselves establish BALL containment.

<a id="crt-trusted-components"></a>
### Trusted components

The proof is complete given the correctness of:
- FLINT 3 / Arb: ball arithmetic, including the certified root isolation of `acb_poly_find_roots`;
  `n_is_prime` (deterministic below `2^64`); `nmod_poly` root finding and irreducibility; `fmpz`
  Chinese remainder reconstruction;
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

The high-precision certificate `sic-certify-norm` (the norm argument, practical for `d ≤ 16`) is a
second, different method with its own reader.

<a id="crt-measured-cost"></a>
### Cost of the Chinese remainder theorem certificates

The certificates of the release ran on the release machine (192 AMD EPYC 9B45 cores, about 1.4 TiB
of memory, Ubuntu 24.04, GCC 13.3, FLINT 3.6.0, Release build with link-time optimization) with
`--threads 188 --seed 1`. Every run finished `[PROVED]`. Wall time and peak resident memory are the
whole-process measurements of GNU `time`, including input admission.

| multiplet | `[L:K]` | certificate primes | wall time | peak resident memory |
| --- | ---: | ---: | ---: | ---: |
| `d4-m1` | 8 | 1 | 0.03 s | 10.5 MiB |
| `d8-m1` | 16 | 2 | 0.04 s | 10.5 MiB |
| `d8-m2` | 64 | 8 | 0.06 s | 10.7 MiB |
| `d16-m1` | 512 | 25 | 0.20 s | 10.5 MiB |
| `d32-m1` | 2,048 | 95 | 0.94 s | 12.3 MiB |
| `d64-m1` | 16,384 | 349 | 9.6 s | 833 MiB |
| `d128-m1` | 32,768 | 627 | 14.2 s | 1.71 GiB |
| `d128-m2` | 163,840 | 3,293 | 85.5 s | 9.00 GiB |
| `d256-m1` | 786,432 | 17,210 | 1,542 s | 45.9 GiB |

The cost per prime is dominated by the residual tables, about `4[L:K]·d·log d` modular operations.
Prime counts depend on the exact generator presentation and its proved height and denominator
bounds; a changed exact root requires a new certificate.

<a id="crt-limits"></a>
### Limits

- `d ≥ 4` only, real quadratic `K`; point indices are 32-bit (`2[L:K] ≤ 2^32`).
- The proved bound is pessimistic at levels where Lemma I is not run. There Lemma D grows by about
  ×1.5 per level, and the residual bound carries a factor `4d`. With Lemma I the prime count is `≈
  4d·Σ_j (1 + J_j + ½·avg log2|c_j|)/61`.

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

Full container verification checks the mandatory input invariants, not the
imported irreducibility/group premises or exact SIC-POVM identities. BALL-to-exact
comparison is separate, as specified by section 6.1. Text interchange, release
catalogues, parts manifests, network transport and persistent caches are not
inputs of this local binary reader. A transport implementation must enforce its
own conditional requirements before supplying a stable local file.

The optional BALL/exact ratio spot check is not performed by this input
layer, and the verification summary says so. Container success establishes no
BALL/exact agreement, binary64 agreement or SIC-POVM property. Exact singleton branch
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
representatives and the prime-power unit generators with their canonical lifts by the Chinese remainder theorem.
`galois.unit_generator_words` must have the derived count, canonical Smith residues and the
specified order relations. Entry lookup uses local inverse tables and the smallest positive unit
lift. Nonzero second components are rejected in the rational case. The summary identifies
field/group/action and representative kernel fixity as premises; it does not certify them.

Support for general dimensions here is container and branch validation. The norm-certificate
implementation supports power-of-two dimensions
and explicitly refuses other dimensions before constructing its exact algebra.
Finalization constructs BALL from its own certified decode; this tool requires
no external ball input. Synthetic fixtures are conformance data, not SIC-POVM fiducial vectors.

The quadratic path admits at most 512 MiB of estimated live ball workspace
and precision at most 2^20 before allocation. Rounding-to-integer admission
separately bounds the exponent before materializing an integer midpoint.

<a id="reader-independent-sicx-10-reader"></a>
## Independent reader behavior

The independent reader implements SICX 1.0 for all integer dimensions 2 through 2^20, divisor
representatives, canonical prime-power unit generators with their lifts by the Chinese remainder theorem and the generator words. The
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
Full container verification is not a field, automorphism, classification or exact SIC-POVM proof. BALL
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

<a id="clifford-3-identification-sic-identify"></a>
## Identifying a numerical vector

```
sic-identify FILE --catalogue JSON|TSV [--repo DIR] [--tol T=1e-6] [--entry-error E=2^-50] [--moments K=4] [--unitary-only]
```
The input is a vector in the Weyl–Heisenberg convention of this repository. The
[Gabor-frame section of the specification](sicx.md#mathematical-conventions) converts Gabor
notation: the time-frequency shift `M_b T_a` equals `D_(a,b)` up to a phase, so a fiducial vector in
that convention is used as is. A JSON catalogue uses `sic-release-catalogue/1.0`;
`data/catalogue.json` is such a catalogue, and its Hoggar entry is excluded from Weyl–Heisenberg
searches. Explicit TSV input (`d label order status csv_path`, paths relative to `--repo`) is
supported for independent reference fixtures. Catalogue metadata alone does not certify any newly
generated vector.

Output: the input check, the fingerprint `M_1..M_K`, one line per catalogued fiducial vector of the
same dimension, then `MATCH label (...) psi = c D_chi U_F phi, F = [[a,b],[c,e]] mod dbar (det +-1),
chi = (x, y), residual, z_deviation` or `NO_MATCH among N catalogued rays`. Exit 0 for a match, 1 for
no match, not a SIC-POVM fiducial vector (`max deviation > 1e-8`) or an input or runtime error, 2 for
a usage error. A reported element is a numerical equivalence witness at the stated tolerance.

<a id="clifford-invariant-and-its-status"></a>
### Clifford fingerprint and equivalence-search scope

* **Fingerprint (proved invariance).** `z_p = (a_p/|a_p|)^(2d)` with `a_p = ⟨ψ|D_p ψ⟩`, a function of
  `p mod d`; `M_k = Σ_{p ≠ 0} Re(z_p^k)/(d²−1)`. For an element `U` of the extended Clifford group,
  `U D_p U^(−1) = μ_p D_(Gp)` with `μ_p^(2d) = 1` and `G ∈ ESL(2, Z_d)`. For a unitary element
  (`det G = +1`) the values `z_p` are permuted; for an antiunitary element (`det G = −1`) they are
  permuted and complex conjugated. The real parts `Re(z_p^k)`, and hence the moments `M_k`, are
  invariant in both cases. This is the moment invariant used for certified inequivalence; here it is
  computed in binary64 and used only for display. Equal fingerprints do not prove equivalence.
* **Explicit equivalence (numerical, exhaustive search).** If `ψ = c U φ` then
  `z_ψ(p) = z_φ(G p)` for all `p`, with `G = F^(−1) mod d` in `SL(2, Z_d)` (after replacing `φ` by
  `conj(φ)` for antiunitary elements). `G` is fixed by `G e1` and `G e2`, which must lie in the level
  sets of `z_φ` at the values `z_ψ(e1)` and `z_ψ(e2)`; enumerating those pairs (determinant 1) and
  testing all `d²` labels is exhaustive over the extended Clifford group **provided `z_tol` exceeds
  the sum of the `z` errors of both inputs**. `z_error_bound(d, e)` gives an a priori bound in the
  standard floating-point rounding model (not interval arithmetic): for an entry error `e = 2^−50` it
  is `6.4·10^−9` at `d = 1024`, against `z_tol = 10^−6`. Each surviving `G` is then confirmed on the
  vectors: the canonical lift `F`, `u = U_F φ`, and `χ = argmax |⟨D_χ u|ψ⟩|` over all `χ` (`d` fast
  Fourier transforms); a match requires `1 − |⟨ψ|D_χ U_F φ⟩| ≤ 10^−6`. When the fiducial vector has a
  nontrivial stabilizer, several elements map `φ` to `ψ`; the first one found (unitary first) is
  printed. The bound is derived in the comment of `z_error_bound` and assumes the stated entry
  error. A match is a numerical statement about the binary64 points; `NO_MATCH` means that no
  catalogued fiducial vector of that dimension matched within these tolerances. Missing catalogue
  files are reported as unavailable and skipped. The search does not establish inequivalence to
  vectors outside the catalogue.

The Clifford fixture tests identify unitary and antiunitary images of the enrolled numerical
fixtures through dimension 256, using random `F` in `ESL(2, Z_d̄)`, random `χ`, a varying phase and
the fixed scale 1.7; the returned element reproduces the image.

## Identity names

Textual format, algorithm and checkpoint identities use `sic-<name>/1.0` or `sicx-<name>/1.0`.
Readers require the identities in the [identity registry](sicx.md#identity-registry). Fixed-width
binary signatures and record tags retain their specified widths; the header carries the container
version.
