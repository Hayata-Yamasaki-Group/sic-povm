<a id="spec-sicx-10-final-exact-weylheisenberg-sic-fiducial-format"></a>
# SICX 1.0 specification

This document is the normative SICX 1.0 format specification.

A SICX file stores one exact, embedded number-field presentation of a
Weyl–Heisenberg (WH) SIC multiplet. It may contain zero published rays (exact-only),
one ray, or many rays. The exact root identifies that presentation, including its
embedding and entry rule, independently of physical chunk grouping, codec,
provenance, transport parts and finalization. It is not an invariant of abstract
isomorphic fields or Clifford-equivalent vectors. A separate file root binds the
physical layout and finalization. Matching a trusted root establishes identity;
recomputing a root from an untrusted file establishes self-consistency only.

All requirements for an independent reader are here. Internal paths and tool names
are informative. MUST, MUST NOT and SHOULD express requirements, prohibitions and
recommendations. Offsets and lengths are bytes; intervals are half-open unless
stated otherwise. Concatenation is `||`; SHA-256 returns 32 raw bytes (not hex).
Displayed digests in JSON are exactly 64 lowercase hexadecimal digits.

**Scope.** Version 1.0 accepts every integer `2 ≤ d ≤ 2^20`, prime-degree towers with
degrees at most 31, and abelian Galois groups in Smith coordinates. The dimension
cap is an index-width bound: coordinate indices fit in 20 bits, `d̄ ≤ 2^21`,
and flattened displacement indices are below `d² ≤ 2^40`; products and offsets
use checked 64-bit arithmetic (§5.0). It is not a restriction to powers of two
or a promise of feasible computation at the cap. Tower and resource bounds
apply independently. The current producer constructs only `d = 2^s`;
general-d reader paths require synthetic conformance fixtures. This specification
claims neither a general-d producer nor new SIC data. The base-field cases
at `d = 2,3` (§2) use the same entry, encoding and finalization rules as all dimensions.

## Identity registry

JSON document formats use two families: `sic-<name>/1.0` for project data and metadata, and
`sicx-<name>/1.0` for SICX container JSON forms. Every schema `$id` and document `schema` value
matches `^sicx?-[a-z0-9]+(-[a-z0-9]+)*/1\.0$`. Schema filenames retain the uniform `-1.schema.json`
major-version suffix.

Every textual identity uses the same spelling and version rule. The policy checks declarations and
structured fields by location, including algorithm, provenance, transport and checkpoint identities.
Binary signatures have fixed widths; the container version is the header’s major.minor. In
particular, `sic-decode/1.0` identifies a decoder, while `sic-decode-provenance/1.0` identifies its
sidecar schema. Neither the sidecar nor RAYS has a `schema` field naming that decoder.

| Identity | Kind | Where it appears | Part of canonical SICX bytes? |
| --- | --- | --- | --- |
| `sic-decode-provenance/1.0` | textual identity (`sic[x]-<name>/1.0`) | Decode provenance sidecar schema; document has a decoder field, no schema field | No |
| `sic-exact-json/1.0` | textual identity (`sic[x]-<name>/1.0`) | Exact multiplet export and schema | No |
| `sic-fiducial-fp64/1.0` | textual identity (`sic[x]-<name>/1.0`) | Numeric fixture metadata.json and schema | No |
| `sic-fiducial-json/1.0` | textual identity (`sic[x]-<name>/1.0`) | WH ray export and schema | No |
| `sic-fiducial-json-all/1.0` | textual identity (`sic[x]-<name>/1.0`) | All-ray export and schema | No |
| `sic-hoggar-exact-json/1.0` | textual identity (`sic[x]-<name>/1.0`) | data/d8/hoggar/exact.json and schema | No |
| `sic-hoggar-fiducial-json/1.0` | textual identity (`sic[x]-<name>/1.0`) | data/d8/hoggar/c0.json and schema | No |
| `sic-hoggar-manifest/1.0` | textual identity (`sic[x]-<name>/1.0`) | data/d8/hoggar/manifest.json and schema | No |
| `sic-multiplet-manifest/1.0` | textual identity (`sic[x]-<name>/1.0`) | Multiplet manifest and schema | No |
| `sic-npz-bundle/1.0` | textual identity (`sic[x]-<name>/1.0`) | NPZ ZIP archive comment and schema | No |
| `sic-release-catalogue/1.0` | textual identity (`sic[x]-<name>/1.0`) | data/catalogue.json and schema | No |
| `sic-tool-version/1.0` | textual identity (`sic[x]-<name>/1.0`) | CLI --version output and schema | No |
| `sicx-text-json/1.0` | textual identity (`sic[x]-<name>/1.0`) | JSONL header and schema | No; text representation |
| `sicx-text-json-parts/1.0` | textual identity (`sic[x]-<name>/1.0`) | JSONL parts index and schema | No; text representation |
| `sicx-transport-parts/1.0` | textual identity (`sic[x]-<name>/1.0`) | Transport manifest and schema | No; sidecar |
| `sic-decode/1.0` | textual identity (`sic[x]-<name>/1.0`) | RAYS decoder.name; decode provenance decoder field | Yes, in RAYS |
| `sicx-2adic-greedy/1.0` | textual identity (`sic[x]-<name>/1.0`) | EXMT rescaling metadata | Yes |
| `sicx-divisors/1.0` | textual identity (`sic[x]-<name>/1.0`) | EXMT entry-rule metadata | Yes |
| `sicx-dyadic/1.0` | textual identity (`sic[x]-<name>/1.0`) | Construction dimension-policy contract | No |
| `sicx-chunks/1.0` | textual identity (`sic[x]-<name>/1.0`) | Parts manifest alignment field | No |
| `sicx-remote/1.0` | textual identity (`sic[x]-<name>/1.0`) | HTTP User-Agent | No |
| `sic-step-selection/1.0` | textual identity (`sic[x]-<name>/1.0`) | Construction component identity; PROV selector metadata | Yes, when recorded |
| `sic-galois-combination/1.0` | textual identity (`sic[x]-<name>/1.0`) | sic-step-selection/1.0:sic-galois-combination/1.0 registry choice and checkpoint digest | No; registry/checkpoint |
| `sic-conjugate-alignment/1.0` | textual identity (`sic[x]-<name>/1.0`) | Construction component identity; PROV selector metadata | Yes, when recorded |
| `sic-dyadic-evaluator/1.0` | textual identity (`sic[x]-<name>/1.0`) | Construction evaluator registry; nondefault families extend resume records | No; registry/checkpoint |
| `SICX + CR LF SUB LF` | binary signature (fixed width; the container version is the header's major.minor) | Container header, major 1 minor 0 | Yes |
| `SICXEND + SUB` | binary signature (fixed width; the container version is the header's major.minor) | Container footer | Yes |
| `EXMT, TOWR, BODY, PROV, RAYS, BALL, PREC` | binary signature (fixed width; the container version is the header's major.minor) | Container chunk headers | Yes |
| `LDIR, PDIR` | binary signature (fixed width; the container version is the header's major.minor) | Container directories | Yes |
| `SICXPRT1` | binary signature (fixed width; the container version is the header's major.minor) | Producer part journal | No; journal |
| `SICXRNG1` | binary signature (fixed width; the container version is the header's major.minor) | HTTP range-cache records | No; cache |
| `SICXJRN1` | binary signature (fixed width; the container version is the header's major.minor) | Streaming container journal | No; journal |
| `sic-step-selection-checkpoint/1.0` | textual identity (`sic[x]-<name>/1.0`) | Step selection checkpoint | No; checkpoint |
| `sic-analytic-cache/1.0` | textual identity (`sic[x]-<name>/1.0`) | Analytic table checkpoint | No; checkpoint |
| `sic-completed-class/1.0` | textual identity (`sic[x]-<name>/1.0`) | Completed-class cache | No; checkpoint |
| `sicx-raw/1.0` | textual identity (`sic[x]-<name>/1.0`) | Canonical physical chunking profile (§5.4); no stored literal | Defines canonical profile bytes and file root |
| `sic-vector-balls/1.0` | textual identity (`sic[x]-<name>/1.0`) | Decoded raw/normalized ball files and bounds input | No; numeric sidecar |
| `sic-exact-d2-radicals/1.0` | textual identity (`sic[x]-<name>/1.0`) | Dimension-two exact radical input | Its file SHA is stored in d2 PROV; changing the header changes provenance and derived container hashes |
| `sic-fiducial-data-complete/1.0` | textual identity (`sic[x]-<name>/1.0`) | Numeric fixture COMPLETED files | No; sidecar marker |
| `sic-stabilizers/1.0` | textual identity (`sic[x]-<name>/1.0`) | Stabilizer tables; independent stabilizer checker input | No; symmetry data |

<a id="spec-constants"></a>
## Constants

This table is the sole definition of the two format/profile constants. Readers
and writers MUST use these values.

| name | current value | status and use |
| --- | --- | --- |
| `LOGICAL_LEAF_LOG2_LIMIT` (`L`) | 14 | Frozen; limits a logical leaf to `2^L` coordinates (§4.3), affecting the exact root. |
| `PHYSICAL_CHUNK_TARGET_BYTES` | 16,777,216 (16 MiB) | Frozen; greedy payload target for `sicx-raw/1.0` (§5.4), affecting profile bytes and the file root, not container acceptance. |

Changing the logical
limit requires a new version or assigned interpretation feature; existing 1.0
readers MUST NOT silently adopt different logical boundaries.

<a id="spec-1-status-of-the-mathematical-content-premises"></a>
## 1. Status of the mathematical content (premises)

SICX is a container. It stores exact algebraic data and certified numbers; it does not by itself
prove that the stored vector is a SIC. The following are stated honestly:

1. **Exact data.** Every stored integer is exact. Tower, Galois action and representative elements are
   the producer's exact elements after the canonical generator rescaling (§3.7): each leaf equals the
   source leaf divided exactly by a recorded power of two. Here the source presentation is the one
   reconstructed by §3.7; its relation to external recorded inputs is not authenticated by this check.
2. **What conforming finalization certifies per file.** The finalizer checks the canonical encoding,
   hashes, BALL normalization and anchor gauge, and constructs BALL by outward rounding its own certified
   decoded enclosures (§5.2). Container verification (§6) checks the specified encoding, integrity and
   BALL-only constraints; its success alone establishes neither BALL containment of the exact decoded
   values nor equality of their binary64 roundings. A separate decode check reports its own result.
   Both are distinct from an exact SIC certificate.
3. **Premises inherited from the producer** (not re-proved by a SICX reader):
   - the minimal polynomials are irreducible and define the stated extension degrees and field tower;
     §6 nevertheless checks the chosen-base simple nonzero roots, root selection and canonical
     rectangles explicitly (§5.1b), without proving those field premises or recorded producer identity;
   - the action records are genuine automorphisms (`τ_l` acts as stated on every generator) and commute;
   - the entry-action homomorphism and representative kernel-fixity premises of §3.5 hold;
     their exact algebraic verification is separate from container verification;
   - **integrality:** the stored coordinates divided by the stored scale are integral in `O_K` — this is
     how they are stored, but the statement that the trace coordinates of the representatives lie in
     `(1/N) O_K` with the recorded scale is checked per file, not proved in general;
   - the exact SIC property rests on the [construction theorems and stated
     premises](verification.md#verification-1-claims-and-their-status); a numerical overlap check
     (`sic-verify --sic-check`) is a diagnostic, never an exact proof.
4. **Empirical facts used as rules** (checked on every packed file, fail-closed):
   - agreement of the §3.5 entry rule with the producer's exported coordinates — checked against
     every entry line of every packed export (powers of two, d = 4 … 1024, 0 mismatches), not proved
     in general. The general-d rule is a presentation requirement, with no general-d empirical
     evidence claimed;
   - ray records are pure Galois elements with no Clifford correction
     ([reproducibility.md](reproduce.md#reproducing-the-published-results), recorded entry-rule
     checks; four published non-identity selectors match, a wrong-word control fails).

   Recorded format descriptions and run observations in this specification are informative
   background; every rule a reader needs is stated here.
5. **Stabilizer adaptation.** The coordinate rule (§4.2) uses the minimal support level set of each
   element. Native adapted towers make each representative's fixed field a tower level. Recorded
   non-adapted presentations remain mathematically interpretable by the same trace-dual rule; no
   stabilizer-size claim is needed to read them.

<a id="spec-2-fixed-conventions-never-stored"></a>
## 2. Fixed conventions (never stored)

- **Weyl–Heisenberg group.** `X|j⟩ = |j+1⟩`, `Z|j⟩ = ω_d^j |j⟩`, `ω_d = e^{2πi/d}`, indices mod `d`;
  `D_p = τ^{p_1 p_2} X^{p_1} Z^{p_2}`, `τ = -e^{πi/d}`, `p ∈ (Z/d̄)^2`, `d̄ = 2d` for even `d`, `d̄
  = d` for odd `d`. Here `τ² = ω_d`, and `τ` has order `d̄` for both parities. A SIC fiducial is a
  unit vector `ψ` with `|⟨ψ, D_p ψ⟩|^2 = 1/(d+1)` for all `p ≠ (0,0) (mod d)`, checked on `p ∈
  {0,…,d−1}²`. For even `d`, labels nonzero mod `2d` but zero mod `d` are scalar identities and are
  excluded.
- **Base field derived from d.** For `d ≥ 4`, let `D0` be the squarefree kernel of `(d−3)(d+1)`: the
  unique squarefree positive integer for which `(d−3)(d+1) = D0 h²`, `h` a positive integer. Then `K
  = Q(√D0)`. This is the standard SIC quadratic field. Its use here is a format convention, not a
  theorem that arbitrary SIC coordinates satisfy this presentation. For `d ≥ 4`, `(d−2)² <
  (d−3)(d+1) < (d−1)²`, so `D0 > 1`. At `d = 2`, `D0 = −3` and `K = Q(√−3)`; at `d = 3`, `K = Q` and
  `D0 = 1` is a format sentinel (not the squarefree kernel of zero). Readers MUST derive the case
  and `D0` from `d`; neither is stored in EXMT.
- **Integer pairs and embedding.** For quadratic `K`, `O_K = Z[ω]`: if `D0 ≡ 1 (mod 4)`,
  `ω = (1+√D0)/2` and `ι(ω) = (1−s)/2`; otherwise `ω = √D0` and `ι(ω) = −s`.
  Here `s = √D0 > 0` in the real quadratic case, and `s = i√|D0|` with positive imaginary part
  in the imaginary quadratic case. A pair `(x,y)` denotes `x+yω`. Thus the abstract square root always
  maps to `−s`, including at `d = 2`. In the rational case set `ω = 0`, `ι(x) = x`; retain the same
  pair layout but require `y = 0` for every coordinate, making the representation unique.
  For powers of two `d ≥ 4`, `D0 ≡ 1 (mod 4)` ; general dimensions also need the other
  quadratic integral basis (e.g. `d = 5`: `D0 = 3`, `d = 7`: `D0 = 2`).
- **Normalization and phase.** The published vector of a ray is `w = v/‖v‖ · conj(v_a)/|v_a|`, where
  `v` is the decoded vector and `a` is the ray's anchor (stored in RAYS); `w_a` is real positive
  with exactly zero imaginary part. Coordinates are in the original order `0 … d−1`.

<a id="spec-3-mathematical-content"></a>
## 3. Mathematical content



<a id="spec-31-the-tower"></a>
### 3.1 The tower

`L/K` is an abelian extension of degree `D = [L:K]`, presented as a tower
`K = F_0 ⊂ F_1 ⊂ … ⊂ F_t = L`, `F_{i+1} = F_i(g_i)`, `p_i = [F_{i+1}:F_i]` prime (EXMT `tower.degrees`).
`N_i = p_0 ⋯ p_{i−1}` (so `N_0 = 1`, `N_t = D`).

- `p_i = 2`: `g_i = w_i` with `w_i^2 = c_i ∈ F_i`; `c_i` is the element `wsq i`.
- `p_i` odd: `g_i` is a root of `X^{p_i} + Σ_{k<p_i} a_{i,k} X^k`; `a_{i,k}` is the element `fcoef i k`.

The **monomial basis** of `F_n/K` is `g^e = Π_{i<n} g_i^{e_i}`, `0 ≤ e_i < p_i`, indexed by the
little-endian mixed-radix number `e = Σ e_i N_i ∈ [0, N_n)`.

**Embedding.** The branch rectangle `tower.branches[i]` (a pair `[re, im]` of real balls, each a
string in the Arb dump form of §7a; the rectangle is the product of the two intervals) contains
`ι(g_i)` and no other root of the defining polynomial of `g_i` evaluated at the selected lower
roots. This fixes the complex embedding `ι: L → C` (with the embedding of K in §2). How a decoder
selects the roots is described in §3.6.

<a id="spec-32-leaf-coordinates-of-an-element"></a>
### 3.2 Leaf coordinates of an element

For `x ∈ F_n` the **leaf coordinates** are `λ_e(x) = Tr_{F_n/K}(x g^e) ∈ K`, `e ∈ [0, N_n)`: the
coordinates of `x` in the trace-dual basis `{(g^e)^∨}` of the monomial basis, `x = Σ_e λ_e(x) (g^e)^∨`.
The dual basis is a tensor product of per-level relative duals, so `ι(x) = Σ_e λ_e Π_i ι(b_{i,e_i})`
with, at level `i` with root `r = ι(g_i)`:
- `p_i = 2`: `b_{i,0} = 1/2`, `b_{i,1} = 1/(2r)`;
- `p_i` odd: `b_{i,k} = q_k(r)/f'(r)` (Euler), where `f(X)/(X − r) = Σ_k q_k X^k`.

Every exact element in a SICX file is an element `x ∈ F_n` stored through its leaf coordinates
(multiplied by a common integer, §4.3).

<a id="spec-33-galois-group-and-action-records"></a>
### 3.3 Galois group and action records

`G = Gal(L/K) ≅ ⊕_l Z/n_l` (EXMT `galois.smith = [n_0, …]`, `Π n_l = D`), with generators `τ_l`.
Readers MUST check that the list is non-empty, that every `n_l ≥ 2` divides `D` and that `Π n_l =
D`. Producers write the invariant factors in divisibility order `n_0 | n_1 | …` (all files do);
readers do not check the chain and decoding does not depend on it (a word is just a tuple of
residues `c_l mod n_l`). The action of `τ_l` on the tower is given by **action records**:
- `p_i = 2`: element `act` (level `i`, axis `l`) is `A_{il} = τ_l(w_i) w_i ∈ F_i`, so
  `τ_l(w_i) = A_{il}/w_i`;
- `p_i` odd: elements `actdelta` (level `i`) `δ_i ∈ F_i` and `actodd` (level `i`, axis `l`, `k`)
  `δ_i a_k ∈ F_i`, so `τ_l(g_i) = Σ_k a_k g_i^k`;
- an absent record means `τ_l(g_i) = g_i`.

A **Galois word** `c = (c_0, …)`, `0 ≤ c_l < n_l`, denotes `σ_c = Π_l τ_l^{c_l}`.

<a id="spec-34-representatives"></a>
### 3.4 Representatives

There is one **representative** `η_g ∈ L` for every positive divisor `g | d`.
Its element descriptor is `rep`, `position r(g)`, where `r(d) = 0` and
`r(g) = g` for `g < d`. Representatives are ordered by increasing **position**:
`0`, then all proper positive divisors of `d` in increasing order. Thus `η_d`
is the coordinate-zero representative; the divisor
label is not an extra stored field. For `d = 2^s`, the positions are exactly
`{0,1,2,4,…,d/2}`, including `{0,1}` at `d = 2`.

Let `H_g = {σ ∈ G : σ(η_g) = η_g}` and `E_g = L^{H_g}`. The general facts are `[E_g:K] = |G|/|H_g|`
and, under §3.5's premises, `ρ(ker π_g) ⊆ H_g`. The orbit under `ρ(U_d)` has size `[ρ(U_d) : ρ(U_d)
∩ H_g]` dividing `φ(d/g)`, where `φ(1) = 1`. If `ρ(U_d) = G`, this gives `[E_g:K] | φ(d/g)`. Without
surjectivity, no such bound for `[E_g:K]` follows from the entry rule alone. There is no
dimension-only formula for `|H_g|`: `G` may contain actions not in `ρ(U_d)`. Recorded power-of-two
measurements `|H_g| = g` for `0 < g < d/2`, and `|H_d| = |H_(d/2)| = d`, are informative only
([reproducibility.md](reproduce.md#reproducing-the-published-results)), not acceptance criteria.
Readers do not rely on them.

<a id="spec-35-entries-and-rays"></a>
### 3.5 Entries and rays

**Canonical generators (derived, never stored).** Write `U_n = (Z/n)^×`, with
`U_1` the trivial group. Factor `d = Π p^e` and process prime powers in increasing
prime order. The following ordered local generators and their exact orders
give independent cyclic factors, not necessarily in Smith order:

| prime power | ordered local generators | corresponding orders |
| --- | --- | --- |
| `2^1` | none | none |
| `2^2` | `−1 mod 4` | `2` |
| `2^e`, `e ≥ 3` | `−1 mod 2^e`, `5 mod 2^e` | `2`, `2^(e−2)` |
| odd `p^e` | the smallest integer `a ∈ [1,p^e)` of multiplicative order `φ(p^e)` | `φ(p^e) = (p−1)p^(e−1)` |

For each local generator take the unique CRT lift `u_j ∈ [1,d)` equal to that
generator on its prime-power component and `1` on every other component.
Concatenate in the above order; call the orders `o_j`, `j = 0,…,h−1`.
Primitive roots exist for odd prime powers. Their minimality is checkable by
testing candidates in increasing order: a unit `a` is primitive iff
`a^(φ(p^e)/q) ≠ 1 (mod p^e)` for every prime `q | φ(p^e)`.
The CRT decomposition and these local cyclic decompositions prove that every
`u ∈ U_d` has a **unique** tuple `0 ≤ a_j < o_j` with `u = Π u_j^a_j`.
At `d = 2` the generator list and tuple are empty and `U_d = {1}`.

**Words and homomorphism.** EXMT `galois.unit_generator_words[j] = W_j` is the
Smith word for `σ_(u_j)`. Readers MUST check exactly `h` words, each of Smith
rank, with canonical residues, and `o_j W_j = 0` componentwise modulo the Smith
factors. Given the §1 premise that `G` and its actions are genuine and commute,
these relations define a homomorphism `ρ: U_d → G` by
`ρ(u) = σ_(Σ_j a_j W_j)`. It need not be injective or surjective.

**Coordinate and canonical lift.** For `m ∈ [0,d)`, take `g = gcd(m,d)`
(`gcd(0,d) = d`) and `n = d/g`. If `n > 1`, take `k = m/g ∈ U_n`;
if `n = 1`, use the sole class of `U_1`. Let `π_g: U_d → U_n` be reduction.
Define `tilde k` to be the **smallest positive** `u < d` with `gcd(u,d) = 1`
and `u ≡ k (mod n)` (the congruence is vacuous at `n = 1`, so `tilde k = 1`).
This set is nonempty: on prime-power components dividing `n`, any lift of a
unit stays a unit; on components absent from `n`, choose residue `1`, then use
CRT. Thus `π_g` is surjective. Compute the unique tuple of `tilde k` above and set
`word(m) = Σ_j a_j W_j`, reduced componentwise modulo the Smith factors. The
**base vector** is `ψ_m = σ_(word(m))(η_g) = ρ(tilde k)(η_g)`.
In particular `m = 0`, and every positive proper divisor `m | d`, has entry word zero.

**Kernel-fixity premise and lift independence.** For each divisor `g`, the exact
presentation requires `ρ(v)(η_g) = η_g` for every `v ∈ ker π_g`. If two unit
lifts `u,u'` represent `k`, then `v = u' u^(−1) ∈ ker π_g`. Since `ρ` is a
homomorphism, `ρ(u')(η_g) = ρ(u)ρ(v)(η_g) = ρ(u)(η_g)`. This proves lift
independence **under the stated premise**; it is not automatic for arbitrary
representative leaves. At `g = d`, the kernel is all of `U_d`, so `η_d` is
fixed by `ρ(U_d)` (it need not be in `K` if that image is proper).

A separate algebraic verifier can check the premise finitely: enumerate
`v ∈ [1,d)` with `gcd(v,d) = 1` and `v ≡ 1 (mod d/g)` for each `g | d`, apply
the corresponding exact word to `η_g`, and test equality in the exact tower
(equivalently on all exact trace coordinates). A generating set for each kernel
suffices. This check also requires the field/group/action premises in §1 to
have been certified; numerical overlap is not an equality test. A failed exact
identity rejects the algebraic claim; unfinished checks are refusals. Container
verification checks the word relations and all syntactic rules, but does not
certify kernel fixity. Any decode result without that algebraic check MUST
identify kernel fixity and the field/group/actions as premises.

**Discrete logarithms.** A per-file table `u ↦ (a_j)` is RECOMMENDED, built by
enumerating the above direct-product factors; after construction, unit lookup
is constant time. A table of canonical lifts/entry words for all `m` is also
permitted. These tables are derived, not stored in EXMT. The unique tuple,
smallest lift and resulting word are specified; scanning, tables or any other
algorithm yielding exactly those results are conforming. No discrete-log
algorithm or running-time bound is an acceptance condition.

**Reduction to the power-of-two rule.** If `d = 2^s` and `m ≠ 0`, `g` is the
lowest set bit `2^v` and `k = m/g` is odd. It is already a unit mod `d` and the
smallest positive lift, so `tilde k = k`. For `s ≥ 3` its unique tuple is
`(B,A)` with `k = (−1)^B 5^A (mod d)`, `B ∈ {0,1}`, `0 ≤ A < d/4`.
The word is `B·word_minus_1 + A·word_5`, with
`unit_generator_words = [word_minus_1, word_5]`. At `d = 4`, the list is
`[word_minus_1]`; at `d = 2`, it is empty. EXMT uses
`entry_rule.name = "sicx-divisors/1.0"` and the corresponding
`unit_generator_words` list.

**Rays.** Each ray record carries a Galois word `w` (RAYS `rays[].word`). The ray's unnormalized
vector is `v_m = ι(σ_w σ_{word(m)} η_g) = ι(σ_{w + word(m)}(η_g))`, i.e. the base decoding at the
shifted word; its published form is the normalized, gauged `w` of §2 with the ray's `anchor`. Ray
records are listed in the file (they are catalogue data); which rays exist in a multiplet is not
derived by readers.

**Decoding.** To evaluate `ι(σ_c(x))` for an element `x` given by leaves: transport the tower roots
by `σ_c` (apply the action records generator by generator: the image of a root under `τ_l` is
computed from the action record evaluated at the old roots, and identified among the roots of the
defining polynomial at the new lower roots by unique ball overlap, §3.6), rebuild the relative
duals, and contract `Σ_e λ_e Π_i b_{i,e_i}` (§3.2). `sic-decode` implements this with one shared
restriction DAG for all requested coordinates (§11). Cost: `σ_c` takes `Σ_l c_l` generator
applications when generators are applied one at a time, each evaluating the action elements of every
level at the current embedding; see §5.2a for checks that avoid large words.

<a id="spec-36-root-selection-and-transport-decoder-recipe"></a>
### 3.6 Root selection and transport (decoder recipe)

The reference decoder (`cpp/src/decode/`, `prepare`, `member`, `quadratic_roots`, `isolate_roots`,
`transport_odd_value`) selects roots as follows; any method that proves the same identifications is
conforming.

1. **Enclose all roots.** At level `i`, for each embedding of `F_i` reached, evaluate the defining
   coefficients (`c_i`, or `a_{i,k}`, by contracting their elements, §3.2) as complex balls and
   enclose **all** `p_i` roots in pairwise disjoint finite balls, each certified to contain exactly
   one root:
   - `p_i = 2`: roots `±s`. If `Re(c_i)` is certainly negative, `s = i·√(−c_i)`; otherwise `s` is
     the principal `√c_i`. (A ball for `c_i` near the negative real axis would straddle the branch
     cut of the principal square root and give a useless enclosure; the rotation avoids it.) Refuse
     if the ball of `c_i` contains 0, or if the two root balls overlap or contain 0.
   - `p_i` odd: certified simultaneous isolation (the reference uses `acb_poly_find_roots` and
     refuses unless it returns `p_i` finite, pairwise disjoint isolating balls).
2. **Base embedding** (word 0): identify the unique exact root in the branch rectangle and certify an
   isolating enclosure contained in that rectangle. For nonsingleton components, refine the enclosures
   of step 1 until containment and exclusion of the other roots are certified. For every singleton
   component `[q,q]`, first decide exact equality of the corresponding root component with the dyadic
   rational `q`; increasing working precision alone is not an equality test. A concrete certified
   procedure is:
   - Reconstruct the previously selected roots and the defining coefficients as exact complex algebraic
     numbers, starting from §2's exact base-field embedding and using §3.2's exact trace-dual arithmetic.
     Represent their compositum by a primitive element `θ`, its minimal polynomial over `Q` and its
     selected isolating embedding. Clear rational denominators in the coefficient expressions to form
     `f(θ,X)`, and eliminate `θ` by a resultant with its minimal polynomial. Isolate the roots of the
     squarefree part of that rational polynomial exactly. Retain only roots `z` for which substitution
     in the original polynomial with the selected exact coefficients gives exactly zero. Require
     exactly `p_i` distinct roots, each simple and nonzero. Match them to the certified root enclosures
     of step 1, refining until each enclosure contains exactly its identified root.
   - Compute `Re(z) = (z + conjugate(z))/2` and `Im(z) = (z − conjugate(z))/(2i)` by exact algebraic
     arithmetic. Compare each singleton component with `q` by exact equality. Exclude a root if the
     equality fails; if it holds, replace that component of its enclosure by the exact singleton
     `[q,q]`. Refine the remaining nonsingleton components into the rectangle. This includes exactly
     real roots (`Im(z) = 0`), exactly imaginary roots (`Re(z) = 0`), and nonzero dyadic singletons.
   - Require exactly one exact root in the closed rectangle. Zero or multiple exact matches is
     inconsistent input (reject). Failure to finish an exact operation, root matching or refinement
     within admitted resources/precision is refusal, never permission to guess equality.

   An equivalent certified algebraic procedure is permitted. These exact-component checks are also
   required whenever a decoder must fit a root enclosure into a singleton component elsewhere.
3. **Transported root.** For `σ = τ_l` applied to an embedding whose roots are known, compute the
   candidate `t = ι(τ_l(g_i))` from the action record at the *source* embedding: `t = A_{il}/r`
   (`p_i = 2`; refuse if the ball of `r` contains 0) or `t = (Σ_k (δ_i a_k) r^k)/δ_i` (odd; refuse
   if the ball of `δ_i` contains 0), with `r` the source root of level `i` and the action elements
   evaluated at the source embedding of `F_i`. Then select the unique isolating ball at the
   target embedding of `F_i` that **overlaps** `t`. No inflation is needed: `t` contains the true
   image root, which lies in exactly one isolating ball, so at least one ball overlaps `t`. **No**
   overlapping ball means the exact data are inconsistent (reject); more than one means the
   precision is insufficient (refuse).
4. **Refusal** means: raise the working precision (the reference doubles it) and repeat the pass.

A decoder that instead refines a single root by interval Newton starting from `t` MUST first enlarge
`t` to a box `B` for which the Newton image is strictly inside `B` (a ball computed at working
precision `p` has radius near `2^−p`, usually too small for that inclusion to hold); then `B`
contains exactly one root, and since `t ⊆ B` contains the image root, that root is the transported
one. A certified Newton inclusion may require inflating the starting rectangle, for example to
radius `2^−p/2`.

**Precision (informative).** Library options override PREC hints. When unset,
`delta` uses PREC `delta_bits` or 64, and `base_bits` uses PREC
`base_path_bits` or the smaller of 2048 and the computational ceiling. The
base precision is at least `target + delta`. The base path is refined until
its certified roots fit the canonical branch rectangles; other paths start at
`target + delta`. Precision refusals and failed radius checks trigger retries
within the configured attempt and precision limits. If a retry exceeds the
base precision, the base path is recomputed, starting at twice the new working
precision or the ceiling, whichever is smaller. PREC values are hints only
(§5.3); they never replace root, radius or rounding checks.

<a id="spec-37-generator-rescaling-canonical-2-adic-form"></a>
### 3.7 Generator rescaling (canonical 2-adic form)

The tower fixes the fields `F_i` and the embedding, not the generators: for any rational `b_s ≠ 0`,
`g_s' = b_s g_s` generates the same step, and every automorphism fixes `b_s`. SICX 1.0 files are
written with `b_s = 2^{−J_s}`, `J_s ≥ 0` an integer, chosen by the rule below; the stored data then
describe the tower with generators `g_s'` and are an ordinary SICX tower in every respect. The
shifts are authenticated in EXMT; decoding (§3.2, §3.5, §3.6) uses the stored elements and branch
rectangles only, and the decoded values are the same numbers (below).

**Transform (normative for producers).** Let `p = p_s`, `C = p(p−1)/2`, and for a leaf index `e` of
an element of `F_n` let `σ(e) = Σ_{i<n} e_i J_i`. Every stored element is replaced as follows; each
leaf is divided **exactly** (all quotients are integers in `O_K`, i.e. both coordinates of `x + yω`
are divisible):

| element | new element | leaf `λ_e` divided by |
| --- | --- | --- |
| `rep r(g)` (and every element as a field element of `F_n`) | the same element `x` | `2^{σ(e)}` |
| `wsq s` | `c' = g_s'^2 = 4^{−J_s} c` | `2^{σ(e) + 2 J_s}` |
| `act s l` | `A' = τ_l(g_s') g_s' = 4^{−J_s} A` | `2^{σ(e) + 2 J_s}` |
| `fcoef s k` | `a_k' = 2^{−J_s (p−k)} a_k` (`g_s'` is a root of `X^p + Σ_k a_k' X^k`) | `2^{σ(e) + (p−k) J_s}` |
| `actdelta s` | `δ' = Π_{j<j'} (v'_{j'} − v'_j) = 2^{−C J_s} δ` (`v_j` the conjugates of `g_s` over `F_s`) | `2^{σ(e) + C J_s}` |
| `actodd s l k` | `(δ t_k)' = 2^{−(C+1−k) J_s} δ t_k`, since `τ_l(g_s) = Σ_k t_k g_s^k` (the `a_k` of §3.3) gives `τ_l(g_s') = Σ_k t_k 2^{(k−1) J_s} g_s'^k` | `2^{σ(e) + (C+1−k) J_s}` |

(Leaves: `λ'_e(x) = Tr(x Π_i g_i'^{e_i}) = 2^{−σ(e)} λ_e(x)`; the own-level factor is the change of
the element itself. The signed choice of `δ` is part of the exact presentation as specified below.)
The selected root of level `s` is multiplied by `2^{−J_s}`. An existing isolating rectangle can
first be shifted by this factor, but the stored rectangle MUST then be regenerated by the canonical
rule of §5.1b for the rescaled exact presentation. Scale, level set and `max_bits` (§4.2) are those
of the new leaves; the level sets are unchanged (division keeps the nonzero pattern).

**Signed Vandermonde and producer ordering.** At an odd level `s`, the native
producer uses `v_j = gv[p_s c + j]`, `j = 0,…,p_s−1`, over each lower coset
label `c`, and multiplies `(v_(j') − v_j)` for increasing `j < j'`. Its labels
are constructed as follows (informative description of the producer, not
additional EXMT fields). Enumerate group elements by the little-endian Smith
index `Σ_l h_l Π_(a<l) n_a`. Let `M = lcm(n_l)` and
`E(φ,h) = (Σ_l φ_l h_l (M/n_l)) mod M`, in `[0,M)`. For the producer's
selected tower characters `φ_s`, start with `label_0(h) = 0`, `rep_0(0) = 0`.
Given `c = label_s(h)`, define
`label_(s+1)(h) = p_s c + ((E(φ_s,h) − E(φ_s,rep_s(c))) mod M)/(M/p_s)`.
The division is exact; `rep_(s+1)(b)` is the first group element in that
enumeration with label `b`. Thus `gv[p_s c+j]` is the conjugate at
`rep_(s+1)(p_s c+j)`, and this `j` order fixes the producer's sign.

EXMT does not store the characters `φ_s`. Consequently 1.0 does **not**
require a reader to reconstruct this private ordering or choose a universal
Vandermonde sign. The selected sign of `actdelta`, together with the same
sign on every `actodd` numerator at that level, is part of the exact
presentation and is authenticated by the exact root. Simultaneously negating
them gives another permitted presentation with the same action quotients;
it generally changes the exact root. The canonical rescaling rule fixes
powers of two for the given presentation, not this sign.

A container verifier MUST check the element/axis/coefficient presence rules,
coordinate canonicality and rescaling on the stored signed data; it MUST NOT
claim to have verified the producer's private coset order. Decoding MUST
certify a nonzero `δ` before division (§3.6). Certifying the Vandermonde
claim algebraically requires `δ ≠ 0` and `δ² = disc(f_s)`, where the monic
polynomial discriminant is `Π_(j<j') (v_(j') − v_j)²`, and certifying the
actions requires checking the quotient identities of §3.3. These are separate
algebraic checks (§1, §6.1), not mandatory container checks; neither sign is
preferred by them.

**Rule (normative).** Count once per **nonzero scalar trace-coordinate pair**
`λ_e = x_e + y_e ω` of every element (tower, action and representative), over
its full field-level index range. Here `x_e,y_e` are the integer components
of the **full λ before the transform**, before scale division; they are not
the primitive stored pair `c_s` of §4.2 or the transformed pair `(r,y)` of
§4.4. Zero pairs, including implicit zeros outside the support, contribute
neither constraints nor gains. Each nonzero pair has one weight vector
`a ∈ N^t`: the digits `e_i` below the element's field level plus the own
weight of the table at the element's level (`2` for `wsq`/`act`, `p−k` for
`fcoef`, `C` for `actdelta`, `C+1−k` for `actodd`, none for `rep`). Its
valuation is `v = min(v_2(x_e), v_2(y_e))`, taking `v_2(0) = +∞`.

`J` is admissible iff `a·J ≤ v` for every such pair. Let
`gain_s = Σ_(element,e : λ_e ≠ 0) a_s`, summing once per pair, not once
per integer component, run, logical leaf (§4.3), or physical chunk. The
levels with `gain_s > 0` are taken in order of decreasing `gain_s`, ties by
increasing `s`; in that order `J_s` is the largest integer keeping every
pair constraint satisfied, given the values already fixed and 0 for levels
not yet reached. Levels with `gain_s = 0` get `J_s = 0`. Thus grouping and
scale extraction cannot change the objective or the greedy order. Every
route starting from the same full scalar pairs and exact presentation
writes the same rescaled exact data.

**Properties.**
- *Exact and integral* by construction; `J` is bounded by the leaf valuations, so no cap is needed.
- *Same decoded values.* With the per-level dual bases of §3.2, `b'_{s,k} = 2^{k J_s} b_{s,k}` (the
  dual basis of `{g'^k}` scales inversely), so `λ'_e Π_i b'_{i,e_i} = λ_e Π_i b_{i,e_i}` term by
  term; the transported roots, action quotients `A'/r'` and `(Σ (δa_k)' r'^k)/δ'` are the old ones
  times `2^{−J_s}`. In ball arithmetic all these factors are exact exponent shifts. (Measured: the
  reference decoder gives bit-identical raw balls for the fixtures before and after rescaling; see
  [reproducibility.md](reproduce.md#reproducing-the-published-results).)
- *Idempotent.* The rule applied to a file in canonical form gives `J = 0`: after the greedy no
  level of positive gain can be raised, feasibility only decreases, and the transform keeps the
  nonzero pattern and the gains. Full verifiers MUST check this; partial readers may defer it until
  all element leaves have been read (§6).
- *Recorded* in EXMT `generator_rescaling` (§5.1), including the rule and all shifts. Stored
  elements and branches are already rescaled: readers MUST NOT divide them by the shifts again. To
  verify, undo the pair transform, multiply each primitive pair by its element's `scale` to recover
  stored full `λ'_e`, and multiply by `2^(a·J)` to reconstruct `λ_e` before the transform (symbolic
  valuation arithmetic is permitted). Reapplying the greedy rule MUST reproduce both the stored data
  and the recorded shifts; applying it directly to the stored full pairs MUST give zero residual
  shifts. The reconstructed presentation is what this relation verifies. No authenticated
  original-input bytes or digest are provided: this does not authenticate an external recorded
  producer input. Such an association requires independently authenticated provenance. PROV cannot
  override EXMT.

<a id="spec-4-exact-element-encoding"></a>
## 4. Exact element encoding



<a id="spec-41-element-set-and-order"></a>
### 4.1 Element set and order

EXMT `elements` lists every exact element, in this canonical order (readers MUST reject other orders and
missing or duplicate elements):

1. for each level `i = 0 … t−1`: `wsq i` (if `p_i = 2`) or `fcoef i 0 … p_i−1` (if odd); then
   `actdelta i` (present iff level `i` is odd and has any odd action); then for each axis `l` in
   order: `act i l` or `actodd i l 0 … p_i−1` (present or absent as a whole);
2. then `rep r(g)` in increasing position: `0`, then every proper positive divisor of `d` (§3.4).

Element ids are positions in this list. An element of level `i` lies in `F_i`; a representative lies in
`F_t = L`.

<a id="spec-42-coordinates-scale-and-level-set-stabilizer-adapted-form"></a>
### 4.2 Coordinates, scale and level set (stabilizer-adapted form)

For an element `x ∈ F_n` with leaves `λ_e`, the file stores:

- **level set** `J ⊆ {0, …, n−1}` (EXMT `levels`, increasing): the **minimal** set such that every
  nonzero leaf has `e_i = 0` for all `i ∉ J`. Readers verifying a file MUST check minimality (every
  level of `J` occurs as a nonzero digit of some stored coordinate).
- **sub-index** `s ∈ [0, Π_{j∈J} p_j)`: little-endian mixed radix over the levels of `J`; the full index
  `pos(s)` inserts zero digits at the levels not in `J`. Coordinates are stored in increasing `s`.
- **scale** `u` (EXMT `scale`, a positive decimal string): the gcd of all integers `x, y` of all
  nonzero leaves (1 for the zero element). The stored coordinate is `c_s = λ_{pos(s)}/u ∈ O_K`, as a
  pair `(x, y)`; the stored coordinates of an element are primitive (gcd 1).
- **max_bits**: the maximum bit length of `|x|` and `|y|` over the nonzero full leaves `λ = u·c`
  (attained; verifiers MUST check equality, readers MUST reject a leaf above it). Bit length of `n ≠
  0` is `⌊log2 |n|⌋ + 1`; of 0 it is 0.
- **Zero element** (every leaf zero; two elements of the `d = 512` files): `levels []`, `scale "1"`,
  `max_bits "0"` — each follows from the rules above (minimality, primitivity convention, maximum
  over an empty set taken as 0) and verifiers check all three. It is stored as one block of one
  coordinate holding one zero run.

**Interpretation (stabilizer adaptation).** If `K(g_i : i ∈ J)` is the stabilizer field `E` of the
element with monomial basis `{g^{pos(s)}}` and `Tr_{L/E}(g^e) = 0` for `e` outside the sub-layout
(the producer's adapted tower certifies this), then `λ_{pos(s)} = [L:E] · Tr_{E/K}(x g^{pos(s)})`,
and the stored coordinates are the stabilizer-adapted trace coordinates `Tr_{E/K}(x g^{pos(s)})` up
to the factor `u/[L:E]`; the `|H_m|` factor of the full-trace layout is absorbed into `u`.
Observation (not enforced): in every packed file the format trace factor `[F_n : K]/|S|` divides
`u`; it equals `[L:E]` only for an adapted tower. Readers never rely on this interpretation:
decoding uses `λ = u·c` on the sub-layout and zero elsewhere, which is exact by definition.

<a id="spec-43-canonical-logical-leaf-boundaries"></a>
### 4.3 Canonical logical leaf boundaries

The Constants table defines `LOGICAL_LEAF_LOG2_LIMIT` (`L`); the maximum is
`2^L` coordinates. Here `L` is a limit exponent, not the field `L` of §3.

For an element with ordered level set `J = [j_0, …, j_(h−1)]`, let
`W_0 = 1` and `W_k = Π_(a<k) p_(j_a)`. Choose the largest `k ∈ [0,h]`
with `W_k ≤ 2^LOGICAL_LEAF_LOG2_LIMIT`. Its logical leaves, in order, have
`first = b W_k`, `count = W_k`, `0 ≤ b < W_h/W_k`. A leaf varies the
lowest `k` active digits and fixes all higher active digits: the largest
aligned tower subtree within the limit. There is no shorter final leaf and
no tunable `block_coordinates`. For `J = []`, there is one leaf with
`first = 0, count = 1`, including for a zero element.

The rule depends on the exact element's minimal level set and tower degrees,
not its encoding length, working precision, machine, thread count or codec.
TOWR leaves cover tower/action elements, BODY leaves cover representatives.
Leaves are ordered by element id (§4.1), then increasing `first`. Physical
chunks may group consecutive leaves (§5.4); they never change these boundaries.

<a id="spec-44-canonical-logical-leaf-bytes"></a>
### 4.4 Canonical logical leaf bytes

```
varint element | varint first | varint count | runs
run   := varint(2n + 0)                       -- n zero coordinates
       | varint(2n + 1) (int r, int y) × n    -- n nonzero coordinates
```

- `first`, `count` MUST match the logical boundary rule; runs MUST cover `count` coordinates
  exactly, alternate in kind and be nonempty; a data run MUST NOT contain `(0, 0)` (so zero runs are
  maximal).
- **Pair transform.** For `D0 > 1`, `D0 ≡ 1 (mod 4)`, `(x, y)` is stored as `(r, y)` with `r = x −
  k(y)`, `k(y) = ⌊y(√D0 − 1)/2⌋`, computed in integers: `k(0) = 0`; for `y > 0`, `a = isqrt(D0 y²)`;
  for `y < 0`, `a = −isqrt(D0 y²) − 1`; `k = ⌊(a − y)/2⌋`. Readers recover `x = r + k(y)`. For the
  other quadratic integral basis (`D0 ≢ 1 (mod 4)`), `r = x`. For rational `K` (`d = 3`, `D0 = 1`),
  `r = x` and every `y` MUST be zero; nonzero second components are rejected rather than silently
  discarded. Scale and primitivity are still computed on the same pairs, so this is the ordinary
  layout with a unique rational representation. **Balanced rule for `D0 < 0`, `D0 ≡ 1 (mod 4)`** (`d
  = 2`): `ι(ω) = (1 − i√|D0|)/2` has real part `1/2`, so `Re(x + yω) = x + y/2`. The pair is stored
  as `(r, y)` with `r = x + ⌊y/2⌋` (floor division, so `r` is the real part rounded down to an
  integer and `(r, y)` has the size of the embedded real and imaginary parts), and readers recover
  `x = r − ⌊y/2⌋`. This is exact integer arithmetic; no square root is involved. Test vectors (`D0 =
  5`; §12 gives a complete fixture): `k(4) = 2`, `k(2) = 1`, `k(−1) = −1`, `k(−2) = −2`, `k(−8) =
  −5`; so `(0,4) ↦ (−2,4)`, `(2,−2) ↦ (4,−2)`, `(8,−8) ↦ (13,−8)`, `(0,−1) ↦ (1,−1)`, `(0,−8) ↦
  (5,−8)`, `(−2,2) ↦ (−3,2)`. A re-encoding check alone cannot detect a wrong `k(y)` used
  consistently in both directions; these vectors and the overlap of decoded values with BALL (§5.2a)
  do.
- **varint**: unsigned LEB128, at most 10 bytes, no redundant trailing `0x00` group, value < 2^64.
- **int**: `varint(2n + sign)` followed by `n` little-endian magnitude bytes, most significant byte
  nonzero; `sign = 0` for a positive integer and `sign = 1` for a negative integer; zero is the
  single byte `0x00`; code `1` (negative zero) is invalid. Readers MUST bound `n` (reference limit
  2^20).

<a id="spec-5-container-directories-and-roots"></a>
## 5. Container, directories and roots



<a id="spec-50-byte-grammar-and-ordering"></a>
### 5.0 Byte grammar and ordering

Fixed-width `u16`, `u32`, `u64` are unsigned little-endian integers; `i64` is
little-endian two's complement. There is no alignment padding. Every sum,
product, offset and allocation size MUST be checked for overflow before use.

```
header (16 bytes):
  byte  0  magic[8] = 53 49 43 58 0d 0a 1a 0a
  byte  8  u16 major = 1
  byte 10  u16 minor = 0
  byte 12  u32 required_features = 0

chunk header (16 bytes):
  byte  0  type[4]                         ASCII, each byte 0x21..0x7e
  byte  4  u32 flags
  byte  8  u64 encoded_length
  byte 16  encoded_payload[encoded_length]

file:
  header
  EXMT TOWR* BODY* PROV?                   exact prefix
  (RAYS BALL+ PREC?)?                      optional finalization
  LDIR PDIR footer
```

EXMT MUST be first, at offset 16. All chunks and the footer are contiguous;
nothing follows the footer. There is exactly one EXMT, LDIR and PDIR.
TOWR/BODY chunks follow element/leaf order (§4). PROV occurs at most once,
after all elements. RAYS occurs at most once; its nonempty list determines
exactly one BALL chunk per ray in list order. PREC occurs at most once,
after those BALL chunks. BALL and PREC MUST NOT occur without RAYS. Zero rays
are represented by the absence of the entire finalization part, not an empty
RAYS array. Exact-only and finalized files are both valid.

`flags` bit 0 means required; all other bits MUST be zero. EXMT, TOWR, BODY, RAYS, BALL, LDIR and
PDIR MUST have flags 1; PROV and PREC MUST have flags 0. Readers MUST reject every chunk type not
defined by SICX 1.0, regardless of flags. There are no optional unknown chunks or extension gaps.
Flags 0 make only the defined PROV and PREC chunks optional. Only the defined chunk names are
accepted. LDIR/PDIR cannot occur as ordinary data chunks.

Version 1.0 readers MUST reject any other major/minor, nonzero header
`required_features`, unknown required EXMT feature, nonzero reserved field,
or footer layout. The header
feature field is a physical feature bitmask, distinct from EXMT's logical
`required_features` array. No physical feature bits are assigned in 1.0.
Extensions require assigned `required_features` bits in a later version;
a future codec also needs a specification of its deterministic decoding.
Version 1.0 readers reject those versions and features.

<a id="spec-51-exmt-required-exact-metadata"></a>
### 5.1 EXMT (required exact metadata)

EXMT is one uncompressed canonical JSON document (§7), with exactly these keys:

| key | content |
| --- | --- |
| `format`, `version` | `"SICX"`, `["1","0"]` |
| `required_features` | `[]`; future interpretation features require a new specification |
| `d` | any integer `2 ≤ d ≤ 2^20`; derives the base field, divisor positions and unit generators |
| `tower` | exactly `degrees`, `branches`, `branch_bits` (§5.1b); 1–62 degrees, each prime ≤ 31, `D = Π p_i ≤ 2^40`; one branch pair per level |
| `generator_rescaling` | exactly `rule: "sicx-2adic-greedy/1.0"` and `shifts: [J_0,…,J_(t−1)]`; nonnegative integers of at most 18 decimal digits; §3.7 |
| `galois` | exactly `smith`, `unit_generator_words`; the latter is the ordered array `[W_0,…,W_(h−1)]` of §3.5, one word per derived generator, one canonical residue per Smith factor; `[]` at `d = 2` |
| `entry_rule` | exactly `name: "sicx-divisors/1.0"` and `representatives`: the decimal-string positions `["0", then every proper positive divisor of d in increasing order]`; divisor `d` uses position `0` (§3.4); algorithm §3.5 |
| `elements` | ordered array of descriptors below, one per element (§4.1) |

Every element object has exactly its row's keys; array position is its id:

| `kind` | keys | constraints |
| --- | --- | --- |
| `wsq` | `kind`, `level`, `levels`, `max_bits`, `scale` | `p_level = 2` |
| `fcoef` | `kind`, `level`, `k`, `levels`, `max_bits`, `scale` | `p_level` odd, `0 ≤ k < p_level` |
| `actdelta` | `kind`, `level`, `levels`, `max_bits`, `scale` | odd level with at least one action axis |
| `act` | `kind`, `level`, `axis`, `levels`, `max_bits`, `scale` | quadratic level, `0 ≤ axis <` Smith rank |
| `actodd` | `kind`, `level`, `axis`, `k`, `levels`, `max_bits`, `scale` | odd level, axis and `k` as above |
| `rep` | `kind`, `position`, `levels`, `max_bits`, `scale` | `position` from `entry_rule.representatives`; field `F_t` |

For non-representatives `0 ≤ level < t`, the field is `F_level`. `levels` is
strictly increasing, entries less than that field level. `scale` is a positive
decimal string, at most 4,095 digits; `max_bits ≤ 2^23`; other numeric strings
have at most 18 digits. The invariants of §4.2 apply. EXMT bytes MUST equal the
canonical serialization of precisely these keys. Actions may be absent as in
§3.3, but an odd axis has either all `p_i` coefficient elements or none, and
`actdelta` is present iff some odd axis is present at that level.

There is no `body`, `block_coordinates`, `catalogue`, `field`, conductor,
codec, chunk target, ray selection, ball-record width or provenance in EXMT.
Physical choices are expressed by PDIR; free catalogue labels belong in the
release catalogue or PROV. Multiplet numbers are release names, not EXMT data.

**Entry metadata validation.** Readers MUST factor the bounded `d`, derive the
ordered local generators/orders and CRT lifts of §3.5, and compare the
representative array with the complete ordered divisor-position list. They
MUST check word count/rank/residues and `o_j W_j = 0` modulo every Smith
factor; a failed relation is rejection. These checks use integer arithmetic
only and do not require an algebraic certificate. Missing, additional,
reordered or duplicate representative positions and missing/extra words MUST
be rejected. Only the specified EXMT keys and entry-rule identity are accepted.
No stored generator residues, primitive roots, base field or embedding may
override these derived conventions; extra keys are rejected. Kernel fixity
and the action identities remain explicit algebraic premises (§3.5, §6.1).

<a id="spec-51b-canonical-root-isolation-rectangles"></a>
### 5.1b Canonical root-isolation rectangles

This rule is a function of the **rescaled exact tower and selected embedding**,
not of the producer's numerical enclosures. The embedding is part of the exact
input; canonicalization does not choose a different conjugate. Let `z_(i,j)` be
all exact roots of level `i`'s defining polynomial with its coefficients at the
selected base embedding of `F_i`. These roots must be simple and nonzero. One
root at each level is the selected `ι(g_i)`.

For `p ∈ {128,256,512,1024,…}`, construct a real interval `B_p(x)` for each
real or imaginary component `x` of every `z_(i,j)`:

1. Let `q` be the nearest integer to `2^p x`, ties to even (including negative
   ties). These comparisons are comparisons of exact real algebraic numbers;
   uncertain numerical rounding MUST be refined, not guessed.
2. If `2^p x = q` exactly, `B_p(x)` is the singleton `q·2^−p` (radius zero).
   Otherwise its midpoint is `q·2^−p` and its radius is exactly `2^−p`.
3. Serialize midpoint and radius in §7a's form, removing all powers of two
   from nonzero mantissas; zero midpoint/radius has exponent zero.

Let `Q_p(z) = B_p(Re z) × B_p(Im z)`. Choose the **smallest p in the above
sequence** for which, at every level, all `Q_p(z_(i,j))` are pairwise disjoint
as closed rectangles and none contains complex zero. Store that single `p`
as `tower.branch_bits` and store only `Q_p(ι(g_i))` in `tower.branches[i]`.
There is no per-level override. Verification of canonical branch choice MUST
check this minimum and these exact midpoint/radius rules, not just isolation.

**Why this supports §3.6.** Each rectangle contains its exact root. A
non-singleton component has at least `2^−p/2` clearance from the root to its
boundary. Distinct simple roots have positive separation, and nonzero roots
have positive distance from zero, so the search terminates for a valid tower.
Certified root enclosures can be refined into these rectangles; a singleton
component requires exact equality rather than an approximate enclosure. In
particular exact real roots need exact zero imaginary parts. The decoder may
use any certified method that proves §3.6's same unique-root identification;
its working precision is independent of `branch_bits`. This is an existence
argument for finite isolation precision, not a uniform bound for Newton or
transported decoding. If exact comparisons or isolation exceed resource limits,
a tool MUST report refusal, never substitute its current working-precision ball.

<a id="spec-51a-rays-first-finalization-chunk"></a>
### 5.1a RAYS (first finalization chunk)

Canonical JSON with exactly `balls`, `rays`, and optionally `decoder`:

| key | content |
| --- | --- |
| `balls` | exactly `limbs` (1–1024; §5.2) and `working_bits` (1–2^20, informative) |
| `rays` | nonempty array; each object has exactly `label`, `word`, `anchor`, `accuracy_bits` |
| `decoder` | exactly `name` (`sic-decode/1.0`) and `source_commit` (full Git commit, 40 or 64 lowercase hex digits); other decoder identifiers are rejected |

`label` MUST match ASCII `c(0|[1-9][0-9]*)`; its integer suffix is at most 18 digits. Labels MUST be
exactly `c0`, `c1`, … in array order. Canonical words are compared lexicographically as vectors of
numeric Smith residues (first coordinate first); ray words MUST be strictly increasing. A reader
MUST reject unknown spellings; there is no fallback to `c0`. `word` has one residue per Smith
factor. `anchor` is in `[0,d)`. Labels `c<N>` are assigned by increasing canonical word among the
file's rays; `c0` need not have word zero. The release supplies `m<N>` (positive integer, no leading
zeros) and forms the full id `d<d>-m<N>-c<N>`; the ray label alone does not carry `m<N>`.

**`accuracy_bits` is tight.** For each BALL real/imaginary part with nonzero
radius `m·2^e`, set `b = e + bitlen(m)`. The ray's `accuracy_bits` MUST be
`−max b` over all such parts, with absolute value at most `2^20`. All-zero
radii are rejected. Every radius is strictly below `2^−accuracy_bits`.

The anchor fixes §2's gauge and is recorded, not inferred by readers. The
CLI requires an explicit anchor for every finalization ray. Its decode cost
is discussed in §5.2a.

The decoder record is informative; it does not certify BALL by assertion.
Release finalizers MUST write it after certified decoding, BALL construction
and the rounding checks of §5.2.
The decoder is identified by source commit; `sha256` is not an
allowed key in this record. Non-release files may omit the record. The same
rule applies at `d = 2`. RAYS and its ray selection are under the **file root**,
not the exact root; the trusted catalogue binds labels, words and exact root.

<a id="spec-52-ball-one-required-chunk-per-ray"></a>
### 5.2 BALL (one required chunk per ray)

`d` coordinate records, each real then imaginary; each real part is
`32 + 8·limbs` bytes:

```
u64 flags | i64 E | u64 m | i64 e | u64 limb[0] | … | u64 limb[limbs−1]
```

It denotes `[s·M·2^E − m·2^e, s·M·2^E + m·2^e]`, where `s = −1` for
flags 1, `+1` for flags 0, and `M = Σ limb[q]·2^(64q)` is nonnegative.
Limbs are least significant first. BALL decoded length is exactly
`d·2·(32 + 8·limbs)`. Coordinate `j` begins at byte `j·2·(32 + 8·limbs)`.
Width is fixed per file; minimal width is recommended but not required.

Readers MUST reject a part unless: flags are 0 or 1; `|E|,|e| ≤ 2^40`;
`M = 0` implies flags 0 and `E = 0`; nonzero `M` is odd; `m = 0` implies
`e = 0`; and nonzero `m` is odd and less than `2^30`. Thus every part has
the canonical finite Arb dump representation of §7a, with `M < 2^(64·limbs)`.
There is no negative zero and no non-finite special value.

Full verification MUST check the anchor's imaginary part is exactly zero,
its real interval strictly positive, and an outward-rounded enclosure of
`Σ|w_j|²` contains 1, computed at `working_bits + 64` bits or greater. It
also checks tight `accuracy_bits`. This normalization check is necessary,
not a proof of exact normalization or of containment of an exact vector.
Precision is a property of the finalized file. Recorded release-stage balls
used 256 working bits and accuracy about 250–253 bits; those are observations,
not mandatory precision.

**Finalizer obligation.** The finalizer's inputs are the verified exact-only
presentation (or the exact prefix of a verified prior snapshot), ordered ray
words/labels and anchors, the requested precision/stored width, and optional
provenance/hints. External publication balls are **not** an input. For every
ray it MUST certify the anchor is nonzero, decode all coordinates and compute
certified enclosures `C_j` of the normalized, gauged exact values of §2.
The anchor's imaginary component MUST be set to exact zero using the gauge
identity, and its real enclosure MUST be strictly positive.

For each real/imaginary component, the finalizer MUST construct BALL by
rounding its **own `C_j` enclosure outward to the stored width**: choose a
representable midpoint and a representable nonnegative radius such that
the stored closed interval contains the entire source interval, accounting
for both midpoint displacement and upward radius rounding. The result MUST
obey all canonical part, exponent and mantissa bounds above. It MUST certify
endpoint containment `lower(BALL) ≤ lower(C_j)` and
`upper(C_j) ≤ upper(BALL)` with exact dyadic comparisons or certified outward
arithmetic. Overlap alone is insufficient. If the width/precision does not
permit these checks and the remaining admission checks, raise it or refuse.

The finalizer MUST also certify a unique nearest-even binary64 rounding for
both `C_j` and the stored BALL part and require the same value, for every
component of every ray. It then checks BALL gauge, norm enclosure and tight
accuracy as above before admitting finalization. Thus each BALL interval
contains its exact normalized, gauged coordinate by construction from a
certified decode; an informative decoder record or a container-verification
success alone does not establish this containment.

<a id="spec-52a-portable-spot-check-of-ball-against-the-exact-data"></a>
### 5.2a Portable spot check of BALL against the exact data

This check is **optional**, including for a full container verifier. If it is
performed and proves inconsistency, rejection is mandatory. It does not
replace the finalizer's mandatory all-coordinate construction and rounding
checks in §5.2.

**Identity.** For a ray with word `w`, let `v_j` be the decoded unnormalized coordinates (§3.5) and
`w_j` the published ones (§2). Since `w = v · conj(v_a)/(‖v‖·|v_a|)` multiplies every coordinate by
the same nonzero complex number, `w_j / w_k = v_j / v_k` for all `j, k` with `v_k ≠ 0`: the norm and
the anchor phase cancel, so a check needs neither all `d` coordinates nor the anchor.

**Cheap coordinates.** By the entry rule (§3.5), `m = 0` and every positive proper divisor `m | d`
have `word(m) = 0`, so `v_m = ι(σ_w(η_g))` (`g = gcd(m,d)`) needs only the ray word `w`
(no generator application at all for a ray of word 0). For recorded power-of-two data the
recommended pair is `j = 0`, `k = d/2`, whose large measured stabilizers are informative (§3.4).
In general use any two distinct such coordinates with a certified nonzero denominator;
`(0,1)` is always an available index pair, with no nonzero-value guarantee.

**Procedure.** (1) Decode `v_j` and `v_k` as certified complex balls (§3.5, §3.6); `v_k` must not
contain 0 (else refine or pick another coordinate). (2) Load `w_j`, `w_k` from BALL; `w_k` must not
contain 0. (3) Form `ρ = v_j/v_k` and `β = w_j/w_k` in ball arithmetic. (4) Require that `ρ` and `β`
overlap in both the real and the imaginary part; disjoint balls prove that BALL is inconsistent with
the exact data, and the file MUST then be rejected. The check tests consistency of one ratio only;
overlap proves neither exact equality nor containment. It does not check the exact norm, phase or
other coordinates; BALL-only gauge/norm checks remain as in §5.2. (The second reader ran it with
`(0, d/2)` on ray `c0` at `d = 256 … 1024`; see
[reproducibility.md](reproduce.md#reproducing-the-published-results).)

**Cost of the anchor.** The anchor `a` (RAYS) defines the gauge but is generally not a cheap
coordinate: unless `a` is 0 or a positive proper divisor of `d`, its entry word `Σ_j a_j W_j` has
digits up to the Smith factors (up to 5,120 at `d = 1024`), and a decoder applying generators one at
a time needs that many applications, each evaluating the action elements of every level — thousands
at `d ≥ 256`. Spot checks SHOULD therefore use ratios as above, not the anchor. The full decode of
the finalizer pays this cost once, sharing word prefixes in one DAG (§11).

<a id="spec-53-prec-and-prov-optional"></a>
### 5.3 PREC and PROV (optional)

Both are canonical JSON objects. PREC is in the finalization part; its only
permitted keys are `branch_bits`, `base_path_bits`, `delta_bits` and
`max_element_bits`, each an optional nonnegative integer string of at most
18 digits. All are hints. A decoder MUST NOT use a hint instead of a rigorous
radius, isolation or rounding test; disagreement with derived statistics does
not change the exact interpretation.

PROV is in the exact prefix, but is **outside the exact root**. It may contain
free canonical JSON describing source runs, software, source file hashes,
recognition checks and exact candidate-generator transformations. A reader MUST
NOT need it to interpret any element, branch, entry or ray. In particular the
2-adic rule and shifts belong only to EXMT. PROV MUST NOT contain a
top-level key named `generator_rescaling`, unconditionally. Nested objects
may use that spelling as provenance, with no decoding or rescaling meaning.
No timestamp, path or nondeterministic build detail is introduced by the canonical profile;
if provenance is supplied, its exact bytes are an input to file reproducibility.

<a id="spec-54-physical-chunks-and-codec-registry"></a>
### 5.4 Physical chunks and codec registry

A TOWR/BODY decoded payload is the **concatenation of the complete canonical
logical leaf bytes of §4.4**, with no separator, padding, length table or
outer element header. The varints and runs make each leaf self-delimiting:
consume its three header varints, then runs covering exactly `count`
coordinates, then the next leaf. Never merge runs across a logical boundary.
PDIR gives the number and range of leaves. A physical chunk MUST contain one
or more consecutive leaves of exactly one element; no leaf is split or
repeated. Its type, element, coordinate span and leaf span MUST agree with
LDIR and EXMT. Every element leaf is covered by exactly one physical chunk.

EXMT is one physical chunk whose payload is exactly its canonical JSON bytes;
it covers LDIR leaf 0. PROV, RAYS, BALL and PREC carry
no logical leaves. Their decoded payload is their ordinary JSON/binary content.
LDIR and PDIR are always uncompressed and are not listed in PDIR.

| codec id (`u32`) | name | meaning |
| --- | --- | --- |
| 0 | `raw` | encoded payload equals decoded payload byte for byte; lengths MUST agree |
| 1–2^32−1 | reserved | not valid in 1.0; requires a future definition and a declared physical `required_features` bit (§5.0) |

All 1.0 data chunks use codec 0, including EXMT, optional and finalization
chunks. Unknown codecs MUST be rejected before decoding or allocating a
decoded buffer; flags 0 do not authorize an unknown codec. Codec ids and
physical grouping are authenticated in PDIR; neither enters the exact root.

**Canonical physical profile `sicx-raw/1.0`.** The target is
`PHYSICAL_CHUNK_TARGET_BYTES` from the Constants table. For each element
independently, take its next leaf and greedily add consecutive leaves while the sum of their canonical
byte lengths is at most the target. Close the chunk before the first leaf
that would exceed it; equality is included. A single leaf larger than the
target forms one chunk by itself. Length includes each leaf's varints and
runs, but excludes the 16-byte physical chunk header. Emit in canonical
order with codec raw. EXMT and each non-element chunk stay one chunk each.
The profile supplies no extra chunk or padding.

The profile name and target are not stored as independent claims: PDIR records
the resulting grouping and codec, and profile conformance is checked by
recomputing the greedy boundaries. Other groupings of consecutive leaves are
valid containers with a different file root and the same exact root. Fixed
canonical exact data, optional/finalization payload bytes and physical profile
give identical file bytes across scheduling, thread counts, resumes and
transport splitting. Changing BALL precision, RAYS, PREC or PROV changes those
inputs. This is not a claim that all numerically valid enclosures have the
same bytes.

<a id="spec-55-logical-directory-ldir-and-leaf-hashes"></a>
### 5.5 Logical directory (LDIR) and leaf hashes

LDIR has the ordinary 16-byte chunk header (flags 1). Its payload is:

```
u64 logical_count                       1..2^24
logical_entry[logical_count]             64 bytes each

logical_entry:
  byte  0  kind[4]                       "EXMT", "TOWR" or "BODY"
  byte  4  u32 reserved = 0
  byte  8  u64 element
  byte 16  u64 first
  byte 24  u64 count
  byte 32  logical_sha256[32]
```

Payload length MUST equal `8 + 64·logical_count`. Entry 0 is exactly
`(EXMT, 0, element=0, first=0, count=1, hash)`. Its canonical logical bytes
are EXMT's JSON payload, excluding the physical chunk header. Subsequent
entries are exactly the element leaves of §4.3, in §4.1 element order and
increasing `first`, with the kind TOWR/BODY as appropriate. There are no
PROV, finalization or directory leaves. `count` is a positive coordinate
count for elements and the fixed marker 1 for EXMT, not a byte length.

For any leaf, let `C` be its canonical logical bytes (including the three
varints for TOWR/BODY), and `kind`, `element`, `first`, `count` be its entry
fields. The digest is exactly:

```
logical_sha256 = SHA256(
    53 49 43 58 31 2d 4c 45 41 46 00      # ASCII "SICX1-LEAF" then NUL
    || kind[4]
    || LE64(element) || LE64(first) || LE64(count)
    || LE64(byte_length(C)) || C)
```

The reserved field is not in this digest (it is fixed zero and included in
the tree entry below). Domain strings are literal bytes with exactly the one
shown NUL. A raw chunk of several leaves has one encoded-payload digest in
PDIR and a separate domain-separated digest for **each** constituent leaf.
No whole-chunk digest may replace those logical digests.

<a id="spec-56-physical-directory-pdir"></a>
### 5.6 Physical directory (PDIR)

PDIR has flags 1 and follows LDIR immediately. Its uncompressed payload is:

```
u64 physical_count                      1..2^24
physical_entry[physical_count]           112 bytes each

physical_entry:
  byte   0  type[4]
  byte   4  u32 flags
  byte   8  u64 offset                    physical chunk header offset
  byte  16  u64 encoded_length            payload only, excludes header
  byte  24  u32 codec                     0 in 1.0
  byte  28  u32 reserved = 0
  byte  32  u64 decoded_length            payload after codec decoding
  byte  40  u64 first_leaf                zero-based LDIR index
  byte  48  u64 leaf_count                number of LDIR entries covered
  byte  56  u64 object
  byte  64  u64 first
  byte  72  u64 count
  byte  80  payload_sha256[32]            SHA256(encoded_payload), no header
```

Payload length MUST equal `8 + 112·physical_count`. There is one entry per
physical chunk preceding LDIR, in file order, and no other entry. Each entry's
type/flags/encoded length MUST equal its physical header; a partial verifier
checks this equality when it reads that chunk and defers unread ordinary
headers (§6.2). The first offset is
16 and every next offset is the preceding offset + 16 + encoded length;
the final chunk ends exactly at the LDIR offset. PDIR is not self-indexed.

| type | `first_leaf`, `leaf_count` | `object`, `first`, `count` |
| --- | --- | --- |
| EXMT | `0`, `1` | `0`, `0`, `0` |
| TOWR/BODY | the consecutive LDIR range covered; `leaf_count ≥ 1` | element id, first coordinate sub-index, sum of leaf coordinate counts |
| BALL | `0`, `0` | ray array index, `0`, `d` |
| PROV/RAYS/PREC | `0`, `0` | `0`, `0`, `0` |

For TOWR/BODY, every covered entry MUST have the same kind and element id,
match its decoded logical leaf's header varints, and follow the logical
boundary rule. EXMT coverage instead follows its fixed singleton mapping
and JSON-byte rule (§5.5). Non-leaf
chunks cannot claim LDIR coverage. Every LDIR entry is covered exactly once.
A digest comparison MUST hash all encoded payload bytes.

<a id="spec-57-roots-and-footer"></a>
### 5.7 Roots and footer

For an ordered sequence of byte strings `S`, define the RFC 6962 tree:

```
T([])       = SHA256(empty byte string)
T([s])      = SHA256(00 || s)
T(S), n > 1 = SHA256(01 || T(S[0:k]) || T(S[k:n]))
              k = largest power of two strictly less than n
```

`00` and `01` are single bytes. There is no padding, duplicated last leaf,
length prefix or hex encoding inside this tree. See
[RFC 6962 §2.1](https://www.rfc-editor.org/rfc/rfc6962.html#section-2.1).

Let `E_i` be the exact 64 bytes of LDIR entry `i`, `P_i` the exact 112 bytes
of PDIR entry `i`, and `LDIR_bytes` the **entire LDIR chunk**, its fixed
16-byte header followed by its payload. Then:

```
exact_root = T([E_0, …, E_(logical_count−1)])
ldir_hash  = SHA256(LDIR_bytes)
D          = 53 49 43 58 31 2d 4c 44 49 52 00 || ldir_hash
             # ASCII "SICX1-LDIR" then NUL, then 32 digest bytes
file_root  = T([P_0, …, P_(physical_count−1), D])
```

The final file-tree item is **not** a PDIR entry. Its RFC leaf hash is
`SHA256(00 || D)`. The directory count words are not tree entries; their
values are checked against lengths and canonical coverage.

The exact root binds the canonical EXMT bytes, all exact coordinate leaves,
logical kinds, ids, spans and order. Thus it binds the tower, rescaling,
Galois actions and entry words, representatives and base embedding. It
excludes offsets, physical headers, chunk grouping, codecs, PROV, RAYS,
BALL, PREC and transport. Different choices in
those excluded fields preserve it. Different exact presentations can have
different exact roots even when their decoded vectors are equivalent.

The file root binds all PDIR entry bytes (including offsets, codecs, sizes,
flags, leaf mapping and encoded-payload digests) and all LDIR chunk bytes.
It therefore binds every ordinary physical payload, including PROV and
finalization, when those payload digests are checked. It does not directly
hash the file header, data chunk headers, PDIR envelope or footer. In 1.0
these are fixed or redundant and MUST be validated against the authenticated
entries and actual geometry. A whole-file SHA-256 is a separate identity
for every byte; partial verification does not establish that whole-file hash.
Exact-only files generally have **different** exact and file roots.

The footer is exactly **112 bytes**:

```
byte   0  u64 ldir_offset                offset of LDIR chunk header
byte   8  u64 ldir_length                LDIR payload length, excludes header
byte  16  u64 pdir_offset                offset of PDIR chunk header
byte  24  u64 pdir_length                PDIR payload length, excludes header
byte  32  exact_root[32]
byte  64  file_root[32]
byte  96  u64 file_length               includes header, directories, footer
byte 104  magic[8] = 53 49 43 58 45 4e 44 1a  # "SICXEND" then 1a
```

Require `pdir_offset = ldir_offset + 16 + ldir_length` and
`file_length = pdir_offset + 16 + pdir_length + 112 = actual source size`.
All ranges must be in bounds and disjoint as prescribed. Neither footer nor
root hex text is appended to any tree input.

<a id="58"></a>
<a id="spec-58-two-stage-finalization-checkpoints-and-crash-safety"></a>
### 5.8 Two-stage finalization, checkpoints and crash safety

Stage 1 writes EXMT, all TOWR/BODY chunks, optional PROV, LDIR, PDIR and
footer. Every producer, **including d2**, emits this exact-only file. A
streaming writer must determine descriptors, greedy rescaling and canonical
branches before publishing EXMT; private exact spools or a prepass are
allowed. Their storage representation is not the SICX format.

Stage 2 (the decoder's finalization) verifies stage 1 and binds its certified
decode and outward BALL construction (§5.2) to that exact root. It replaces the **tail starting at the
old LDIR offset** with RAYS, BALL chunks, optional PREC, then new LDIR, PDIR
and footer. The exact-prefix bytes and their PDIR entries MUST stay unchanged;
LDIR bytes and the exact root MUST be identical. The file root, file length,
whole-file hash and transport manifest MUST be recomputed. "Append-only"
means immutable exact-prefix data plus added finalization; it does **not**
mean appending after an old footer or retaining duplicate directories. A new
ray selection on an already finalized input is a new snapshot produced by
replacing its finalization tail with the same invariants, not by appending
a second RAYS. Published snapshots MUST NOT be mutated in place.

Before replacing a public output, a finalizer MUST have checked every ray
as in §5.2 and compared the exact root of the bytes being finalized with the
root used for its decode. A changed input MUST fail closed. Work may be done
in a new private file (copy/reflink plus new tail), or by quarantining the
input under a private work name before changing its tail. The verified final
name MUST never refer to a torn or partially finalized file. Commit requires
successful full container verification, file sync, atomic rename on the same
filesystem, and directory sync; every failed I/O/sync prevents commit. A
private in-place method that sacrifices the old tail MUST durably save enough
checkpoint data to restore a valid exact-only tail or resume deterministically.
An implementation wishing to preserve availability of the old snapshot must
use a separate private file rather than quarantine it.

A durable checkpoint MUST bind the exact root (when available), EXMT and the
intended ordered chunk plan, physical profile, finalization inputs and decoder
source identity. Before EXMT/root completion it instead binds the complete
canonical input/plan digest; it MUST NOT claim a finished exact root. Only
complete chunks can be marked committed: sync chunk bytes first, then append
and sync a length-delimited checksummed journal record with its entry and
payload digest. On restart, discard torn records, rehash every retained chunk
and revalidate its header/plan/leaf hashes before reuse; truncate at the last
verified contiguous chunk. Retained chunks and newly generated chunks MUST
match the deterministic plan. Pending fragments are not committed data. An
interrupted private file without complete directories/footer is a checkpoint,
not a readable SICX file. Journal formats are implementation-private; the
above observable durability and rejection rules are required.

<a id="spec-6-verification-and-reader-rejection"></a>
## 6. Verification and reader rejection



<a id="spec-61-full-verification"></a>
### 6.1 Full verification

A full verifier MUST perform all of these checks:

1. Read the 16-byte header and last 112 bytes, validate version, features,
   magic, size, directory geometry and resource bounds before allocation.
2. Read complete LDIR/PDIR chunks. Check their headers, lengths/counts, fixed fields,
   contiguity, offsets, type/flag/codec rules, canonical order, and both tree
   roots by §5.7. Check that each logical entry has exactly one allowed
   physical owner, with no missing, overlapping, duplicate or extra coverage.
3. Hash and decode EXMT. Check its logical hash, JSON, exact keys, bounds,
   degrees, Smith product, entry rule, derived divisor positions/generator order,
   word counts/residues/order relations (§5.1), descriptors and rescaling record.
   Derive the entire logical layout from it and compare with LDIR/PDIR.
4. Read **every** physical chunk, check header equality and encoded SHA-256,
   exact decoded length, and no trailing/unused decoded bytes. Parse each
   logical leaf, enforce §4.4, check its logical hash and re-encoding identity.
   For rational `K`, reject any nonzero second integer component (§4.4).
   Compute each element's attained `max_bits`, gcd/primitivity and minimal
   level set, including §4.2's zero-element convention. Check §3.7 by undoing
   the recorded rescaling, reapplying the greedy rule, and testing the stored
   data's residual greedy shifts are all zero. Check canonical branch strings,
   exact root selection and §5.1b's precision/minimum rule.
5. Parse RAYS, PROV and PREC if present. Check all JSON/key/order constraints,
   BALL counts/lengths, canonical parts, gauge, norm enclosure and tight
   accuracy (§5.2).
6. If an expected exact root, file root or whole-file SHA-256 was supplied,
   compare it exactly and reject any mismatch. Report separately which roots
   were trusted and which were merely recomputed. Compute whole-file SHA-256
   only by reading the whole file. Rechecking SIC identities or BALL against
   exact decoding is a separate mathematical/numerical check (§1, §11).

This establishes **container** conformance and integrity; authentication
additionally requires independently trusted identities (§6.2). The explicit
algebraic root checks cover simple nonzero roots at the chosen base embeddings,
exact root identification (including singleton components), and canonical
rectangles with the minimum common precision. They do **not** certify
irreducibility, extension degrees, the declared Galois group, action
identities/commutativity, or representative kernel fixity (§3.5). The checked
word-order congruences define a homomorphism into the declared Smith group;
they alone do not certify an action on the stored field elements.
Those field/group/entry premises require a separate
algebraic verifier such as the CRT certificate; a successful certificate
must identify the same exact root. Container success alone also establishes
neither BALL-to-exact containment/overlap nor binary64 agreement or the SIC
property. Inability to finish a required root identification MUST be reported
as refusal, not a passed check.

<a id="spec-62-partial-verification"></a>
### 6.2 Partial verification

A partial reader MUST first read the whole 16-byte file header, 112-byte
footer and complete LDIR/PDIR chunks (including their headers), and perform
steps 1–2 on those bytes and the directory entries. This directory bootstrap
does **not** read every ordinary chunk header. Validate offsets, declared
lengths, contiguity, types/flags/codecs, ownership and canonical directory
order from PDIR; check each ordinary header against its entry only when that
chunk is read. Next read/authenticate EXMT (including its physical header)
as in step 3 and validate the derived logical/physical coverage.

For an unread finalization part, directory validation checks only the
`RAYS BALL+ PREC?` skeleton of §5.0, BALL array indices `0,1,…`, and known
BALL spans `first=0,count=d`. It cannot yet equate the BALL count with the
unread RAYS array length or derive BALL width from RAYS `limbs`. Defer ray
count, labels/order, words, anchors, widths, accuracy and gauge/norm checks
until their required payloads are read. Before exposing any BALL record,
read and validate RAYS and check its array length against the directory's
BALL count and its width against every declared BALL length; validate the
consumed BALL payload's parts/gauge/norm/accuracy. Reading RAYS does not
require reading unused BALL payloads. PROV/PREC JSON checks are likewise
deferred until those payloads are read.

Recompute both roots and establish trust before describing content as
authenticated. Exact-root trust may come from any of:

- matching the recomputed exact root to an independently trusted catalogue
  or certificate;
- matching the recomputed file root to an independently trusted file root,
  after verifying its §5.7 binding to the complete LDIR chunk; that LDIR
  makes its recomputed exact root trusted transitively;
- reading and hashing **all** bytes of the pinned snapshot to match an
  independently trusted whole-file SHA-256, then validating the directories
  and recomputing the roots from those same bytes. This makes both roots
  trusted transitively; a partial fetch alone cannot establish this hash.

Finalization/provenance/physical-layout authentication requires the second
or third route (file-level trust). A trusted exact root alone does not
authenticate them. A footer or untrusted manifest from the same server is
not an independent trust source. If any supplied expected identity
mismatches, reject even when another identity matches. Report each identity
as directly matched, transitively trusted (naming the route), or merely
recomputed; trust does not waive content/semantic checks or verify unread
bytes. A previously verified whole-file digest may be reused only for the
same stable snapshot, never as an assertion about unchecked replacement bytes.

For each requested physical chunk: check its header and encoded SHA-256,
decode exactly the declared length, parse all its logical leaves and compare
each domain-separated logical digest with LDIR. Only then may its exact
values be used. With only a trusted exact root, leaf content is authenticated
but PDIR and finalization remain self-consistent, untrusted routing data;
required logical dependencies MUST be derived from EXMT, not an untrusted
range list. A chunk with any failed check MUST NOT be partly consumed.

Read all leaves of an element before claiming its primitivity, minimality or attained bit bound.
Rescaling and canonical branch checks may be deferred until all their exact dependencies are
consumed; if all exact elements have been read, these global exact-data checks MUST be completed
even in partial mode. Unread ordinary headers, optional/finalization payloads, deferred
RAYS-dependent checks and element-wide canonicality remain unchecked; a partial result MUST state
its scope, not claim full-file verification. A stored digest field is not proof the unread payload
exists. A missing required chunk is an error, never zero data.

Both directories are fetched once and can be cached. No per-access Merkle
audit path, internal-node table or published sidecar is required. An entry
alone cannot yield its missing siblings from this file; an external RFC 6962
proof provider would be a separate protocol and would still not prove a
dependency list complete. Single-chunk reads use the authenticated complete
directories, not a new trust anchor.

<a id="spec-63-rejection-and-resource-limits"></a>
### 6.3 Rejection and resource limits

Readers MUST reject every violated structural, range, encoding, order,
coverage, digest or mandatory semantic constraint above when the relevant
bytes are read. This includes every undefined chunk type regardless of flags,
unknown EXMT/RAYS keys, undefined META/INDX chunks,
non-canonical decimal/JSON/Arb strings, wrong element kinds or field levels,
missing/extra actions or representatives, a dimension outside `2…2^20`, a wrong
divisor-position list or entry-rule name, wrong generator-word count/rank,
word residues out of range or failed generator-order congruences, Smith
factors < 2/not dividing D or wrong product, duplicate/noncanonical ray labels,
wrong anchors or ball widths, bad varints/negative zero/nonmaximal runs,
coordinate overrun/underrun, nonzero rational-field second components,
bytes after a leaf/chunk, leaves above `max_bits`,
and unsupported codecs/features even on optional chunks. TOWR/BODY headers
inside the logical bytes MUST agree with both directories. A leaf's zero
pattern MUST be tested in the original `(x,y)` basis after undoing the pair
transform, not in `(r,y)`. Arithmetic overflow is always a rejection.

Implementation resource limits MUST be checked before allocation or expansion.
The reference defaults are: integer magnitude at most `2^20` bytes; any JSON
chunk at most 64 MiB; decoded logical leaf and decoded physical chunk at most
256 MiB; at most `2^24` entries in each directory; JSON nesting at most 32;
integer strings at most 18 digits except `scale`; no unbounded recursion
from input. These are resource refusals and may be raised explicitly, not
silently bypassed. Declared lengths MUST NOT force whole-file allocation.
A partial reader can defer payload-wide checks, but never a failed check on
bytes it actually uses. Precision/resource refusal MUST be distinguishable
from successful verification and from proved algebraic inconsistency.

<a id="spec-7-canonical-json-all-json-in-sicx-and-its-text-forms"></a>
## 7. Canonical JSON (all JSON in SICX and its text forms)

UTF-8; no whitespace; object keys strictly increasing in the byte order of their decoded, unescaped
UTF-8 strings; **every number is a decimal string** (`"0"`, `"-12"`, no leading zeros, no `"-0"`);
no bare numbers, `true`, `false` or `null`; strings use only the escapes `\"`, `\\` and `\u00XX`
(lowercase hex) for bytes below 0x20. Identical content gives identical bytes. Readers reject
anything else. Keys MUST be unique; strings MUST be valid UTF-8 with no surrogate scalar values.
Characters at or above U+0020 other than quote/backslash are emitted literally (no optional Unicode
escapes or slash escape). Bytes below 0x20 use exactly `\u00xx`, with lowercase hex, including LF
and TAB. Numeric strings are checked by their field rules; other strings may be ordinary labels or
prose. Arrays retain their specified order.

<a id="spec-7a-arb-dump-form-ball-strings"></a>
## 7a. Arb dump form (ball strings)

The branch rectangles of EXMT (`tower.branches[i]`, each part a string) and the BALL parts written
as text (§5.2; `sic-vector-balls/1.0` source files) use the **Arb dump form** of one real ball. It
is defined here completely; it coincides with FLINT/Arb's `arb_dump_str` output for finite balls,
but a reader needs no FLINT.

```
ball   := M " " E " " m " " e          (exactly four fields, single ASCII spaces, nothing else)
M, E, e := hexint
m      := hexnat
hexnat := "0" | [1-9a-f] [0-9a-f]*
hexint := hexnat | "-" [1-9a-f] [0-9a-f]*
```

Every field is a base-16 integer in lowercase, with no leading zeros, no `+`, no `0x`, and no `-0`;
**exponents are hexadecimal too** (`-1fa` = −506). The ball is the real interval
`[M·2^E − m·2^e, M·2^E + m·2^e]`: midpoint `M·2^E`, radius `m·2^e`. **Canonical form** (required):

- `M = 0` or `M` odd; `M = 0` implies `E = 0`;
- `m = 0` or (`m` odd and `m < 2^30`); `m = 0` implies `e = 0`.

(The radius bound is Arb's 30-bit radius mantissa: a radius with a longer odd mantissa is not
exactly representable, and a non-canonical string does not survive `arb_load_str`/`arb_dump_str`,
which is how the reference verifier checks this form.) The zero ball is `0 0 0 0`. Arb's special
values (infinities, NaN) are dumped as `M = 0` with a nonzero `E` or `e` and are therefore excluded.
For BALL parts additionally `|E|, |e| ≤ 2^40` and `|M| < 2^(64·limbs)` (§5.2). A branch rectangle is
the pair `[re, im]` of two such strings, each finite and canonical; verifiers check this.

For example, the exact singleton `i` has branch `["0 0 0 0","1 0 0 0"]` (§9b). Non-dyadic roots have
rectangles prescribed by §5.1b; arbitrary working-precision dump strings are not canonical branch
choices. In the text form (§8) BALL parts are written as four **decimal** strings `[M, E, m, e]`
with the same values.

<a id="spec-8-canonical-text-form-json-lines-sicx-text-json10"></a>
## 8. Canonical text form: JSON Lines `sicx-text-json/1.0`

A lossless streamable view, one canonical JSON object per line, each terminated
by one LF byte. It is a serialization view, not a remote index. The schema at
`docs/schemas/sicx-text-json-1.schema.json` describes the final layout;
the rules below define the final form completely.

1. Header, exactly
   `{"content_hash":"sha256","required_features":[],"schema":"sicx-text-json/1.0","sicx_version":["1","0"]}`.
2. One line per physical chunk in binary file order, excluding LDIR and PDIR:
   - EXMT: exactly `{"chunk":"EXMT","flags":"1","meta":{…}}`, with the full §5.1 object.
   - TOWR or BODY: exactly `chunk`, `codec` (`"0"`), `flags` (`"1"`) and
     `leaves` (nonempty array). Each leaf object has exactly `count`, `element`,
     `first`, `runs`. `runs` is an array of `["zeros","n"]` or
     `["pairs",[["x","y"],…]]`, in the original `(x,y)` basis, not transformed
     `(r,y)`. The leaf boundaries and maximal run rules are §4.3–4.4.
     Element kind/level/axis/position are derived from EXMT, not repeated.
     One line preserves one physical grouping; the same element may occupy
     several lines.
   - RAYS: exactly `chunk: "RAYS"`, `flags: "1"`, `json` (the §5.1a object).
   - BALL: exactly `chunk: "BALL"`, `flags: "1"`, `label` (matching RAYS),
     `ray` (array index), `values` (length d). Each value is
     `[[M,E,m,e],[M,E,m,e]]`, real then imaginary, all decimal strings;
     midpoint sign is folded into `M`, no negative zero. RAYS `limbs` fixes
     binary width, including leading zero limbs.
   - PREC/PROV: exactly `chunk`, `flags: "0"`, `json` (their object).
3. Final line: exactly `content_sha256`, `exact_root`, `lines`, `merkle_root`,
   `sicx_bytes`, `sicx_sha256`. The content digest hashes every preceding
   byte, including each LF; `lines` counts preceding lines (including the
   header, excluding the final line). `merkle_root` is the **file root**,
   retained as this external field spelling; `exact_root` is §5.7's root.
   `sicx_bytes` and `sicx_sha256` are the binary file's length and digest.

Arrays remain JSON arrays; their numeric values are decimal strings as in §7; the lists above give
exact key sets. Binary reconstruction encodes each leaf independently, concatenates leaves per
physical line, reconstructs both directories and footer, performs full verification, and checks all
final-line values. Rendering the rebuilt file MUST reproduce the same text bytes. Thus both
directions are byte identities, even for a noncanonical physical grouping. Undefined chunk types,
extra keys, bad flags/codec, missing LF, empty or nonmaximal runs and inconsistent final values MUST
be rejected. The final schema has no META/INDX lines or stored directory/offset lines.
`sic-container from-text` performs reconstruction.

Single-chunk rendering can use the complete authenticated directories plus
that chunk (§6.2); a standalone line has no implied root authentication. Full
text is shipped only for `d ≤ 64`; larger text is generated on demand.
Recorded measurements found text about 4.1 times binary size at d512 maximal;
that is not a bound for the final layout. Text splitting occurs at line
boundaries into `NAME.part-NNNNN.jsonl` plus `NAME.parts.json`: exactly
`{"parts":[{"bytes","lines","name","sha256"}],"schema":"sicx-text-json-parts/1.0"}`
(with actual values, canonical keys). `bytes` and `lines` count bytes/LFs in
that part; `name` is its basename; `sha256` hashes those bytes. Concatenating
parts in list order yields the exact single-file text. The binary transport
manifest (§10b) is separate. Both directions can stream within a bounded
window of complete physical chunks.

<a id="spec-9-ray-files-sic-fiducial-json10-cjson"></a>
## 9. Ray files: `sic-fiducial-json/1.0` (`c<N>.json`)

One canonical JSON document **per ray**, `d<d>/m<N>/c<N>.json`, generated from a verified SICX file
by `sic-inspect FILE --json-rays DIR` or `--release-layout ROOT` (`sic::sicx::ray_json`); schema
`docs/schemas/sic-fiducial-json-1.schema.json`. `c<N>` is the strict RAYS label of §5.1a; unknown
labels MUST fail rather than map to `c0`. A caller MUST supply a multiplet label for every ray
export; the file carries the full id `d<d>-m<N>-c<N>` (e.g. `d1024-m1-c12`; `m<N>` is assigned by
the release, §10a, and is not stored in SICX). Bare ids `d<d>-c<N>` are invalid. Keys: `schema`,
`required_features`, `d`, `multiplet`, `id`, `index` (position in RAYS `rays`), `galois_element`
(`smith`, `exponents`), `labels` (catalogue labels, `anchor`, `sicx_ray_label`), `sicx` (`name`,
`sha256`, `merkle_root`, the §5.7 file root), `exact` (`file`: `exact.json`, `ray`: the id; only
when `exact.json` exists) and the levels:

The exact-value guarantees below assume BALL contains the exact normalized,
gauged coordinates, as ensured by conforming certified finalization (§5.2).
Container verification or a RAYS decoder record alone does not establish this
premise. An exporter may independently establish containment by certifying
that a fresh exact decode's normalized/gauged enclosures are contained in
the existing BALL intervals for the same snapshot; overlap is insufficient.
This specification defines no additional finalizer-evidence object or workflow.
No external publication-ball file is an input to finalization or needed for these exports.

- `fp32`, `fp64`: `values` `[[hex_re, hex_im], …]` (IEEE-754 bit patterns, 8 or 16 hex digits),
  `decimal` (9 or 17 significant digits, `%.8e` / `%.16e`, round-tripping), `error_bound` (a power
  of two `0x1p±N` above every `|value − exact|`), `rounding`. **Generation certifies** that each
  value is the correct rounding (nearest, ties to even) of the exact coordinate: both ends of the
  BALL ball must round to the same value, else generation fails. `fp32` is rounded from the ball,
  never from `fp64`; binary32 values must be normal or zero.
- `hp`: `decimal_places` `P`, `accuracy_bits` (every decimal radius < 2^−accuracy_bits), `values`
  `[[mid_re, rad_re, mid_im, rad_im], …]` with exactly `P` decimals; each decimal ball contains the
  BALL record (outward rounding: midpoint to nearest, radius `⌈rad·10^P⌉ + 1` units), hence the
  exact coordinate enclosed by the finalizer's certified decode. Overlap with a separate enclosure
  would not imply containment.

Ordering, normalization, phase and the WH convention are those of §2 and are fixed by the schema version.

<a id="spec-9a-exactjson-power-basis-of-a-multiplet-sic-exact-json10"></a>
## 9a. `exact.json`: power basis of a multiplet (`sic-exact-json/1.0`)

One document per multiplet, generated **on demand** (not stored in git;
`build/data/d<d>/m<N>/exact.json` by the `exact-data` build target), only where the power basis is
feasible (measured, d ≤ 16; [reproducibility.md](reproduce.md#reproducing-the-published-results)).
Generated by `sic-inspect --exact-json IN.sicx OUT.json --multiplet mN` (output via `OUT.partial`
and rename; memory is the document, 28 MB peak RSS at d = 16) (`sic::sicx::exact::DocumentBuilder`,
`cpp/include/sic/sicx/exact.hpp`) from a **finalized** file; schema
`docs/schemas/sic-exact-json-1.schema.json`. Every number is a decimal string.

- `field`: derived `D0`, `variable` (`omega`), `polynomial` (infix) and low-to-high
  `polynomial_coefficients`: `[c0,c1,1]` of `ω²+c1 ω+c0` for quadratic `K`, or `[0,1]`
  of `ω = 0` for rational `K`; `embedding` is §2's embedding. For the quadratic bases,
  `(c0,c1) = ((1−D0)/4,−1)` if `D0 ≡ 1 (mod 4)`, and `(−D0,0)` otherwise.
  `extension_order`: `omega, g0, g1, …`; each variable is adjoined to the field
  generated by the previous ones (`F_{i+1} = F_i(g_i)`); rational `omega = 0` is a placeholder,
  not an extension. This derived text export does not add conventions to EXMT.
- **Element** of `F_n`: `{denominator, coefficients}`; `coefficients[e] = [x, y]` is the coefficient
  `(x + yω)/denominator` of the monomial `g0^{e0} g1^{e1} …`, `e = e0 + p0 e1 + p0 p1 e2 + …` (the
  monomial basis of §3.1; length `N_n`). The leaves of §3.2 are converted exactly: `x = Σ_k X_k
  b_{i,k}` level by level, `b_{i,k} = q_k(g_i)/f'(g_i)`, with `1/f'(g_i)` computed in `F_{i+1}`
  (closed form for quadratic levels).
- `tower[i]`: `variable`, `degree`, `polynomial` (monic, low to high, coefficients elements of
  `F_i`; for a quadratic level `g_i² − c_i` with `c_i` the `wsq` element) and `polynomial_infix`
  (GP/Sage/Mathematica syntax in `omega, g0, …`); `root`: decimal boxes `[mid, radius]` for `re` and
  `im`; `branch`: the EXMT rectangle.
- **Root selection by isolating ball.** The box is certified at generation by interval Newton: with
  the lower levels at their published boxes, `N = x0 − f(x0)/f'(B)` lies inside the box `B`, so `B`
  contains exactly one root of the level polynomial, and `B` overlaps the EXMT branch rectangle.
  `ι(g_i)` is that root.
- `rays`: keyed by the ray id `d<d>-m<N>-c<N>` (a multiplet label MUST be supplied; the conductor is
  not stored; top-level `multiplet` is required); `galois_element` (Smith factors, exponents) and
  the `d` unnormalized coordinates `ψ_m = ι(σ_{w+word(m)} η_g)`, `g = gcd(m,d)` (§3.5), as elements
  of `L`. The Galois transports are applied exactly in the power basis (the action records become
  ring endomorphisms `g_i ↦ τ_l(g_i)`, composed word by word).
- `conjugation`: complex conjugation as an exact ring automorphism of `L` (images of `ω` and of
  every `g_i`), with its Smith word when it fixes `K`. It is identified by ball arithmetic (exactly
  one Smith word maps every root box to the conjugate box) and its images are checked to satisfy
  every relation. It is stored because the exact SIC test uses `conj(ψ_k)`.
- **Generation checks (fail-closed):** root isolation; the conjugation identification; and a
  numerical check that the power-basis coordinates, evaluated at the published root boxes with
  768-bit balls, normalized and gauged as in §2, overlap the BALL record of every coordinate of
  every ray. The check is a consistency check of the conversion against the file, not a proof of the
  SIC property.
- **GP reference script** `cpp/checkers/gp/sicx_exact_verify.gp` (independent of the C++ code;
  PARI/GP ≥ 2.13, tested with 2.13.3; it uses only GP language features, so it also runs in Sage's
  bundled GP, untested here). It parses the file with its own JSON reader, rebuilds `L` with nested
  polmods, rebuilds every `ψ`, and verifies **exactly** that `ν = Σ ψ_k conj(ψ_k)` is
  conjugation-invariant and `|⟨ψ|D_p ψ⟩|² = 1/(d+1)` for every `p ≠ 0` (homogeneous autocorrelation
  form, no roots of unity), plus a 150-digit floating point check of the definition for all `p`.
  `SIC_SHIFTS="0,1"` restricts the autocorrelation shifts (partial check, reported as such). The
  positivity of `ν` and the identification of conjugation with complex conjugation use the embedding
  selected from the root boxes by floating point and are not certified by the script.

<a id="spec-9b-small-dimensions-informative-producer-example"></a>
## 9b. Small dimensions (informative producer example)

The base-field cases `d = 2` and `d = 3` introduce no special container,
entry-rule or finalization branch. At `d = 2`, `U_2` is trivial,
`unit_generator_words = []` and positions are `[0,1]`. At `d = 3`, `K = Q`,
`U_3` has generator `2` of order `2`, positions are `[0,1]`, and all pair
second components vanish. Its entries follow §3.5 (`m = 0,1` have word zero;
`m = 2` has the generator word), subject to the same premises. No `d = 3`
producer or SIC data is claimed here.

`d = 2` has `K = Q(√−3)`, `ω = (1 + √−3)/2`, `ι(ω) = (1 − i√3)/2`. Tower: one quadratic level `g_0²
= −1` (`ι(g_0) = i`, branch rectangle exactly `[0, 1]`), `L = Q(i, √3)`, Smith `[2]`, `τ_0: g_0 ↦
−g_0`; `unit_generator_words = []`. Fiducial class: `ψ = (1, z)` with `z = ψ_1/ψ_0 = (1+i)(√3−1)/2 =
−ω + (ω−1) g_0` (equal to `(1+i)β/α` for the radical record `α² = 1/2 + √3/6`, `β² = 1/4 − √3/12` of
`data/d2/d2.sicrad`). Elements: `wsq 0 = −1`, `act 0 0 = 1`, `rep 0 = η_2 = 1` (levels `[]`, scale
2, coordinate `(1,0)`), `rep 1 = η_1 = z` (levels `[0]`, scale 2, coordinates `(0,−1), (1,−1)`, i.e.
`λ = (−2ω, 2 − 2ω)`). One finalized ray `c0` (release id `d2-m1-c0`), word `[0]`, anchor 0. EXMT
`generator_rescaling.shifts` is `["0"]` and `tower.branch_bits` is `"128"`. The file is produced by
the native producer (`sic-construct 1 --out FILE.sicx --radicals d2.sicrad`), which emits
**exact-only**, just as at every dimension. It uses the explicit closed form of the tetrahedral
fiducial rather than the real-quadratic construction. The decoder then finalizes it using the same
certified full-coordinate checks and `{name, source_commit}` record as every other multiplet. BALL
is computed and checked from the exact leaves (the reference decoder `sic-decode` supports `D0 < 0`
and decodes `d = 2`). Complex conjugation does not fix `K` (`ω ↦ 1 − ω`), so
`conjugation.galois_element` is empty and its images are `ω ↦ 1 − ω`, `g_0 ↦ −g_0`. Fixtures
`cpp/tests/fixtures/sicx/d2.sicx`, `d2.jsonl` (canonical text form), `d2-c0.json`.

**Reader scope.** `d = 2` is in the scope of 1.0: container readers and verifiers MUST accept it; it
exercises the `D0 < 0` rules (§2 embedding with `√D0 = i√|D0|`, balanced pair rule of §4.4) and a
branch rectangle `["0 0 0 0", "1 0 0 0"]` (§7a). A decoder evaluates `ι(ω) = (1 − i√3)/2` and, for
`c_0 = −1` (real part certainly negative), takes `s = i·√1 = i` (§3.6). The reference decoder
`sic-decode` supports `D0 < 0` : its `d = 2` decode overlaps the BALL with identical certified
binary64 values (recorded goldens in `cpp/tests/decode/goldens/`). The `d = 2` BALL is derived
directly from the exact leaves, and admitted only after decoder finalization. Readers imposing a
power-of-two or `d ≥ 4` restriction are not conforming to this general-d 1.0 specification.

An optional all-in-one export for small `d` (`sic-inspect FILE --json-all OUT --multiplet mN`, schema id
`sic-fiducial-json-all/1.0`: `{"rays":[ray documents]}`) is not a release file.

<a id="spec-10a-release-tree-and-catalogue"></a>
## 10a. Release tree and catalogue

Release layout ([data-guide.md](data.md#data-guide)): numerical data and manifests in git, every
SICX a GitHub Release asset transported as parts (§10b,
[data-download.md](data.md#transport-getting-the-exact-data)). The conductor appears in no public
name.

Release production uses native Smith words and recorded anchors for the exact presentation being
finalized. It generates the current per-ray bindings in `data/catalogue.json` and the ray records.
Finalization consumes those words directly. A ray is published only with its checked exact root and
authenticated file association; current production bindings are not available merely from an
identification catalogue or an illustrative fixture. See [current ray
bindings](reproduce.md#current-ray-bindings).

```
data/catalogue.json
data/d<d>/all.npz, stabilizers.txt                         (other writers)
data/d<d>/m<N>/manifest.json, multiplet.sicx.parts.json, c<N>.json (+ c<N>.npy/.mat/.h5/.csv from other writers)
```

`m<N>` numbers follow increasing construction conductor within a dimension; N is given on the
command line from provenance (`--multiplet N`, or `--provenance-conductors LIST
--provenance-conductor C`) and is never read from the SICX. `sic-inspect FILE --release-layout ROOT
...` writes one multiplet folder from a verified `.sicx` (manifest, parts file, `c<N>.json` per ray;
`--assets-dir DIR` also writes the transport parts; no SICX, exact JSON or text form is placed in
the tree). `sic-inspect --catalogue ROOT` rebuilds `ROOT/catalogue.json` (identity
`sic-release-catalogue/1.0`, schema `docs/schemas/sic-release-catalogue-1.schema.json`, canonical
JSON, every number a string): per dimension its files; per multiplet its id `d<d>-m<N>`, labels,
Smith factors, files and SICX assets (SHA-256, size, Merkle roots); per ray its id `d<d>-m<N>-c<N>`,
labels, exponents and files. Trust chain: trusted catalogue/certificate → exact root → LDIR → EXMT
and canonical logical leaves. For physical/finalization content: trusted catalogue → file root →
PDIR and LDIR hash → encoded chunks → decoded leaves. The parts manifest additionally binds complete
asset and file SHA-256; partial reads do not establish those whole-file hashes. The external key
`merkle_root` in ray files and catalogue assets denotes the file root; `exact_root` denotes the
separate logical root wherever present. The release catalogue MUST record, for every released SICX
file, its exact root and its file root (or whole-file SHA-256), because trusting RAYS/BALL requires
file-level trust.

<a id="10b"></a>
<a id="spec-10b-transport-parts-remote-reads-and-cache-identities"></a>
## 10b. Transport parts, remote reads and cache identities



<a id="spec-10b1-parts-manifest-and-deterministic-splitting"></a>
### 10b.1 Parts manifest and deterministic splitting

The canonical object is one complete SICX file; parts are contiguous transport
ranges, not separate algebraic objects. The transport profile is
`sicx-transport-parts/1.0`.
It is canonical JSON with exactly:

| key | value |
| --- | --- |
| `schema` | `"sicx-transport-parts/1.0"` |
| `alignment` | `"sicx-chunks/1.0"` |
| `file` | `"multiplet.sicx"` |
| `size_bytes`, `sha256` | complete file length and whole-file SHA-256 |
| `exact_root`, `file_root` | §5.7 roots, each 64 lowercase hex digits |
| `part_size_bytes` | `"1900000000"`; a **ceiling**, not a stride |
| `release` | exactly `repository`, `tag`, both nonempty strings; repository `OWNER/REPOSITORY` |
| `parts` | ordered nonempty array, at most `2^24` entries |

Each part has exactly `index`, `asset`, `offset`, `size_bytes`, `sha256`.
Numbers are unsigned canonical decimal strings, fit `u64`; a part is at most
**1,900,000,000 bytes** (decimal B). `index` is its zero-based array position.
For release id `d<d>/m<N>`, asset is
`d<d>-m<N>.sicx.part-<index>`, with the index in decimal padded to at least
four digits (`0000`, …, `9999`, `10000`); no path separator or extra padding.
Each entry has positive size. Readers MUST check:

- `offset[0] = 0`; `offset[i+1] = offset[i] + size_bytes[i]`, with checked
  arithmetic, and final end equals whole `size_bytes`; no gaps or overlaps;
- every size is at most `part_size_bytes`; offsets are explicit, **never**
  inferred as `i·part_size_bytes`; asset names/indices are unique and correct;
- every interior cut is the end of an ordinary physical chunk, or the start
  of LDIR for an exact-only file; no chunk header, payload, LDIR, PDIR or
  footer is split across parts; the header stays with EXMT;
- the last part holds the whole suffix starting at RAYS for a finalized
  file, or LDIR for an exact-only file, through both directories and footer;
  no earlier part contains finalization data or directories;
- full concatenation has the declared size, whole-file SHA-256, roots and
  canonical container structure. A partial reader checks boundaries against
  the authenticated PDIR/geometry without claiming whole-part hashes.

Deterministic release splitting: if the whole file fits the ceiling, emit one
part. Otherwise reserve the suffix just defined as its own last part. Greedily
pack the prefix's complete ordinary chunks up to the ceiling (include the
header in the first pack), closing a part before the next chunk would exceed
it; equality is included. The final prefix pack is followed by the reserved
suffix part. If one indivisible chunk (with the header for EXMT), or the
reserved suffix itself, exceeds the ceiling, this transport profile cannot
represent the file: split MUST fail rather than cut it or emit an oversized
asset. Such a file can still be a valid local container; its producer must
choose smaller physical grouping or a smaller finalization selection for
release. This gives a small tail when finalization and directories are small;
there is no unmeasured universal tail-size bound.

The manifest MUST be generated after finalization and written last. A trusted
catalogue binds the release id, manifest identity, exact root, file root,
file size and whole-file hash; a manifest downloaded from an untrusted server
is not its own trust anchor. PROV and RAYS changes require a new file root,
whole-file SHA-256, parts hashes and manifest. Their exact root can stay fixed.

<a id="spec-10b2-full-fetch-and-repair"></a>
### 10b.2 Full fetch and repair

A full fetch checks the manifest before allocation, hashes every part before
writing its explicit range, checks the full-file SHA-256 and full container,
then syncs and atomically renames its private work file. Resume rehashes
retained complete ranges; repair replaces only bad parts under a private
name, then repeats the complete checks. A missing part, truncated body,
wrong digest or failed sync MUST prevent publication of the final name.
The public name MUST never expose a sparse or incomplete SICX file. Transport
disk admission includes the whole file and concurrent temporary parts.

<a id="spec-10b3-authenticated-http-range-reads"></a>
### 10b.3 Authenticated HTTP Range reads

Remote partial reading first pins a trusted source snapshot (manifest,
length and roots), fetches the file header, footer and complete directories,
then authenticates EXMT and follows §6.2. Unrequested ordinary chunk headers
are deferred; PDIR supplies their geometry. RAYS-dependent finalization checks
are deferred until the relevant payloads are read. A trusted file root with
its LDIR binding verified also establishes exact-root trust; a trusted
whole-file SHA-256 does so only after all snapshot bytes have been hashed.
For a logical interval `[a,b)`, intersect with each manifest
part `[o,o+s)`; request each nonempty intersection `[lo,hi)` as the inclusive
**part-local** `Range: bytes=(lo−o)-(hi−o−1)`. Bounds and arithmetic MUST be
checked. Coalescing adjacent chunks is permitted; authentication still checks
whole physical payloads and all logical leaves before use. No sub-chunk
slice can be authenticated by the chunk digest alone.

The Range client MUST send `Accept-Encoding: identity`, disable transparent
content decoding, and use one contiguous range per request. The final response
(after bounded HTTPS redirects and normal TLS verification) MUST be 206 with
exactly one valid `Content-Range: bytes start-end/total`, endpoints equal to
the request and total equal to that manifest part's size. Reject wildcard
totals, duplicate/conflicting ranges, multipart bodies, non-identity content
encoding, wrong lengths, overflows and extra bytes. Content-Length, if present,
MUST agree; body size MUST be enforced even without it. Header/body buffers
and redirect count MUST be bounded. Redirect/interim headers are not final
response headers. A **200 response MUST be rejected at its headers**, even
for a whole-part Range; it MUST NOT silently trigger a full download or fill
the cache. A full sequential fetch is a separate explicit strategy chosen
before requesting that transfer. These are stricter application requirements
than generic [HTTP range semantics](https://www.rfc-editor.org/rfc/rfc9110.html#name-range-requests).

A client may retry transient failures with bounded backoff; it MUST fail
closed on persistent digest errors or invalid Range semantics and MUST NOT
combine snapshots. Changed validators/manifest/roots cause a source-change
error or a fresh snapshot bootstrap. Strong ETags can supplement hashes, not
replace them. Signed redirect URLs are temporary locators, not identities;
credentials MUST NOT be forwarded to a different origin. Loopback HTTP is
allowed only in explicit tests. Remote transport MUST NOT allocate the whole
file in RAM; directory, chunk, network and decoder buffers need an admitted
aggregate budget and cancellation.

**Access planning.** Derive the needed elements/leaves from EXMT and requested words/coordinates,
then choose ranges or sequential fetch using dependencies, cache state and measured bandwidth. A
full exact ray currently needs all representatives and essentially all exact data, even if the
multiplet has many rays. Coordinate subsets and ratio checks can need much less. BALL-only reads
require file-level trust (§6.2), not just the exact root. No public-server Range behavior or
availability guarantee is assumed. The project's release, authenticated access and long transfers
remain integration checks, and there is no asserted per-hour Range quota or service guarantee.

<a id="spec-10b4-verified-caches"></a>
### 10b.4 Verified caches

Canonical exact leaf cache identity is `(exact_root, LDIR index, full 64-byte
LDIR entry)`. It can be reused after rechunking or finalization by checking the
new authenticated LDIR entry and recomputing the logical digest. EXMT is leaf
0. **Encoded chunks, offsets, directories, footer, PROV, RAYS, BALL, PREC and
pending transport fragments are keyed by file root**, plus the matching PDIR
entry and source descriptor where applicable. Reusing an encoded chunk across
file roots requires comparing its authenticated entry and revalidating bytes;
an equal exact root alone is insufficient. Pin file length, whole-file hash
and canonical manifest identity with the file-root snapshot so that transport
fragments cannot be spliced between manifests. URLs and ray labels alone
MUST NOT be cache keys.

Only complete, hashed/decoded/leaf-checked chunks or canonical leaves can be
marked verified. Sync bytes before their checksummed validity record; discard
torn records and rehash on use after restart. Hash and consume the same stable
bytes, under cache coordination. On corruption, invalidate and refetch the
whole affected unit. Sparse holes mean missing data, never zeros; sparse cache
files MUST NOT be presented as complete `.sicx` files. Incomplete fragments
remain explicitly unverified. Eviction removes validity before reclaiming
bytes. Derived numerical results additionally need the actual Galois word,
coordinate set, normalization/anchor, decoder identity and resolved precision
policy; an exact root alone does not identify ball-arithmetic output bytes.

<a id="spec-11-decoding-summary-sic-decode"></a>
## 11. Decoding (summary; `sic-decode`)

Requested outputs (rays × coordinates) form one restriction DAG over the tower
for each contraction pass. Tower/action data are retained or reloaded under the
memory policy. BODY leaves stream through PDIR/LDIR in ordered batches and are
contracted for all requested restriction classes. Block results are combined in
block order. Precision retries may reread leaves. The base path is certified
against the canonical branch rectangles; other paths start at `target + delta`
and outputs failing the accuracy gate are retried within configured limits.
The CLI writes raw balls for subsets and normalized balls plus certified
binary64 values for complete rays.

Implementation notes (`cpp/include/sic/decode/decoder.hpp`, `sic-decode`):
- **Base path.** Node 0 is computed alone at the admitted base precision (§3.6, doubling on refusal)
  and each root must lie inside its branch rectangle. In the main pass node 0's root at each level
  is the unique family root overlapping the (outward-rounded) base-path root; any ambiguity is a
  precision refusal. If a replay raises the working precision above the base precision, the base
  path is recomputed starting at twice the working precision, bounded by the ceiling.
- **Graph.** A PrefixPlan over the words `ray word + entry word` gives a parent-first graph; at each
  level a node's target root is computed from the action records evaluated at its source's lower
  class, and nodes are processed by graph depth (generations) in parallel; class labels are assigned
  in node order.
- **Contraction.** Each block is contracted to its block level; when an element has fewer blocks
  than workers, the top classes are split into groups contracted on their ancestor subtrees. Every
  class value is produced by the same operation sequence either way, so outputs are byte-identical
  for any worker count (checked: d1024 maximal raw balls identical at 8, 32 and 64 workers).
- **Acceptance.** An output is kept only if it is finite and has absolute radius ≤ 2^−target (both
  parts) or relative accuracy ≥ target; others are re-decoded alone at doubled precision. The CLI
  normalizes with the ray's anchor, certifies binary64 rounding, and requires overlap with every
  BALL record and identical binary64 values when checking an existing BALL snapshot. This separate
  decode consistency check does not prove containment in an arbitrary preexisting BALL. Finalization
  instead constructs BALL outward from its own certified full normalized decode and verifies
  containment/rounding as required by §5.2; it does not import publication balls. A subset of
  coordinates is reported as RAW-ONLY (normalization needs all `d` coordinates; a subset can still
  be checked against BALL through ratios, §5.2a).
- **Resources.** With pinning enabled, workers are pinned once, ordered by NUMA node; `--memory-mib`
  sets a shared admission budget for decoding, including BODY blocks and tower fit. It is not a hard
  RSS cap.

<a id="spec-12-worked-examples"></a>
## 12. Worked examples



<a id="spec-121-d--4-existing-exact-presentation"></a>
### 12.1 `d = 4` (illustrative exact presentation)

This is an illustrative fixture presentation, finalized from the decoder’s own certified enclosures.
Its hashes identify this example; they are not current publication bindings.

The illustrative exact presentation has `d = 4`, `D0 = 5`, degrees
`[2,2,2]`, `D = 8`, Smith `[2,2,2]`, unit generator `3 = −1 mod 4`, order `2`,
and `unit_generator_words = [["1","1","1"]]`. Its representative positions are
`[0,1,2]`, for divisor labels `[4,1,2]`, respectively.
Entries are `m=0 → (0,0)`, `m=1 → (1,0)`, `m=2 → (2,0)`,
`m=3 → (1,[1,1,1])`, where each pair is `(position, word)`.
For this illustrative presentation the 2-adic shifts
were `[2,1,1]`; EXMT carries them, and the stored level-0 equation is
`g_0²=2`. The selected root is `+√2`; its branch is recomputed by
§5.1b, using the canonical exact-root rule.

Every element layout has at most 8 coordinates, hence one logical leaf per
element for the frozen limit. Canonical raw emits one physical chunk per
element. The first 16 header bytes are
`53 49 43 58 0d 0a 1a 0a 01 00 00 00 00 00 00 00`; immediately
following is an EXMT header `45 58 4d 54 01 00 00 00` plus its 8-byte
length and canonical JSON. A possible element-0 logical payload is
`00 00 01 | 03 | 02 01 | 00`: element 0, first 0, count 1, one nonzero
pair `(r,y)=(1,0)`. With scale `"2"`, this is the element `g_0²=2`.
Its logical hash is §5.5's tagged hash of these 7 bytes and their descriptors;
its physical payload hash is the ordinary SHA-256 of the 7 bytes.

`rep 2` has levels `[2]`, scale `"8"` and coordinates
`(0,0),(0,−1)`; for the illustrative id 13 its logical bytes are
`0d 00 02 | 02 | 03 | 02 01 | 03 01`. The last pair is stored as
`(r,y)=(1,−1)` using `k(−1)=−1`. A raw chunk holds this complete
leaf unchanged. The fixture uses element id 13 for this representative.

An exact-only example has:

`header | EXMT | TOWR* | BODY* | PROV? | LDIR | PDIR | footer`.

Its LDIR begins with EXMT and then one entry per element; PDIR begins with
the physical EXMT and all element chunks, plus PROV if supplied. Finalizing
ray `c0`, word `[0,0,0]`, anchor 1 replaces the old directory/footer tail:

`header | EXMT | TOWR* | BODY* | PROV? | RAYS | BALL | PREC? | LDIR | PDIR | footer`.

LDIR and the exact root stay byte-identical; PDIR, file root, file length and
whole-file SHA-256 change. RAYS records the decoder name and source commit.
The example ray id is `d4-m1-c0`; its binary64 anchor value is
`0.75028485585320670` (`3fe8025562d25ae8`), imaginary part exactly zero.
Fixture SHA-256: d3ce560e18d325a746b07d7d8972bf78cdcd1bb3391bb59c95c4b8d76fc583b3; bytes: 6098
LDIR offset 2850, payload 968; PDIR offset 3834, payload 2136.
Exact root: 616f89361d9a0646ddcd169e7a59a63cdcc7adead9990ca8882ae424f5b846a1
File root: fe3c091ca91b5a115670dcf2eb23d0b4597c20f31373dddf6d049b802f2ff345
Logical entries 15; physical entries 19; LDIR chunk bytes 984; PDIR chunk bytes 2152.

| Chunk | Offset | Payload bytes | SHA-256 |
| --- | ---: | ---: | --- |
| EXMT | 16 | 1582 | ed1a3d2ed9e4a1f93534b50694d3b92f3fb8ac7c51568f0f9116138b01b5c2fd |
| TOWR | 1614 | 7 | b2919bb309f8bf18442e6a8bcdac7b2aad774f2eb2a87c1458e4ffc496ff30b2 |
| TOWR | 1637 | 7 | d9a0a689a4bef6b27a3787e2697784e5339b1bd6b236462e7d456c568ed6d319 |
| TOWR | 1660 | 7 | 03b980e048865d5b3857556438a51408685c4bf0ef2953366287218118589eed |
| TOWR | 1683 | 8 | e5b83017030db3861d177efcdceb2e26f1a0b2b1a21436e1c24889a12529a973 |
| TOWR | 1707 | 8 | 3d2ea7f6dec7fb11df0f3baf4671944efb0d37fd389168a12016a24b48a87243 |
| TOWR | 1731 | 8 | 1cd778c5bfb5205640014da5cbcd8bead498b39dba088af71e18f0d8834cf65a |
| TOWR | 1755 | 8 | e90210efd77c44c5833bc417de51c2d7b7d7da7dc2fd3e5712e876f3d6381cf7 |
| TOWR | 1779 | 7 | 53282aa2d2d4deef319c0dd8e2df0fc69af14399e2cf06229c954af628f03cfa |
| TOWR | 1802 | 7 | 0d7af6983fef5b2888eb9c27fe783a05bcc45f613a75b66c6f83874cec2a1c33 |
| TOWR | 1825 | 7 | c435b1a0681065e1940a2bd3e0da476805bfc96c9be976ec46935e53ba6a02c6 |
| TOWR | 1848 | 7 | a3ec78e016cba87c4bd861bec9e4998db4993bb3661c2d3e56da60d22fb8ceb6 |
| BODY | 1871 | 10 | e92f608f7119a4540c3d90c3309e6833e34fd2dbc67f196c5e1c162713653673 |
| BODY | 1897 | 30 | 8303e1102dfc2023940fa964d64c529ae410341b4aa341272bd832ee2bac0203 |
| BODY | 1943 | 9 | 8d7844d59f07757730a1f861725530915146629550938e5afcbbff5b9a290439 |
| PROV | 1968 | 2 | 44136fa355b3678a1146ad16f7e8649e94fb4fc21fe77e8310c060f61caaff8a |
| RAYS | 1986 | 219 | 701e168ffed3087c090844c0379aed9fe39be7dd14a4326765d7255b00c3aaa5 |
| BALL | 2221 | 512 | 94b0f99bc61e103f4bd7ba1e9341b5cd2816341aa6f4d6c29d95871c3794b5da |
| PREC | 2749 | 85 | 3929e4d0e3f324aebcb44d3af30e515908f9538b89bd63449bfbd5eee2311237 |

Footer bytes:
```text
220b000000000000c803000000000000fa0e0000000000005808000000000000
616f89361d9a0646ddcd169e7a59a63cdcc7adead9990ca8882ae424f5b846a1
fe3c091ca91b5a115670dcf2eb23d0b4597c20f31373dddf6d049b802f2ff345
d21700000000000053494358454e441a
```

<a id="spec-122-d--5-structural-entry-table-only"></a>
### 12.2 `d = 5` (structural entry table only)

`K = Q(√3)`, `ω = √3`, `ι(ω) = −√3`. The smallest primitive root mod `5`
is `2`, of order `4`; let its stored word be `W` (`4W = 0` in the Smith group).
Representative positions are `[0,1]`, for `η_5,η_1`.

| `m` | `g = gcd(m,5)` | `n = 5/g` | `k` in `U_n` | canonical lift | exponent of `2` | position | entry word |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 5 | 1 | sole class | 1 | 0 | 0 | `0` |
| 1 | 1 | 5 | 1 | 1 | 0 | 1 | `0` |
| 2 | 1 | 5 | 2 | 2 | 1 | 1 | `W` |
| 3 | 1 | 5 | 3 | 3 | 3 | 1 | `3W` |
| 4 | 1 | 5 | 4 | 4 | 2 | 1 | `2W` |

Kernel fixity requires `ρ(U_5)` to fix `η_5`; the kernel for `η_1` is trivial.
This table defines indices/words only, with no constructed field tower or SIC asserted.

<a id="spec-123-d--6-structural-entry-table-only"></a>
### 12.3 `d = 6` (structural entry table only)

`K = Q(√21)`, `ω = (1+√21)/2`, `ι(ω) = (1−√21)/2`.
The `2` component has no generator. The smallest primitive root on the `3`
component is `2`, whose CRT lift is `5 mod 6`, of order `2`; let its word
be `W`, `2W = 0`. Positions `[0,1,2,3]` represent `η_6,η_1,η_2,η_3`.

| `m` | `g = gcd(m,6)` | `n = 6/g` | `k` in `U_n` | canonical lift | exponent of `5` | position | entry word |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 6 | 1 | sole class | 1 | 0 | 0 | `0` |
| 1 | 1 | 6 | 1 | 1 | 0 | 1 | `0` |
| 2 | 2 | 3 | 1 | 1 | 0 | 2 | `0` |
| 3 | 3 | 2 | 1 | 1 | 0 | 3 | `0` |
| 4 | 2 | 3 | 2 | 5 | 1 | 2 | `W` |
| 5 | 1 | 6 | 5 | 5 | 1 | 1 | `W` |

Here `m = 4` demonstrates why the cofactor `2` itself cannot be used as a
unit mod `6`. Kernel fixity requires `ρ(5)` to fix `η_6` and `η_3`;
the kernels for `η_1` and `η_2` are trivial. There is no general-d producer
or actual SIC claim behind either structural table.



## Mathematical conventions

<a id="mathematical-conventions"></a>
The following conventions are normative for the interpretation of the WH data. Sections 2–3 above
define the exact embedding and entry rule.

<a id="basis"></a>
<a id="conventions-1-hilbert-space-basis-and-qubit-ordering"></a>
### Hilbert space, basis and qubit ordering

- The Hilbert space is `C^d` with the computational (standard) basis `|0⟩, |1⟩, …, |d−1⟩`.
  Coordinate `j` of a stored vector `ψ` is the amplitude of `|j⟩`; indices are taken mod `d`.
  Coordinates are stored in the order `j = 0, 1, …, d−1`.
- SICX 1.0 permits every integer `2 ≤ d ≤ 2^20`; the current producer and collection use `d = 2^s`.
  The vector is a single `d`-level system. For `d = 2^s`, a qubit view is only an
  encoding of the index. Write `j = Σ_{k=0}^{s−1} b_k 2^k` with `b_k ∈ {0, 1}`, so `b_0` is the least
  significant bit. In the usual Kronecker (tensor) order the most significant bit comes first:

  `|j⟩ = |b_{s−1}⟩ ⊗ |b_{s−2}⟩ ⊗ … ⊗ |b_0⟩`.

- Consequences for common software (no reordering of `ψ` is needed in either case, only the qubit labels
  differ):
  - Qiskit is little-endian: qubit `k` is bit `b_k`, so `Statevector(psi)` is correct as is.
  - QuTiP and Cirq are big-endian: tensor factor 0 is `b_{s−1}`, so factor `k` is Qiskit qubit `s−1−k`.
    Bit-reverse the index to relabel.
- The WH group below uses the cyclic shift on `Z_d` (`|j⟩ ↦ |j+1 mod d⟩`). For `s > 1`,
  it differs from the tensor-product `s`-qubit Pauli group.

<a id="conventions-2-shift-and-clock"></a>
### Shift and clock

```
ω = exp(2πi/d),     X|j⟩ = |j+1⟩,     Z|j⟩ = ω^j |j⟩       (indices mod d).
```

`Z X = ω X Z`.

<a id="conventions-3-displacement-operators"></a>
### Displacement operators

```
τ = −exp(πi/d),   τ² = ω,
D_p = τ^(p1·p2) X^p1 Z^p2,      p = (p1, p2).
```

- **Index range.** `τ` has order `d̄`, with `d̄ = 2d` for even `d` and `d̄ = d` for odd `d`, so
  `D_p` is defined for `p ∈ (Z/d̄)²`. For even `d`, `D_(p + d·q) = (−1)^⟨p,q⟩ D_p` (`q ∈ Z²`), so
  the operators with `p ∈ {0, …, d−1}²` give each displacement once up to sign. Data, tools and
  overlap tables use `p1, p2 ∈ {0, …, d−1}`; Clifford matrices are taken mod `d̄` ([convention
  5](#conventions-5-clifford-group-extended-clifford-group-stabilizer-and-zauner-symmetry)).
- **Symplectic form and composition law.** With `⟨p, q⟩ = p2·q1 − p1·q2`,

  ```
  D_p D_q = τ^⟨p,q⟩ D_(p+q),     D_p† = D_(−p),     D_p D_q = ω^⟨p,q⟩ D_q D_p.
  ```

- **Action on coordinates.** `(D_p ψ)_k = τ^(−p1·p2) ω^(p2·k) ψ_(k−p1)`.
- **Overlaps.** With `⟨ψ|φ⟩ = Σ_k conj(ψ_k) φ_k` (conjugate-linear in the first argument),

  ```
  a_p = ⟨ψ|D_p ψ⟩ = τ^(p1·p2) Σ_j conj(ψ_(j+p1)) ψ_j ω^(p2·j).
  ```

  For fixed `p1` this is one length-`d` DFT with kernel `exp(+2πi·p2·j/d)`, so all `d²` overlaps cost
  `O(d² log d)`. The phase `τ^n` should be evaluated as `exp(πi·(n(d+1) mod 2d)/d)` with the exponent reduced in
  exact integers; evaluating `τ**n` in floating point loses phase accuracy at large `d`.

<a id="conventions-4-sic-condition-normalization-and-phase"></a>
### SIC condition, normalization and phase

- **SIC fiducial.** A unit vector `ψ ∈ C^d` is a WH SIC fiducial if

  ```
  |⟨ψ|D_p ψ⟩|² = 1/(d+1)     for every p ≠ 0 (mod d).
  ```

  The `d²` vectors `D_p ψ`, `p ∈ {0, …, d−1}²`, then give the SIC-POVM `{ (1/d) |D_p ψ⟩⟨D_p ψ| }`.
  Since `|a_p|` depends only on `p mod d`, the condition is checked on the `d² − 1` labels `p ≠ (0, 0)` with
  `p1, p2 ∈ {0, …, d−1}`.
- **Normalization and phase (anchor rule).** A fiducial is fixed only up to a global phase. The published
  vector of a ray is

  ```
  w = v/‖v‖ · conj(v_a)/|v_a|,
  ```

  where `v` is the exact (decoded) vector and `a` is the ray's **anchor**: a coordinate index with `v_a ≠ 0`.
  Hence `‖w‖ = 1` and `w_a > 0` is real positive, with imaginary part **exactly** zero (also in the
  binary32 and binary64 files).
- **The anchor is recorded, not derived.** Each ray file stores it (`labels.anchor` in `c<N>.json`,
  `anchor` in `.mat` metadata when supplied). HDF5 exports from `sic-data` omit the anchor; read it
  from the source JSON. It is **not** always coordinate 0: for example the anchor is 0 at `d = 2`, 1
  in the worked `d = 4` fixture, and can be as large as 569 in a recorded `d = 1024` numerical
  fixture. Do not assume that the first coordinate is real.
- Coordinates are in the original order `0, …, d−1`; no permutation is applied.

<a id="conventions-5-clifford-group-extended-clifford-group-stabilizer-and-zauner-symmetry"></a>
### Clifford group, extended Clifford group, stabilizer and Zauner symmetry

The unitary convention below is the one used by the implementation.

- **Symplectic unitaries `U_F`.** For `F = [[a, b], [c, e]] ∈ SL(2, Z/d̄)` acting on column labels
  `p = (p1, p2)` (so `F p = (a p1 + b p2, c p1 + e p2)`), and `b` a unit mod `d̄`,

  ```
  U_F = d^(−1/2) Σ_{u,v=0}^{d−1} τ^(b⁻¹(a v² − 2uv + e u²)) |u⟩⟨v|,     b⁻¹ the inverse of b mod d̄,
  ```

  satisfies `U_F D_p U_F† = D_(F p)`. If `b` is not a unit, write `F = F1 F2` with `F1 = [[0, −1], [1, x]]`,
  `F2 = [[c + x a, e + x b], [−a, −b]]` and `x` chosen so that `e + x b` is a unit; then `U_F = U_(F1) U_(F2)`.
  The library uses this formula with no extra phase; only the class of `U_F` modulo phases matters below.
- **Anti-unitaries.** `ESL(2, Z/d̄)` is the group of matrices with determinant `±1` mod `d̄`. For
  `det F = −1`, `U_F = U_(F J) K` with `J = diag(1, −1)` and `K` complex conjugation in the standard
  basis (`K D_p K = D_(J p)`); again `U_F D_p U_F^(−1) = D_(F p)`.
- **Extended Clifford group.** Its elements are `U = D_χ U_F` with `χ ∈ (Z/d)²` and `F ∈ ESL(2,
  Z/d̄)`, up to a phase. `PEC(d)` denotes the projective extended Clifford group (elements modulo
  phases), as in Scott–Grassl (2010).
- **Stabilizer.** The stabilizer of a fiducial `ψ` is `S(ψ) = { [U] ∈ PEC(d) : U ψ ∝ ψ }`, i.e.
  `|⟨ψ|U ψ⟩| = 1` for unit `ψ`. The symmetry tables list each element as `(det, F, χ)` with `F` the
  lexicographically smallest lift to `[0, d̄)` with the right determinant.
- **Zauner symmetry.** An element of the symmetry tables is flagged `zauner` (Zauner type) if it is
  unitary (`det F = 1`), has order 3 in `PEC(d)` and `tr F ≡ −1 (mod d)`: Appleby's canonical
  order-3 elements (Scott–Grassl §4); `F_z = [[0, −1], [1, −1]]` is one. Zauner's conjecture (every
  dimension has a fiducial with such a symmetry) is open; the symmetry tables record which released
  rays have Zauner-type elements (see [verification.md](verification.md) for the status of those
  tables).

<a id="conventions-6-equivalence-rays-and-multiplets"></a>
### Equivalence, rays and multiplets

- **Equivalence.** Two fiducials `ψ, φ` are **equivalent** if `φ ∝ U ψ` for some `U` in the extended
  Clifford group (unitary or anti-unitary); otherwise they are **inequivalent**. Equivalent
  fiducials give the same SIC up to a Clifford transformation.
- **Ray.** In this project a **ray** is one extended-Clifford orbit (one `PEC(d)` orbit) of SIC
  fiducials. The data publish one representative fiducial vector per ray, normalized and phased as
  in [convention 4](#conventions-4-sic-condition-normalization-and-phase). (This is the usage of the
  catalogue; it is broader than "a vector up to phase".)
- **Multiplet.** A multiplet `m<N>` is the set of rays published from one exact SICX file. All its
  rays share one number field `L` (an abelian extension of the base field `K` of [convention
  8](#conventions-8-exact-data-summary-of-sicx-spec-2)), and each ray is the image of the base
  vector defined by the entry rule under a Galois automorphism `σ ∈ Gal(L/K)`, applied to every
  coordinate and evaluated in the fixed embedding. The ray file records this automorphism as
  `galois_element` (Smith invariant factors and exponents). The name follows the Galois multiplets
  of the SIC literature; that each `m<N>` is exactly one such multiplet is not claimed beyond the
  conjectural classification below. Which rays a multiplet contains is catalogue data and is not
  derived by readers.
- **The order ↔ multiplet correspondence is conjectural.** Each multiplet was constructed from a real
  quadratic order. The proposed correspondence between real quadratic orders and SIC multiplets, and the
  resulting count of 174 rays for `d = 2^s ≤ 2048`, rest on a conjectural classification (Kopp–Lagarias,
  Conjecture 1.2). The data structure does not assume it, and no file states it as fact.

<a id="conventions-7-labels"></a>
### Labels

| label | meaning |
| --- | --- |
| `d<d>` | the dimension, e.g. `d1024` |
| `m<N>` | a multiplet within the dimension: `m1, m2, …` in increasing order of the conductor of the quadratic order used in construction. The conductor itself is recorded only in provenance; the label makes no arithmetic claim. |
| `c<N>` | a ray within its multiplet. `N` is the zero-based rank of its canonical numeric Smith word in lexicographic order among the file's rays. `c0` need not be the identity Galois element. The normative identification of a ray inside its multiplet is its `galois_element`. |
| `d<d>-m<N>-c<N>` | the full ray id, carried in every ray file (e.g. `d1024-m1-c12`) and used as the member name in `all.npz` |

- `m<N>` is assigned by the release from provenance and is not stored in the SICX file.
- `m<N>` numbers the multiplets of a dimension by increasing conductor of the order used in construction,
  which is recorded in provenance only; so the maximal order is `m1` (d = 8, 128, 512, 1024).
  The fixture `cpp/tests/fixtures/sicx/d8_release_catalogue.json` follows this rule.

<a id="conventions-8-exact-data-summary-of-sicx-spec-2"></a>
### Exact data (summary of sicx-spec §2)

- **Base field.** For `d ≥ 4`, `K = Q(√D0)`, where `D0 > 1` is the squarefree kernel of
  `(d−3)(d+1)`. This is the standard SIC quadratic field of Appleby–Flammia–McConnell–Yard,
  [*Generating ray class fields of real quadratic fields via complex equiangular lines*,
  §1](https://arxiv.org/pdf/1604.06098); its use here is a format convention. At `d = 2`, `D0 = −3`,
  `K = Q(√−3)`; at `d = 3`, `K = Q` and `D0 = 1` is a sentinel. Readers derive the case from `d`; it
  is not stored in EXMT.
- **Integers.** For quadratic `K`, `O_K = Z[ω_K]`: `ω_K = (1+√D0)/2` if `D0 ≡ 1 (mod 4)`; otherwise
  `ω_K = √D0`. A pair `(x,y)` means `x+yω_K`. This `ω_K` is the integral-basis generator, distinct
  from the root of unity `ω` of [convention 2](#conventions-2-shift-and-clock) (the SICX
  specification calls it `ω`). At `d = 3`, set `ω_K = 0` and require `y = 0` in every pair, using
  the same byte layout.
- **Embedding.** Set `s = √D0 > 0` for real quadratic `K`, or `s = i√|D0|` with positive imaginary
  part for imaginary quadratic `K`. Evaluate `ω_K ↦ (1−s)/2` in the half-integral basis, or
  `ω_K ↦ −s` in the other quadratic basis. Rational `K` has its unique embedding `x ↦ x`.
  For `d = 2^s ≥ 4`, `D0 ≡ 1 (mod 4)` ; e.g. `d = 5` instead has `D0 = 3` and
  `ω_K ↦ −√3`. Tower roots are fixed by the branch rectangles stored in the file.
- **Entry convention.** There is one representative per divisor `g | d`, at position `g` except
  `g = d` at position `0`. For coordinate `m`, use `g = gcd(m,d)` and the smallest positive unit
  lift of `m/g mod d/g`, with the trivial quotient at `m = 0`. Words are formed from the canonical
  prime-power generators, CRT lifted in increasing prime order. The representatives' kernel fixity is
  an explicit algebraic premise; [sicx-spec.md §3.5](sicx.md#spec-35-entries-and-rays) gives the proof,
  full generator order, EXMT words and verification procedure. These rules introduce no special entry
  or finalization case at `d = 2,3` and claim no general-d producer.
- Normalization and phase of decoded vectors are those of [convention
  4](#conventions-4-sic-condition-normalization-and-phase).

<a id="gabor"></a>
<a id="conventions-9-gabor-frame-time-frequency-view"></a>
### Gabor-frame (time-frequency) view

View `ψ` as a periodic signal on `Z_d`. With translation `(T_a f)(j) = f(j − a)` and modulation
`(M_b f)(j) = exp(2πi·b·j/d) f(j)`:

```
T_a = X^a,     M_b = Z^b,
D_(a,b) = τ^(ab) T_a M_b = (−1)^(ab) exp(iπab/d) T_a M_b,
M_b T_a = ω^(ab) T_a M_b.
```

So `D_(a,b)` equals the time-frequency shift `M_b T_a` up to a phase, which does not affect `|⟨ψ|D
ψ⟩|²` or the projectors. The SIC condition says that the discrete ambiguity function `A(a, b) =
|⟨ψ|M_b T_a ψ⟩|²` equals `1/(d+1)` off the origin, i.e. the Gabor system `{M_b T_a ψ}` with all `d`
translations and all `d` modulations is an equiangular tight frame. A fiducial given in Gabor
notation can be used as is.

<a id="conventions-10-worked-examples"></a>
### Worked examples



<a id="conventions-101-d--2-the-tetrahedron"></a>
#### `d = 2`: the tetrahedron

Illustrative fixture ray `d2-c0` (fixture `cpp/tests/fixtures/sicx/d2-c0.json`; release id
`d2-m1-c0`), anchor 0. The binary64 values are

```
w_0 = 0.88807383397711526
w_1 = 0.32505758367186816 + 0.32505758367186816 i
```

Exactly, `w_0 = α`, `w_1 = (1 + i)β` with `α² = 1/2 + √3/6`, `β² = 1/4 − √3/12` (the radical record
`data/d2/d2.sicrad`); equivalently `w ∝ (1, z)` with `z = (1 + i)(√3 − 1)/2`. The anchor
coordinate `w_0` is real positive.

Overlaps (`τ = −exp(πi/2) = −i`, `ω = −1`):

| `p` | `a_p = ⟨w|D_p w⟩` | `|a_p|²` |
| --- | --- | --- |
| (0, 1) | `+1/√3 ≈ 0.577350269190` | 1/3 |
| (1, 0) | `+1/√3` | 1/3 |
| (1, 1) | `−1/√3` | 1/3 |

so `|a_p|² = 1/(d+1) = 1/3` for all `p ≠ 0`. The Bloch vectors `(⟨σ_x⟩, ⟨σ_y⟩, ⟨σ_z⟩)` of the four
states `D_p w` (`σ_x = [[0, 1], [1, 0]]`, `σ_y = [[0, −i], [i, 0]]`, `σ_z = diag(1, −1)`) are

```
(1, 1, 1)/√3,  (−1, −1, 1)/√3,  (1, −1, −1)/√3,  (−1, 1, −1)/√3      for p = (0,0), (0,1), (1,0), (1,1),
```

the vertices of a regular tetrahedron. The stabilizer table (`data/d2/stabilizers.txt`) lists six
elements; for example `F = [[1, 1], [1, 2]]` (mod 4), `χ = (0, 1)`, `det = +1`, order 3, `tr F = 3 ≡
−1 (mod 2)`: a Zauner-type element, and `|⟨w|D_χ U_F w⟩| = 1` to binary64 rounding.

<a id="conventions-102-d--4"></a>
#### `d = 4`

Illustrative fixture ray `d4-c0` (fixture `cpp/tests/fixtures/sicx/d4-c0.json`; release id
`d4-m1-c0`), anchor 1. Exact data: `D0 = 5`, `K = Q(√5)`, `[L:K] = 8`, Smith factors `[2, 2, 2]`
([sicx-spec.md](sicx.md#spec-sicx-10-final-exact-weylheisenberg-sic-fiducial-format) §12). Binary64
values:

```
w_0 = −0.068909968959496928 − 0.48079909639623813 i
w_1 =  0.75028485585320670                          (anchor: imaginary part exactly 0)
w_2 =  0.19915350650405092 − 0.028543443725732625 i
w_3 = −0.29802920318270115 − 0.26806347546354786 i
```

Selected overlaps (`τ = −exp(πi/4)`, `ω = i`), computed from these values with the formula of
[convention 3](#conventions-3-displacement-operators):

| `p` | `a_p` | `|a_p|²` |
| --- | --- | --- |
| (0, 1) | `0.195439507585 + 0.402247932095 i` | 0.2 |
| (0, 2) | `−0.447213595500 ≈ −1/√5` | 0.2 |
| (1, 0) | `0.195439507585 − 0.402247932095 i` | 0.2 |
| (1, 1) | `−0.195439507585 + 0.402247932095 i` | 0.2 |
| (2, 2) | `−0.447213595500` | 0.2 |

All 15 nonzero labels give `|a_p|² = 1/5` within `1e-16` in binary64. The stabilizer has order 6; one
Zauner-type element is `F = [[0, 1], [7, 3]]` (mod 8), `χ = (0, 2)`, `tr F = 3 ≡ −1 (mod 4)`, with
`|⟨w|D_χ U_F w⟩| = 1` to binary64 rounding (`data/d4/stabilizers.txt`, element 3).

These checks are floating-point illustrations of the conventions. The status of each statement about the
released data (exact, certified or conditional) is given in [verification.md](verification.md).
