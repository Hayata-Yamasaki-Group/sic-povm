# Data guide

Conventions for the basis, qubit order, displacement operators, normalization, anchors and labels
are normative in [SICX](sicx.md#mathematical-conventions). This guide owns file layouts, loading,
download and storage. See [verification](verification.md) for claim scope and
[reproduction](reproduce.md) for construction recipes.

<a id="data-1-the-data-tree-and-cataloguejson"></a>
The source tree contains one generated d2 WH ray, shared numerical
stabilizer tables through d1024, and the exceptional Hoggar object. The layout
below also specifies release records; it does not assert their presence.
The d2 transport manifest uses placeholder release coordinates; its local
radical input and numerical records are complete, while download assets await
publication.
Names use lowercase letters, digits and underscores, with dots only for file
extensions. Dimension appears once as a non-padded `d<N>` directory; the sole
exception is `d2/d2.sicrad`, whose basename is canonical SICX provenance.
Multiplets use stable `m<M>` numbers ordered by increasing construction conductor,
with standard/maximal order described in metadata. This accommodates both orders
in a single dimension and future d1024 standard/d2048 tables without renaming.
Shared dimension data are `stabilizers.txt` and optional `all.npz`; `d8/hoggar`
is the explicitly named non-WH object. An optional root checksum sidecar uses
`sha256sums.txt` and covers every other data file, including the catalogue. It is
excluded from the catalogue to avoid circular hashes. Test oracles live in
`cpp/tests/fixtures/core`; they are never installed as release data.

## 1. The data tree and `catalogue.json`

```
data/
  catalogue.json                       every dimension, multiplet, ray and file (sizes, SHA-256)
  sha256sums.txt                     optional release-tree checksum sidecar
  d<d>/
    all.npz                       all numerical fiducials of the dimension
    stabilizers.txt                    extended Clifford stabilizers (symmetry table)
    m<N>/                              one multiplet
      manifest.json                    multiplet id, rays, Smith factors, SICX hashes
      multiplet.sicx.parts.json        Release assets of the exact SICX file (transport parts)
      c<N>.json                        one ray: fp32, fp64, hp and metadata
      c<N>.npy  c<N>.mat  c<N>.h5  c<N>.csv
```

Labels `d<d>`, `m<N>`, `c<N>` and the full id `d<d>-m<N>-c<N>` are defined in
[conventions.md §7](sicx.md#conventions-7-labels).

**`catalogue.json`** (schema `sic-release-catalogue/1.0`, canonical JSON, every number a decimal string):

| key | content |
| --- | --- |
| `release` | `repository`, `tag` |
| `dimensions[]` | `d`; `files[]` (per-dimension files); `multiplets[]` |
| `multiplets[]` | `id` (`d<d>-m<N>`), `labels`, `smith` (Smith invariant factors of `Gal(L/K)`), `files[]`, `sicx` (`size_bytes`, `sha256`, `merkle_root`, `exact_root`, `parts_file`, `assets[]` with `asset`, `size_bytes`, `sha256`), `rays[]` |
| `rays[]` | `id` (`d<d>-m<N>-c<N>`), `label` (`c<N>`), `sicx_ray_label`, `exponents` (Galois element), `accuracy_bits`, `files[]` |
| `files[]` | `path` (relative to `data/`), `bytes`, `sha256` |

To find a ray: select the dimension by `d`, the multiplet by `id`, the ray by `id`; its `files[]`
list the paths and SHA-256 of `c<N>.json` and the binary files.



`manifest.json` (schema `sic-multiplet-manifest/1.0`) holds the same multiplet record for one
folder: `id`, `d`, `multiplet`, `labels`, `smith`, `rays[]` (`id`, `label`, `sicx_ray_label`,
`exponents`, `accuracy_bits`) and `sicx` (`parts_file`, `size_bytes`, `sha256`, `merkle_root`,
`exact_root`).

<a id="data-2-the-ray-file-cjson"></a>
## 2. The ray file `c<N>.json`

Schema `sic-fiducial-json/1.0`
([schemas/sic-fiducial-json-1.schema.json](schemas/sic-fiducial-json-1.schema.json)). One canonical
JSON document per ray, generated from the verified SICX file. **Every number is a string**; complex
values are pairs `[re, im]`; coordinates are in the order `0, …, d−1`; the vector is the normalized,
anchor-phased fiducial `w` of [conventions.md
§4](sicx.md#conventions-4-sic-condition-normalization-and-phase). "Exact coordinate" below means
`w_j` computed from the exact SICX data in the embedding of [conventions.md
§8](sicx.md#conventions-8-exact-data-summary-of-sicx-spec-2); whether that vector is a SIC, and on
what premises, is stated in [verification.md](verification.md).

<a id="data-21-metadata"></a>
### 2.1 Metadata

| key | content |
| --- | --- |
| `schema` | `"sic-fiducial-json/1.0"` |
| `required_features` | empty list in this version |
| `d` | dimension |
| `multiplet` | `m<N>` |
| `id` | full ray id `d<d>-m<N>-c<N>` |
| `index` | position of the ray in the SICX ray list |
| `galois_element` | `smith` (invariant factors `n_l`) and `exponents` (`c_l`, `0 ≤ c_l < n_l`): the Galois automorphism `Π τ_l^(c_l)` applied to the entry-rule base vector to obtain this ray; the label `c0` need not have identity word |
| `labels` | catalogue labels, plus `anchor` (the anchor index `a`, 0-based) and `sicx_ray_label` (the ray label inside the SICX file) |
| `sicx` | `name`, `sha256`, `merkle_root` of the SICX file this ray was generated from |
| `exact` | present only when `exact.json` exists: `file` (`"exact.json"`) and `ray` (the id) |

<a id="data-22-numerical-levels-and-what-each-guarantees"></a>
### 2.2 Numerical levels and what each guarantees

| level | fields | guarantee |
| --- | --- | --- |
| `fp64` | `values` (`[hex_re, hex_im]`, 16 hex digits: the IEEE-754 binary64 bit pattern), `decimal` (`%.16e`, 17 significant digits, round-trips to the same binary64), `error_bound`, `rounding` | each real and each imaginary part is the **correct rounding** (round to nearest, ties to even) of the exact coordinate. `error_bound` is a power of two `0x1p−N` strictly above `|value − exact|` for every real and imaginary part, or `0x0p+0` if all represented components are exact. |
| `fp32` | as `fp64`, with 8 hex digits and 9 significant digits (`%.8e`) | the correct rounding to binary32 of the exact coordinate, rounded **from the certified ball, never from `fp64`**; values are normal or zero. |
| `hp` | `accuracy_bits`, `decimal_places` `P`, `values` (`[mid_re, rad_re, mid_im, rad_im]`, decimal strings with exactly `P` decimals) | each decimal ball (midpoint ± radius) **contains** the stored certified ball, hence the exact coordinate; every radius is `< 2^−accuracy_bits`. |

How the guarantees are established (each is checked when the file is generated; generation fails
otherwise):

- The SICX file stores a certified ball (`BALL` chunk) for every coordinate of every ray, containing
  the exact coordinate. Recorded balls through d1024 use 256 working bits and `accuracy_bits`
  250–253; these are input-specific measurements, not format limits.
- `fp64` and `fp32`: both endpoints of the ball must round to the same floating-point number; that
  number is then the correct rounding of every point of the ball, in particular of the exact
  coordinate, with zero encoded as positive zero.
- `hp`: the decimal ball is rounded outward from the binary ball (midpoint to nearest, radius
  rounded up plus one unit in the last place), so it contains the binary ball. `P = ⌈accuracy ·
  log10 2⌉ + 3` for the ball accuracy.
- The anchor coordinate has imaginary part exactly 0 in `fp64` and `fp32`; in `hp` its imaginary
  midpoint is 0 and its radius is one unit in the last decimal place (outward rounding).
- The binary64 vector is not exactly of norm 1; renormalize if you need that to rounding accuracy.

<a id="data-23-example"></a>
### 2.3 Example

From the pinned `d4-c0.json` illustrative fixture (coordinate 1 is its anchor, not a current
release-production binding):

```json
"fp64": {"values": [["bfb1a4156f0ca66d","bfdec56992bdf6e5"], ["3fe8025562d25ae8","0000000000000000"], ...],
         "decimal": [["-6.8909968959496928e-02","-4.8079909639623813e-01"], ["7.5028485585320670e-01","0.0000000000000000e+00"], ...],
         "error_bound": "0x1p-54",
         "rounding": "certified round-to-nearest-even of the exact normalized coordinate"}
```

<a id="data-3-binary-files-npy-npz-mat-h5-csv"></a>
## 3. Binary files: `.npy`, `.npz`, `.mat`, `.h5`, `.csv`

The layout above is the release contract; not every listed artifact is present in a source checkout.
Exact files, current per-ray bindings and hosted assets are emitted by release production. The
binary files carry a binary64 (complex128) vector. `sic-data write` accepts a current WH or Hoggar
ray JSON file. It checks dimension, finite FP64 words and exact agreement of hexadecimal words with
their prescribed decimal spelling; it preserves those words. It does not re-establish the JSON’s
high-precision enclosure or SIC proof. Correct-rounding and containment guarantees originate in
conforming SICX finalization and ray JSON export (§2.2). `sic-data check` compares generated
numerical files against that JSON through separate readback codecs. Decoder text/ball output is not
an input mode of `sic-data write`. The pinned numerical reference fixtures establish a coordinate
error bound, described below; that guarantee is weaker than correct rounding.

| file | layout |
| --- | --- |
| `c<N>.npy` | NumPy format 1.0, dtype `<c16` (little-endian complex128), shape `(d,)`, C order. No pickle. |
| `all.npz` | ZIP of `.npy` members, one per ray, named `<ray id>.npy`, sorted by id; stored without compression, fixed timestamps (byte-deterministic). The ZIP archive comment is JSON (`"schema": "sic-npz-bundle/1.0"`, dimension, dtype, ray count, source hashes or, for many rays, their digest); NumPy ignores it. |
| `c<N>.mat` | MATLAB Level 5, uncompressed. Variables: `psi` (complex `d × 1`), `d`, `wh_convention`, `tau_phase`, `ray_id`, `rounding`, `error_bound`, `source_sha256`; `anchor` (0-based) and `anchor_index_1based` are present when the source JSON records an anchor. |
| `c<N>.h5` | HDF5 dataset `psi`, shape `(d,)`, compound type `{r: float64, i: float64}` (the h5py complex layout); attributes written by `sic-data`: `d`, `ray_id`, `rounding`, `error_bound`, `source_sha256`. Read the anchor and status from the source JSON; these attributes are not emitted by this writer. No timestamps. |
| `c<N>.csv` | header `index,real,imag`, then `d` rows; 17 significant digits. |

<a id="data-4-loading-recipes"></a>
## 4. Loading recipes

Snippets are minimal. Paths follow the release tree; replace `d8`, `m1`, `c0` as needed.

<a id="data-41-numpy"></a>
### 4.1 NumPy

```python
import numpy as np
bundle = np.load("data/d8/all.npz", allow_pickle=False)   # one member per ray
print(bundle.files)                                          # ray ids
psi = bundle[bundle.files[0]]                                # complex128, shape (d,)
psi = np.load("data/d8/m1/c0.npy", allow_pickle=False)        # a single ray
```



From the JSON file (bit-exact, no binary file needed):

```python
import json, struct
import numpy as np
r = json.load(open("data/d8/m1/c0.json"))
f64 = lambda h: struct.unpack(">d", bytes.fromhex(h))[0]
psi = np.array([complex(f64(re), f64(im)) for re, im in r["fp64"]["values"]])
anchor = int(r["labels"]["anchor"])
```

<a id="data-42-scipy-mat"></a>
### 4.2 SciPy (`.mat`)

```python
from scipy.io import loadmat
S = loadmat("data/d8/m1/c0.mat"); psi = S["psi"].ravel(); anchor = int(S["anchor"][0, 0])
```

<a id="data-43-matlab-and-gnu-octave"></a>
### 4.3 MATLAB and GNU Octave

```matlab
S = load('data/d8/m1/c0.mat');  psi = S.psi;  d = S.d;   % psi is d x 1 complex double
a = S.anchor_index_1based;                              % psi(a) is real positive
```

<a id="data-44-hdf5-h5py-julia-hdf5jl-matlab-h5read"></a>
### 4.4 HDF5 (h5py, Julia HDF5.jl, MATLAB `h5read`)

```python
import h5py
with h5py.File("data/d8/m1/c0.h5", "r") as f:
    psi = f["psi"][()]                 # h5py reads compound {r, i} as complex128
    # The sic-data HDF5 writer does not store the anchor; read labels.anchor from c0.json.
```

```julia
using HDF5
psi = h5read("data/d8/m1/c0.h5", "psi")   # Vector{ComplexF64} with complex support enabled (default)
```

```matlab
s = h5read('data/d8/m1/c0.h5', '/psi');  psi = s.r + 1i * s.i;
```

<a id="data-45-julia-npy-mat--planned"></a>
### 4.5 Julia (`.npy`, `.mat`)

```julia
using NPZ, MAT
psi = npzread("data/d8/m1/c0.npy")
psi = vec(matread("data/d8/m1/c0.mat")["psi"])
```



<a id="data-46-mathematica-hdf5-or-json--planned"></a>
### 4.6 Mathematica (HDF5 or JSON)

HDF5 and JSON are interchange options, but a validated Mathematica loading recipe is not provided.
Import complex HDF5 components explicitly if the reader does not recognize the compound `{r,i}`
representation; use the JSON hexadecimal FP64 words for bit-exact reconstruction.



<a id="data-47-c-library-reader"></a>
### 4.7 C++ (library reader)

The `read-fiducial` example is compiled and run on a CSV fixture in CTest.
Library reader tests separately exercise NPY and text input.
See also the [compiled library example](library.md#library-compiled-core-example).

<a id="data-5-checking-a-fiducial-yourself"></a>
## 5. Checking a fiducial yourself

The library tool `sic-overlaps FILE` prints the maximum of `| |⟨ψ|D_p ψ⟩|² − 1/(d+1) |` over `p ≠ 0`
(binary64; a diagnostic, not a certificate). A NumPy recipe with one FFT per shift `p1`, using the
formula of [conventions.md §3](sicx.md#conventions-3-displacement-operators):

```python
def overlaps(psi):
    d = psi.size; n = np.arange(d)
    A = np.empty((d, d), complex)
    for p1 in range(d):
        row = np.fft.ifft(np.conj(np.roll(psi, -p1)) * psi) * d          # sum_j conj(psi_(j+p1)) psi_j omega^(p2 j)
        A[p1] = np.exp(1j * np.pi * ((p1 * n * (d + 1)) % (2 * d)) / d) * row  # times tau^(p1 p2)
    return A                                                             # A[p1, p2] = <psi|D_p psi>
A = overlaps(psi); d = psi.size
print(np.abs(np.abs(A.ravel()[1:])**2 - 1 / (d + 1)).max())             # about 1e-16 for binary64 data
```



Rigorous checks (certified bounds, exact checks) are described in [verification.md](verification.md).

<a id="data-6-symmetry-tables-and-the-cross-reference--planned"></a>
## 6. Symmetry tables and the cross-reference

- `d<d>/stabilizers.txt`: the extended Clifford stabilizer of each ray, one `elem` line per element
  with `det`, `F` (canonical lift mod `d̄`), `χ`, order, trace and a `zauner` flag ([conventions.md
  §5](sicx.md#conventions-5-clifford-group-extended-clifford-group-stabilizer-and-zauner-symmetry)).
  Column definitions are given in the stabilizer-table section below.

<a id="data-7-which-file-should-i-use--planned"></a>
## 7. Which file should I use?

| need | file |
| --- | --- |
| all rays of a dimension in Python | `all.npz` |
| one ray, bit-exact binary64 with metadata | `c<N>.json` (`fp64`) |
| more than 53 bits, with a rigorous enclosure | `c<N>.json` (`hp`) |
| MATLAB / Octave | `c<N>.mat` |
| exact algebraic data | the SICX file ([data-download.md](data.md#transport-getting-the-exact-data)) |

<a id="data-8-the-hoggar-lines-datad8hoggar"></a>
## 8. The Hoggar lines (`data/d8/hoggar/`)

The Hoggar lines (64 lines in `C^8`, covariant under the 3-qubit Pauli group) are an exceptional
entry, **not** one of the 174 Weyl–Heisenberg rays and not counted in `catalogue.json`'s ray totals;
group, convention and the relation to the WH SICs are in [conventions.md
§11](data.md#conventions-11-hoggar-lines-exceptional-entry-at-d--8). Provenance: the files were
added directly as data from the closed-form fiducial `(−1+2i, 1, …, 1)/√12`; they are not generated
by code in this repository and are checked by the CTest `sic_test_data_hoggar_data` and
`cpp/checkers/gp/hoggar_verify.gp`.

```
data/d8/hoggar/
  manifest.json   schema sic-hoggar-manifest/1.0: group, convention, status, provenance, ray id d8-hoggar-c0
  exact.json      Gaussian integers of v = sqrt(12) psi (all numbers strings), normalization 1/sqrt(12)
  c0.json         fp32, fp64, hp as in section 2 (schema sic-hoggar-fiducial-json/1.0; no multiplet/galois/sicx)
  c0.npy  c0.mat  c0.h5  c0.csv   the binary64 vector of c0.json (fp64), same formats as section 3
```

- `c0.json`: `fp64`/`fp32` are the correct roundings of the exact coordinates; `hp` has 80 decimals,
  radius 0 exactly where the coordinate is exactly 0 and one unit in the last place otherwise.
- Load it exactly like a WH ray (section 4), e.g. `np.load("data/d8/hoggar/c0.npy")`. The 64 lines
  are `P_{a,b} ψ` with `P_{a,b}|x⟩ = (−1)^{b·x}|x ⊕ a⟩` (`a, b` in `0..7`, bit 2 = tensor factor 1):
  for a 3-bit index `x`, `(P ψ)[x ^ a] = (−1)^{popcount(b & x)} ψ[x]`. Do **not** apply the WH
  operators `D_p` of the displacement-operator section of SICX.
- Check it: `ctest --test-dir cpp/build -R hoggar` (exact norm, 63 overlaps `= 1/9`, 2016 line
  pairs, and that `c0.json` (hex values, `hp`), `c0.csv`, `c0.npy` and `c0.mat` agree with
  `exact.json`; `c0.h5` has its HDF5 signature checked and, when HDF5 is enabled, its `psi` schema
  attribute checked); `gp -q -f cpp/checkers/gp/hoggar_verify.gp` from the repository root prints
  `PASS`.
- `all.npz`: the manifest records intended inclusion (`npz.included` is `true`) as
  `d8-hoggar-c0.npy`, with archive metadata marking it Pauli-covariant (not WH).
  The source tree contains no dimension archive.

<a id="hoggar"></a>
<a id="conventions-11-hoggar-lines-exceptional-entry-at-d--8"></a>
## Hoggar covariance and exact proof




The Hoggar lines are 64 equiangular lines in `C^8`. They form a SIC (`d² = 64` lines, `|⟨ψ|φ⟩|² =
1/(d+1) = 1/9`), but the group that generates them is the **3-qubit Pauli group**, not the cyclic
Weyl–Heisenberg group of section 3. They are therefore *not* one of the 174 WH rays of the catalogue
and are listed separately as an exceptional entry.

- **Group and convention.** `C^8 = (C²)^{⊗3}`, `|x⟩ = |x_1⟩ ⊗ |x_2⟩ ⊗ |x_3⟩` with `x = 4 x_1 + 2 x_2
  + x_3`: tensor factor 1 is the most significant bit, following the [basis
  rule](sicx.md#conventions-1-hilbert-space-basis-and-qubit-ordering). With `X|x⟩ = |x+1 mod 2⟩`,
  `Z|x⟩ = (−1)^x |x⟩` on one qubit, `P_{a,b} = (X^{a_1} Z^{b_1}) ⊗ (X^{a_2} Z^{b_2}) ⊗ (X^{a_3}
  Z^{b_3})`, `a, b ∈ F_2³`, i.e. `P_{a,b}|x⟩ = (−1)^{b·x} |x ⊕ a⟩`: real signed permutations, no
  extra phase. Packed labels: `a = 4 a_1 + 2 a_2 + a_3` (same for `b`).
- **Fiducial.** `ψ_H = (−1+2i, 1, 1, 1, 1, 1, 1, 1)^T / √12`, global phase as written (no anchor
  gauge). The 64 lines are `P_{a,b} ψ_H`.
- **What is proved.** `⟨v|v⟩ = 12` for `v = √12 ψ_H`, `|⟨ψ_H|P_{a,b} ψ_H⟩|² = 1/9` for all 63 `(a,b)
  ≠ 0`, and the 64 lines are pairwise equiangular with `1/9`; checked in exact integer arithmetic
  over `Z[i]` (status **proved**), by the CTest `sic_test_data_hoggar_data` (C++) and the optional
  `cpp/checkers/gp/hoggar_verify.gp`.
- **Handling.** `d = 8` WH SICs use the general producer, decoder and SICX format. The Hoggar lines
  are an independent exception with a closed-form fiducial: no SICX, no producer or decoder, no
  `exact-data` step; the files in `data/d8/hoggar/` were added directly as data from the closed form
  (not generated by code in this repository) and are checked by the tests above.
- **Relation to the WH SICs.** The `d = 8` WH SICs are covariant under `Z_8 × Z_8`; the Hoggar lines
  are not (their symmetry group contains the Pauli group, a different group). The WH displacement,
  Clifford-label and anchor rules do not apply to them.



## Exact data downloads

<a id="transport-getting-the-exact-data"></a>
## Getting the exact data

Numerical data (JSON, NumPy, MATLAB, HDF5, CSV and manifests) is in git under
`data/`. Each canonical exact object is one complete `.sicx` file. GitHub
Release assets carry contiguous transport parts, each **at most 1,900,000,000
bytes**, cut at physical chunk boundaries. Part offsets are explicit; part
sizes vary. A small file has one part. The last part holds all finalization
data, LDIR, PDIR and the footer. Concatenating the parts reproduces the file.

The normative final-format contract is
[sicx-spec.md](sicx.md#spec-sicx-10-final-exact-weylheisenberg-sic-fiducial-format), especially
§§5–6 and §10b. The tools implement the container/transport contract; offline tests do not establish
real release-download integration. SICX 1.0 readers reject every undefined chunk type regardless of
flags; extensions require `required_features` bits in a later version.

<a id="transport-the-exact-data-build-target"></a>
### The `exact-data` build target

Needs a FLINT build. Off by default; a normal build and CTest never touch the network.

    cmake -S cpp -B build -DSIC_ENABLE_FLINT=ON -DSIC_DATA_MULTIPLETS="d1024/m1;d2048/m1"
    cmake --build build --target exact-data

For each multiplet this fetches a verified SICX into `build/data/d<d>/m<N>/`
and generates `exact.json` on demand with `sic-inspect --exact-json` where
power-basis conversion is practical (measured through d16, not large d).
Options: `SIC_DATA_SOURCE` (base URL/local parts directory, default the release
in the manifest), `SIC_DATA_ROOT` (default `data/`), `SIC_DATA_JOBS` (parallel
downloads, default 4), `SIC_DATA_EXACT_JSON_FACTOR` (disk forecast multiplier,
default 2). Disk admission covers the SICX, parts in flight and the JSON
forecast; that multiplier is not a proved size bound for large power bases.

<a id="transport-sic-container-fetch-repair-split"></a>
### `sic-container fetch`, `repair`, `split`

<!-- shell:template -->
```sh
sic-container fetch d8/m1 --data-root data
sic-container repair d8/m1 --data-root data
sic-container split multiplet.sicx --out parts --asset-prefix d8-m1
```

All tools support `--help` (stdout, exit 0). See the [complete CLI
reference](library.md#command-argument-contract) for option scopes and exit codes. Fetch and repair
default to `--data-root data`, `--out build/data/d<d>/m<N>`, `--name multiplet.sicx` and `--threads
4` (range 1..64). Only fetch accepts `--force` and `--extra-bytes`. Split uses the fixed
1,900,000,000-byte ceiling; `--part-size` accepts only that value.

- **fetch** validates the trusted parts manifest, works under a private name,
  and reuses only ranges whose complete part SHA-256 matches. It downloads
  each missing part, checks its hash, writes it at the manifest's explicit
  offset, and syncs. After whole-file SHA-256, both roots and full container
  verification pass, it syncs and atomically renames to the final name. An
  interrupted fetch resumes by running it again. Extra temporary disk use is
  at most `J` part-sized files, in addition to the work file.
- **repair** works under a private `.repairing` name, checks each explicit
  part range, refetches only missing/bad parts and repeats all final checks
  before renaming back. It reports the repaired indices. A partial or sparse
  work file is never exposed as a verified SICX.
- **split** validates the complete file and deterministically packs whole
  chunks to the ceiling, reserving the complete finalization/directory/footer
  suffix as its own last part unless the entire file fits one part (§10b.1).
  It writes `d<d>-m<N>.sicx.part-NNNN` (at least four index digits) and
  `multiplet.sicx.parts.json`, the latter last. An indivisible chunk or suffix
  larger than the ceiling is a split error. `--emit-and-upload CMD` writes,
  hashes, uploads, confirms remote size and deletes one part at a time;
  `--confirm-size-cmd` receives the asset name in `SIC_PART_ASSET`. Resume
  skips only previously uploaded and confirmed parts of the same snapshot.

Full fetch/repair can use the `curl` executable without a shell. The stable
asset URL is `https://github.com/<repo>/releases/download/<tag>/<asset>`;
`--source` supplies a mirror base URL or local directory. A full fetch and
an HTTP Range read are separate strategies; a server ignoring Range must
never silently switch a partial reader to full download.

<a id="transport-parts-manifest"></a>
### Parts manifest

`sicx-transport-parts/1.0` is canonical JSON, all numbers strings, with exactly
`schema`, `alignment: "sicx-chunks/1.0"`, `file: "multiplet.sicx"`, `size_bytes`,
`sha256`, `exact_root`, `file_root`, `part_size_bytes: "1900000000"`,
`release {repository, tag}` and `parts`. Each part has exactly `index`, `asset`,
`offset`, `size_bytes`, `sha256`. See §10b.1 for full bounds/name grammar.

Readers check indices 0 through n−1, positive sizes no larger than the ceiling, `offset[0]=0`,
`offset[i+1]=offset[i]+size[i]`, final end equal to file size, correct unique asset names and no
gaps/overlaps. They check cut positions against PDIR and ensure the entire
finalization/LDIR/PDIR/footer suffix is in the last part. There is **no** `offset = i·part_size`
invariant and no rule that earlier parts have equal size. Validate each explicit offset and chunk
boundary rather than deriving offsets from a fixed stride. Full fetch checks every part hash and the
whole-file hash; a partial read cannot claim either from a small range.

The trusted catalogue binds a manifest snapshot, exact root, file root, file
size and whole-file hash. The exact root identifies the canonical embedded
exact presentation, independently of ray selection, physical grouping and
transport. The file root binds PDIR, LDIR and all physical payload digests,
including finalization and provenance. A root obtained only from the same
untrusted server proves self-consistency, not authenticity.
A trusted file root, after verification of its complete LDIR binding, also
makes the recomputed exact root trusted transitively. A trusted whole-file
SHA-256 makes both roots trusted only after every byte of that same snapshot
has been hashed. Exact-root trust alone does not authenticate RAYS/BALL.

<a id="transport-remote-partial-access"></a>
### Remote partial access

A remote reader fetches the file header, last 112-byte footer and complete
LDIR/PDIR chunks (including their headers), checks geometry and both roots,
establishes trust by one of the routes above, authenticates EXMT and derives
required leaves from the requested words and coordinates. Bootstrap does not
fetch every ordinary chunk header: check it against PDIR when its chunk is
read. For unread finalization, check the directory's `RAYS BALL+ PREC?`
skeleton, BALL indices and coordinate spans; defer RAYS-dependent ray count,
width and semantic checks until the required payloads are read (§6.2).
It then fetches whole physical chunks and checks both the encoded-payload digest in
PDIR and every decoded logical leaf's domain-separated digest in LDIR before
using values. No per-request audit paths or published sidecar index are needed.
A partial result lists unread headers/payloads and deferred checks; it does
not claim full verification or a complete-file hash from a subset. Container
verification alone does not establish BALL-to-exact containment, binary64
agreement or the field/group premises. BALL containment is the certified
finalizer's obligation (§5.2); algebraic certificates are separate.

Map logical ranges using the manifest's actual part offsets. Send one
part-local inclusive Range per request with `Accept-Encoding: identity` and
no transparent decompression. Require **206** and exactly one matching
`Content-Range: bytes start-end/part_size`, known total equal to that part's
size, and an exact body length. Reject **200 at its headers**, missing/wrong/
duplicate Content-Range, multipart/encoded bodies, truncation and extra bytes.
Follow bounded HTTPS redirects with normal TLS checks; do not forward
credentials across origins. Pin one immutable source snapshot; a source
change requires a fresh bootstrap or an explicit error. §10b.3 gives the full
checkable HTTP contract and bounded-buffer requirements.

Cold decoding of one **complete exact ray currently needs essentially all
exact data**, because every representative is used; selecting one ray does
not divide the transfer by the ray count. Coordinate subsets and ratio spot
checks may save substantial transfer. BALL-only numerical reads avoid exact
elements and require **file-level trust** (a trusted file root or a fully
verified trusted whole-file hash). The access planner can choose a
full sequential fetch when the dependency set and bandwidth favor it.

Cache canonical exact leaves by `(exact_root, LDIR index, full LDIR entry)`;
reuse across grouping/finalization only after digest validation. Cache encoded
chunks, directory/footer snapshots, finalization and pending ranges by file
root with their PDIR entry and matching source descriptor. Never reuse BALL
solely because the exact root matches. Sparse holes are missing bytes;
incomplete fragments stay unverified, and cached bytes are rehashed on use.
A disk cache is private bookkeeping, not a partial `.sicx` publication format.

<a id="transport-integration-status"></a>
## Integration status

The intended repository is `Hayata-Yamasaki-Group/sic-povm`, tag `v1.0.0`. Project release
downloads, authenticated access and long transfers remain untested integration gates. Offline tests
use local fixtures and a Node HTTP server. No public-server request-rate or availability guarantee
is assumed.

<a id="remote-decoding"></a>
<a id="transport-remote-decoding-and-random-access"></a>
### Remote decoding and random access

The reader uses the final SICX 1.0 EXMT/LDIR/PDIR layout, 112-byte footer and
chunk-aligned parts manifest. Opening validates both complete directories and
their roots, EXMT and the derived logical/physical coverage. A requested
physical chunk passes its header, payload digest, every logical digest and
encoding checks before any constituent leaf is consumed.

The SICX reader and decoder also accept a **local parts manifest** with remote byte access. The SICX
bytes, exact root and certificates are unchanged. Build with libcurl >= 7.81 development headers for
HTTP support (`SIC_ENABLE_REMOTE` defaults to ON when libcurl is found). The numeric core library
does not link libcurl. The CLIs load shared libcurl when opening an HTTP source, so local decoding
does not load it. `-DSIC_ENABLE_REMOTE=OFF` still supports local files and local parts mirrors.
Tests use a local Node HTTP server and never contact GitHub.

API callers using libcurl older than 7.84 must construct HTTP sources before starting application
threads; subsequent reads are thread-safe. The CLIs do this before creating decoder/verification
workers. The loaded HTTP runtime stays initialized for process lifetime, avoiding resolver-thread
unload races.

<!-- shell:template -->
```sh
sic-verify --remote data/d256/m1/multiplet.sicx.parts.json --quick
sic-decode --remote data/d256/m1/multiplet.sicx.parts.json decoded \
  --rays c0 --coordinates 0,128 --threads 2
sic-decode --remote PARTS.json decoded --source /path/to/mirror --cache /path/to/cache
sic-verify --remote PARTS.json --source file:///absolute/path/to/mirror --quick
sic-container read-range --remote PARTS.json 1000 128 --source https://example.org/parts > bytes.bin
sic-container read-range local.sicx 1000 128 > local-bytes.bin
```

See [Command-line tools](tools.md) for the shared remote controls and the decoder selection syntax.
For example, `--coordinates 0-3,7,2` selects 0, 1, 2, 3 and 7 in that order. A noncontiguous
selection writes `c<N>.raw-selected.balls` and lists its indices in `decode.provenance.json`.
Subsets are raw enclosures; full normalization and FP64 rounding require all coordinates. To compare
scheduled and demand-only access, repeat a decode using separate empty caches with `--prefetch-mib 8`
and `--prefetch-mib 0`; local and remote numerical output bytes agree.

The default source is `https://github.com/<repository>/releases/download/<tag>` from the manifest;
`--source URL|DIR` replaces that base. The asset name is appended to it. Redirects preserve Range.
Explicit `http://` sources are supported for local testing; use HTTPS for public mirrors. Each
logical read is split at asset boundaries and at `--range-bytes` (default 1 MiB, maximum 8 MiB). The
reader also fetches the header, footer, complete LDIR/PDIR, EXMT and RAYS. The decoder derives all
tower/action chunks and all chunks of each selected representative from that metadata. A coordinate
is not a same-index stored leaf: it needs its whole representative. Comma-separated coordinates and
inclusive ranges are accepted (for example `--coordinates 0,128` or `0-3,128`); duplicates are
removed and output order is increasing. Contiguous selections use their range filenames.
Noncontiguous selections write `cN.raw-selected.balls`, with their indices in
`decode.provenance.json`'s `coordinate_indices`. These are raw enclosures; normalization needs the
complete ray. `sic-container read-range` writes raw bytes to stdout and transfer statistics to
stderr; its arbitrary output range is a debugging facility, while opening the container still runs
the reader's metadata checks.

At most `--range-jobs` requests run concurrently (default 4, range 1..32). The default
range size is 1 MiB. Each range request
requires **206** and the exact `Content-Range: bytes FIRST-LAST/PART_SIZE`; a 200 response is aborted
at its headers. Wrong ranges, sizes, encodings and malformed redirects fail immediately. Connection
failures, timeouts and HTTP 408/429/5xx retry with exponential backoff (100 ms initially,
`--remote-retries 3` additional attempts, maximum 10). `--remote-timeout-ms` defaults to 30000 per
attempt; the connection timeout is 10000 ms, and stalled bodies also have a low-speed timeout.
Within a retry, a received prefix is retained and the next Range starts at the next byte. Across
process interruptions, complete atomic cache extents survive; at most the in-flight extents need
fetching again. The decoder reserves source staging and metadata headroom inside `--memory-mib`:
`2*jobs*(range_bytes+56) + 2*8 MiB + 512*cache_max_ranges + 8 MiB`, in addition to its measured
resident baseline and admitted numerical work. This is conservative (about 160 MiB at the defaults),
so tiny remote budgets require smaller range/job/metadata caps or are refused before decoding.
RSS remains subject to allocator/library overhead and is checked with `--check-budget`; the admission
estimate is not a proof of whole-process RSS. `--cache-max-ranges` bounds cache and read-plan metadata
(default 262144, maximum 1048576); exceeding it recommends a full download.

`--prefetch-mib` (default 8, 0 disables, maximum 256) controls physical-order lookahead to the disk
cache. One background scheduler shares demand's staging and request limits; it retains no decoded
chunks. It merges adjacent ranges and gaps up to `--coalesce-gap` bytes (default 4096, maximum 8 MiB),
never crossing a part boundary or the request-size cap. Gap bytes count toward the transfer plan.
The lookahead does not accumulate whole rays in RAM. Quiescing after decode makes the `REMOTE`
transfer/request/buffer statistics final; destruction cancels speculative HTTP and retries.

The default transfer limit is **50% of the logical file**. The decoder preflights all tower/action
chunks and requested representative chunks, plus the needed finalization chunks, before loading the
tower. Full verification preflights every chunk. If the union of missing ranges planned in that
process exceeds `--remote-max-fraction 0.5`, access stops and recommends `sic-container fetch`.
Already cached ranges cost no transfer budget. Small fixtures and some large representative chunks
can exceed the limit even for one coordinate. An explicit fraction in `(0,1]` changes the limit.
Alternatively, `--fetch-full` permits a separate, explicit verified full-download strategy after a
threshold refusal or a rejected HTTP 200 (the Range response itself is always aborted at its
headers):

<!-- shell:template -->
```sh
sic-decode --remote PARTS.json decoded --fetch-full --threads 2
# Or explicitly permit up to 75% as sparse range requests:
sic-decode --remote PARTS.json decoded --remote-max-fraction 0.75 --coordinates 1
```

Full fallback is streamed to a resumable work file, verifies part and whole-file SHA-256 plus the
local reader checks, then atomically publishes a complete cached `full.sicx`. Subsequent processes
can use that verified file offline. Its roots must also agree with any identity already opened
through sparse reads. `--finalize` requires a local complete SICX file; remote access is read-only.
See [full-file transport](#transport-sic-container-fetch-repair-split) for `split`, `fetch` and `repair`.

Full-ray remote decoding streams BODY chunks under the decoder budget. It still transfers essentially
all exact data. When all exact elements have been consumed, the decoder also completes the global
exact canonicality checks, under a serial admitted scratch window, without fetching unrelated BALLs
or PROV. A subset reports those checks as deferred. Field/group/action and SIC premises remain separate.
The library uses `Reader(local_byte_source(path))` or `Reader(remote_byte_source(manifest, options))`
with the same `Decoder` API. `Decoder(reader, options, requests)` admits the full set before tower
loading; the two-argument constructor admits towers first and request dependencies when `decode` runs.
`Decoder::input_chunks` exposes the common dependency planner for additional BALL preflight.
Library callers set `RemoteOptions::memory_budget` to the decoder's memory limit before opening
the Reader, so an early full-fetch fallback uses the same aggregate verification budget as decoding.

<a id="transport-cache-and-integrity"></a>
#### Cache and integrity

The default cache is `$XDG_CACHE_HOME/sicx`, or `$HOME/.cache/sicx`; `--cache DIR` overrides it.
The sparse cache stores independent extents, not a partial SICX file:

```text
DIR/ranges/<exact-root>/<file-root>/<canonical-manifest SHA-256>/
  identity                         # checksummed exact/file root hint
  <file-root>/<offset>-<size>.range # unverified transport fragments
  full.sicx                        # only after explicit full fallback, if used
```

Each extent is one atomic record: magic, logical offset, length, SHA-256 integrity digest and bytes.
The digest binds the manifest identity, record header and range bytes. Every
reused record is rehashed; damaged or truncated records are discarded and
refetched. Temporary writes use unique names, sync before rename and sync the
directory. File-root and canonical-manifest namespaces prevent fragments from
being spliced between physical layouts, finalizations or part manifests sharing
the same exact root. Transport fragments remain unverified until the Reader
checks their complete physical unit and every logical leaf. Cache contents can
be deleted to reclaim disk space; there is no automatic eviction.

The manifest carries both roots. It must itself come from a trusted source to
authenticate them; self-consistent server metadata alone is not a trust anchor.
A sparse subset does not establish the whole-file SHA-256 or a part hash. Supply
independently trusted roots when opening an untrusted manifest:

<!-- shell:template -->
```sh
sic-verify --remote PARTS.json --quick --expect-exact-root HEX --expect-root HEX
```

The file root pins finalization too; the exact root alone permits different
finalizations of the same exact data. For whole-file SHA-256 verification against a trusted manifest,
use `sic-container fetch` (or explicitly enabled full fallback). A trusted exact root
authenticates logical leaves; authenticating finalization additionally requires
a trusted file root or verified trusted whole-file SHA-256.
A real GitHub Releases transfer remains untested here; redirect/range behavior is tested offline.

## Reference vectors and fixtures

<a id="data-tiers"></a>
The source tree contains decimal reference vectors in `cpp/tests/fixtures/core/`, the d2 radical
input in `data/d2/`, exact SICX fixtures in `cpp/tests/fixtures/sicx/`, and normalized
balls/CSV/provenance fixtures in `cpp/tests/fixtures/numeric/`. They are test inputs, not newly
constructed results. The test fixtures under `cpp/tests/` are part of the software and use
[AGPL-3.0-or-later](../LICENSE). The datasets in `data/`, including the d2 radical input, and the
datasets distributed as GitHub Release assets are made available under the
[ODbL v1.0](../LICENSE-ODbL); any rights in their individual contents are licensed under the
[DbCL v1.0](../LICENSE-DbCL).

<a id="reference-vectors"></a>
### Reference text vectors

`cpp/tests/fixtures/core/` contains test oracles, rather than algorithm output (d = 3
closed form; d = 4 derived from published ghost values, cited in the file).
The text format read by `sic-check` is:

```text
d
re_0 im_0
re_1 im_1
...
re_{d-1} im_{d-1}
```

Lines starting with `#` are comments. State the source and WH convention in a
comment header. Coordinates use the standard basis; the vector need not be
normalized. Source citations accompany the reference vectors.

<a id="dimension-two-radical-input"></a>
## Dimension-two radical input

`data/d2/d2.sicrad` begins with `sic-exact-d2-radicals/1.0` and defines

\[
\psi=(\alpha,(1+i)\beta),\qquad
\alpha=\sqrt A>0,\quad \beta=\sqrt B>0,\quad \sqrt3>0.
\]

The records `alpha_squared a b` and `beta_squared a b` mean `A=a+b*sqrt(3)` and `B=a+b*sqrt(3)`,
respectively. Coefficients are exact rationals. The pinned input uses exactly six lines in the order
shown by the record. The independent stabilizer radical reader enforces that layout, including
dimension/radicand and no extra records. The producer’s input parser accepts more permissive record
order and stops at `end`; use the pinned record for reproducibility. Coefficients are exact
rationals; equivalent rational spellings denote the same values. No decimal approximation defines
the artifact.

For the published data,

\[
A=(3+\sqrt3)/6,\qquad B=(3-\sqrt3)/12.
\]

The radical proof uses positivity and these identities on the **parsed**
coefficients in `Q(sqrt(3))`:

\[
A+2B=1,\qquad4AB=1/3,\qquad(A-2B)^2=1/3.
\]

Under `D_p=tau^(p1*p2) X^p1 Z^p2` with `tau=-i`, the nonidentity
displacements are `X,Z,-Y`. Their expectations are `2*alpha*beta`,
`A-2B`, and `-2*alpha*beta`. Thus the identities prove normalization and
all three exact SIC overlap equations. The four orbit projectors divided
by two form the POVM. Arb independently checks consistency with the same
positive branches; its small residual is supplementary to the exact proof.

`sic-construct 1 --out d2.sicx --radicals data/d2/d2.sicrad` reads this record (the default uses the
embedded pinned record). `sic-certify-stabilizer --d2-exact data/d2/d2.sicrad STABFILE` checks the
d2 symmetries exactly. The optional exact-JSON GP check also exercises the d2 SICX; its embedding
limitations remain as stated in verification. The parsed polynomial identities above are the
mathematical basis of the radical proof, distinct from the supplementary Arb residual.

Writing refuses an existing destination. This is the explicit d=2 base case,
not a ghost-tower export, a new classification result, or a proof of the
external inputs used by the larger-dimension construction.

<a id="formats-2-enclosing-vectors-balls-and-collections"></a>
## Decoded ball-vector format

Decoded coordinates are stored as rigorous Arb balls in a text format
`sic-vector-balls/1.0`:

```
sic-vector-balls/1.0
d 1024
RE_DUMP | IM_DUMP          one row per coordinate j = 0 .. d-1 (d rows)
...
end
```
Each `*_DUMP` is Arb's `arb_dump_str`: midpoint mantissa and exponent, then radius
mantissa and exponent, four whitespace-separated hexadecimal tokens (value =
`mantissa * 2^exponent`; `0` mid prints as `0`). Load with `arb_load_str` or the
public data reader. A file of `d`
coordinates has `d + 3` lines.

<a id="formats-3-fp64-data-the-files-most-users-want"></a>
## Pinned FP64 reference fixtures

The numerical test fixtures use the following schema and error contract. These are distinct from the
correctly rounded current ray JSON levels.


Each enrolled fixture directory contains:

| file | content |
| --- | --- |
| `fiducial-fp64.csv` | header `index,real,imag`, then `d` rows; 17-significant-digit decimal; coordinate `j` of the normalized, phase-fixed fiducial |
| `normalized-gauged.balls` | the `sic-vector-balls/1.0` enclosure of the exact normalized/gauged vector |
| `metadata.json` | schema `sic-fiducial-fp64/1.0`; see below |
| `SHA256SUMS` | `sha256sum` list of the three files above (relative names) |
| `COMPLETED` | `sic-fiducial-data-complete/1.0` plus the SHA-256 of `SHA256SUMS` |

**Normalization.** For the exact fiducial `v`, with anchor index `k` (nonzero
coordinate, recorded in metadata as `anchor`): `w_j = v_j / ||v|| * conj(v_k)/|v_k|`.
Thus `||w|| = 1` and `w_k = |v_k|/||v|| > 0` (the anchor's imaginary part is
exactly 0). Fiducials are determined by this only up to the symmetries of the
construction (selector choice, Clifford action); see the premises file.

**Error contract.** Each `(real_j, imag_j)` is the nearest binary64 midpoint of
an enclosure of `w_j`, and Arb certifies `|w_j - q_j| <= 2^-50` for every
coordinate (complex distance; the ceiling is recorded as
`coordinate_complex_error_ceiling "2^-50"`). It is not a claim of correct
rounding, and the binary64 vector does not have exact norm 1.

**`metadata.json` keys:** `schema`, `dimension`, `basis` (the status/scope sentence: conditional or
independent, numerical-bound caveat), `selector` (order, class label, Smith word, coordinate order),
`association_contract`, `wh_convention`, `gauge`, `anchor`, `working_bits` (256), `native_workers`,
`coordinate_complex_error_ceiling`, `rounding`, `zero_policy`, `data_files`,
`converter_binary_object_id`, `native_flint_version`, `reproduction_command_template`,
`portable_provenance`, `fp64_point_squared_norm_arb_dump`,
`fp64_point_maximum_squared_overlap_deviation_arb_dump`, `point_verification_scope`,
`descriptor_sha256`, `pins` (role, SHA-256 artifact identifier, `sha256`, `object_id` for the
vector, export, producer binary, producer source, source mapping and premises file) and
`components`.

**Audit columns.** `components[j]` carries `index`, `real_binary64_hex`, `imag_binary64_hex`
(IEEE-754 hex of the CSV values, exact) and `complex_error_arb_dump` (the Arb upper bound on `|w_j -
q_j|`). Independent readback re-parses the CSV and checks error and gauge consistency. Its log has
the form

```
PASS CSV independent reparse/error/gauge; d=1024 anchor=569 complex error<=2^-50
maximum_complex_error=<arb dump>
binary64_point_squared_norm=<arb dump>
binary64_point_deviation=<arb dump>
Diagnostic of approximation only; no exact SIC equality inferred.
```
`binary64_point_deviation` encloses the WH overlap deviation of the stored point;
it is a diagnostic of the approximation, not an exact SIC identity.

<a id="integrity-and-package-checks"></a>
## Integrity and package checks

A checksum identifies bytes; it proves no SIC identity. Check a release or fixture list from the
directory against which its relative paths were written. For a release containing the optional root
sidecar:

<!-- shell:template -->
```sh
cd data
sha256sum -c sha256sums.txt
```

For a downloaded archive, first verify its trusted sidecar, extract into a fresh directory, and
check every manifest entry from its declared base directory. A sidecar may contain either a bare
filename or a path relative to its parent. Inspect that path before choosing the working directory.
Missing entries mean incomplete verification. Checking only the files present, or ignoring missing
entries, cannot establish completeness. Entries present in an archive but absent from the manifest
are unauthenticated by that manifest. For an absolute-path sidecar, compare the digest field to a
locally computed hash; the original path is not portable. Per-ray `SHA256SUMS` checks are additional
to the outer archive manifest.

Transport manifests authenticate explicit part offsets and lengths; concatenation is valid only
after all part hashes and the whole-file SHA-256 agree. Full fetch verifies both roots and publishes
atomically. See the download section above for repair and subset trust.

<a id="availability-and-storage"></a>
## Availability and storage

Hosted asset URLs and a DOI are not yet assigned. The multi-GB exact files and full collection
packages are not committed source inputs. A source checkout contains fixtures and manifests; the
proposed release tree is not evidence that every target ray has been published. d1024 standard and
d2048 remain unpublished. Use the actual manifest byte lengths for disk admission, including parts
in flight, the final work file and any on-demand power-basis JSON. A JSON disk forecast multiplier
is an estimate.

<a id="formats-5-reconstructing-a-fiducial-vector"></a>
## Reconstructing a vector and its SIC

From the FP64 CSV (no special software):

1. Read `d` rows `(real_j, imag_j)`; set `psi_j = real_j + i imag_j`, `j = 0..d-1`. (`psi` has norm
   1 up to the certified error; renormalize if you need unit norm to floating-point accuracy.)
2. The SIC is `{ D_p psi : p in {0..d-1}^2 }` with
   `(D_p psi)_k = tau^(p1 p2) * omega^(p2 (k - p1)) * psi_{(k - p1) mod d}`,
   `tau = -exp(i pi/d)`, `omega = exp(2 i pi/d)`. The POVM elements are
   `(1/d) |D_p psi><D_p psi|`, and `|<psi|D_p psi>|^2 = 1/(d+1)` for `p != 0`
   up to the error bounds above.

For a current `c0.csv`, the binary64 diagnostic is:

<!-- shell:template -->
```sh
{ echo 8; awk -F, 'NR>1 {print $2, $3}' data/d8/m1/c0.csv; } | sic-check - 1e-10
```

This checks the approximation only. `sic-decode` obtains certified enclosures from exact SICX;
`sic-data write` exports numerical files. Keep the exact-source premises and root-bound status with
the data.

<a id="formats-8-using-the-data-in-other-software"></a>
## Numerical interchange operations

Use `sic-data write RAY.json OUTDIR --formats npy,mat,csv` to write the
binary64 values of a current ray JSON file. Optional HDF5 output is selected
at configure time. `sic-data check RAY.json FILE...` compares each numerical
file bit for bit, using separate readback readers. Inputs must have consistent
hexadecimal and decimal FP64 values. These operations do not establish an
exact SIC proof.

`sic-data bundle OUT.npz D RAY.json...` produces a deterministic, sorted,
store-only ZIP archive of complex128 NPY members, with a JSON archive comment.
Use `--exceptional RAY.json GROUP` for exceptional covariance groups and
`sic-data check-bundle FILE.npz RAY.json... --exceptional RAY.json` to read
back all members. `sic-check-balls VECTOR.normalized.balls --bits 100`
checks a rigorous numerical overlap bound. It requires complete ball data.

<a id="formats-81-loading-snippets"></a>
### Additional loading recipes

NumPy / SciPy (CSV or `.npy` or `.mat`):

```python
import numpy as np
psi = np.load("fiducial.npy")                                   # complex128, shape (d,)
# or: c = np.loadtxt("fiducial-fp64.csv", delimiter=",", skiprows=1); psi = c[:, 1] + 1j * c[:, 2]
# or: from scipy.io import loadmat; psi = loadmat("fiducial.mat")["psi"].ravel()
d = psi.size; psi = psi / np.linalg.norm(psi)                   # unit norm to rounding; the stored vector has norm 1 only up to its
                                                                # coordinate error: typically ~1e-16, guaranteed only to 2^-50 per coordinate
```

Julia:

```julia
using NPZ, MAT
psi = npzread("fiducial.npy")                  # Vector{ComplexF64}
psi = vec(matread("fiducial.mat")["psi"])      # same values from the .mat file
# decimal balls, e.g. with Arblib.jl: each bracket is an Arb string, Arb("[0.60044826... +/- 8.05e-77]")
```

MATLAB / GNU Octave (LTFAT works on the same column vector):

```matlab
S = load('fiducial.mat');  psi = S.psi;  d = S.d;       % psi is d x 1 complex double
% The .npy file is for Python/Julia/C++. MATLAB readers of .npy (e.g. npy-matlab) are not known to support the
% complex dtype '<c16'; this was not tested, so use fiducial.mat (or the CSV) in MATLAB and Octave.
% LTFAT: with f = g = psi, c = dgt(psi, psi, 1, d) has |c(m+1,n+1)|^2 = |<psi, D_(n,m) psi>|^2
%        (a = 1, M = d), i.e. a d x d array equal to 1/(d+1) except entry (1,1) = 1.
```
The LTFAT statement follows from LTFAT's documented `dgt` formula and was not run here
(no LTFAT or Octave on the build host); MATLAB R2022b could not be run either (license
error), so the `.mat` files were validated with SciPy `loadmat` and the C++ reader only.

Qiskit and QuTiP (d = 2^n; [qubit ordering](sicx.md#conventions-1-hilbert-space-basis-and-qubit-ordering)):

```python
from qiskit.quantum_info import Statevector
sv = Statevector(psi)                         # n qubits, qubit 0 = least significant bit of j

import qutip
n = int(np.log2(d))
q = qutip.Qobj(psi.reshape(-1, 1), dims=[[2] * n, [1] * n])   # tensor factor 0 = MOST significant bit
q1 = qutip.Qobj(psi.reshape(-1, 1))                           # or: one d-level system (natural here)
```

<a id="formats-83-rebuilding-the-full-sic-efficiently"></a>
### Rebuilding a full SIC efficiently

The SIC is `{D_(a,b) psi}`: `d^2` vectors of length `d`. Do not form the `d^2` projectors
(`d^4` entries; 1.1 TB at d = 512). Even the matrix of all vectors needs `d^3 * 16` bytes
(2 GB at d = 512, 17 GB at d = 1024), so generate it in blocks of fixed `a`:
`(D_(a,b) psi)_k = tau^(-ab) omega^(bk) psi_(k-a)`, which for fixed `a` is a DFT matrix times
the rolled vector, rows scaled by `tau^(-ab)`. Quantities are best computed without
building vectors at all, using one FFT per shift `a`:

```python
import numpy as np
b = np.arange(d)
def tau_pow(n):
    """tau^n = exp(i pi n (d+1)/d) for integer n, with the exponent reduced mod 2d in exact integers
    (np.exp(1j*pi*n/d) or tau**n lose about 1e-10 of phase at d = 1024)."""
    return np.exp(1j * np.pi * (((np.asarray(n, dtype=np.int64) * (d + 1)) % (2 * d)) / d))
def overlaps_with_state(psi, phi):
    """<D_(a,b) psi | phi> for all a, b (array [a, b]); O(d^2 log d)."""
    out = np.empty((d, d), complex)
    for a in range(d):
        u = np.conj(np.roll(psi, a)) * phi                 # conj(psi_{k-a}) phi_k
        out[a] = tau_pow(a * b) * np.fft.fft(u)
    return out        # SIC-POVM outcome probabilities: |out|^2 / d  (sum to 1 for a unit vector phi)
def sic_deviation(psi):
    worst = 0.0
    for a in range(d):
        s = np.abs(np.fft.ifft(np.conj(psi) * np.roll(psi, a)) * d) ** 2   # |<psi, D_(a,b) psi>|^2 over b
        if a == 0: s[0] = 1 / (d + 1)                                       # skip the norm entry
        worst = max(worst, np.abs(s - 1 / (d + 1)).max())
    return worst
```
For tomography or channel simulation, stream vectors using the
[frame API](library.md#frame-dft-and-clifford-signatures) and accumulate the required quantities.

<a id="clifford-2-sic-overlaps"></a>
## Overlap-table output

```
sic-overlaps FILE [--tol T=1e-10] [--vectors] [--table OUT.bin]
```
Prints `d`, input norm, norm defect, `max_{p != 0} | |<psi|D_p psi>|^2 - 1/(d+1) |` with the worst
label, the Parseval defect `|sum_p |<psi|D_p psi>|^2 - d|` and timings. `--vectors` streams all
frame vectors (norm defect, checksum); `--table` writes the overlaps as raw native-endian
complex128, index `p1*d + p2`. Exit 0 within tolerance, 1 for failed checks or input/runtime errors,
2 for usage errors.

<a id="stab"></a>
<a id="clifford-4-stabilizer-data-sic-stabilizer-datasymmetry"></a>
## Stabilizer table format

```
sic-stabilizer FILE [--tol T] [--entry-error E]
sic-stabilizer --dim D [--catalogue FILE] [--repo DIR] [--out FILE]
```
Computes `S(phi) = {[U] in PEC(d) : |<phi|U phi>| = 1}` (Scott2010symmetric eq. (4.4)) with
`clifford_maps(phi, phi, find_all)`. `S` embeds into ESL(2, Z_d) x {+-1} via `(F mod d, det)`: if
two elements share `(F mod d, det)`, their quotient is unitary with `F = I mod d`, hence a
displacement `D_chi` (Appleby2005symmetric), which fixes no SIC fiducial unless `chi = 0` because
`|<phi|D_chi phi>|^2 = 1/(d+1)`; the sign is kept explicitly since `det = -1 = 1 mod 2` at d = 2. So
group order, element orders and the closure check are exact integer computations on `(F mod d,
det)`.

File format `data/d<D>/stabilizers.txt` (`sic-stabilizers/1.0`), `#` comments, then per ray

```
ray LABEL d D order ORDER status STATUS group_order N unitary_order Nu closed yes|NO exhaustive yes|NO
    max_overlap_error X z_tol T z_error_bound B max_element_z_deviation Z max_element_residual R
    min_rejected_z_deviation Zr min_rejected_residual Rr level_set_sizes L candidate_pairs C
elem LABEL INDEX det +1|-1 F a b c e chi x y order K trace t [zauner] z_dev Z residual R
```
(the `ray` record is one line). `F = [[a,b],[c,e]]` is the canonical lift in ESL(2, Z_dbar)
(lexicographically smallest entries in `[0, dbar)` with the right determinant), the element is
`U = D_chi U_F`; `order` is the order in the group; `trace` is `tr F mod dbar`; `zauner` marks
order-3 unitary elements with `tr F = -1 mod d` (Appleby's canonical order-3 elements,
Scott2010symmetric section 4). `exhaustive yes` means `2 * z_error_bound <= z_tol`;
`min_rejected_*` are -1 when no candidate was rejected. Files exist for d = 2 .. 1024 (70 rays).

Certification `sic-certify-stabilizer STABFILE [--d2-exact data/d2/d2.sicrad]`
(FLINT): for each non-identity element, a ball enclosure (384 bits, Appleby's formula as a direct
sum) of `1 - |<phi|U phi>|^2/<phi|phi>^2`; pass if the bound is below 2^-100.
The default release catalogue lists `c<N>.json`; the checker independently reads
its HP decimal midpoints/radii as exact rationals and encloses them outward in Arb.
Explicit TSV catalogues instead select adjacent `normalized-gauged.balls` inputs.
The retained shared tables keep their original ray labels and use those explicit
TSV associations in tests. Generate a table with `sic-stabilizer --dim 2` to check
the default release ray with its canonical `d2-m1-c0` label.
With `--d2-exact` the d = 2 elements are also verified exactly in FLINT `qqbar`
from the radical artifact (`U psi` proportional to `psi`).

<a id="clifford-findings"></a>
### Recorded stabilizer orders

| d | rays | stabilizer order (unitary part) |
| --- | --- | --- |
| 2 | d2 | 6 (3), S_3 |
| 4 | d4 | 6 (3), cyclic |
| 8 | d8 / d8-maximal | 3 (3) / 12 (6), cyclic |
| 16 | 2 rays | 3 (3) each |
| 32 | 2 rays | 3 (3) each |
| 64 | 4 rays | 3 (3) each |
| 128, 256, 512, 1024 | 12, 12, 24, 10 | 3 (3) each, Zauner type |

Apart from the d2 stabilizer S_3, the recorded stabilizers are cyclic of order a multiple of 3 with
a Zauner-type canonical order-3 element, and the orbit counts and |S| at d <= 64 agree with the
published tables. For d = 2^s >= 16, the extra symmetries listed there (`F_a`: d = 9k+3; `F_b`: d =
k^2-1; `F_c`: d = (3k+-1)^2+3) cannot occur, so |S| = 3 is the expected value. Margins: accepted
elements have z deviation <= 5.0e-12 and residual <= 1.6e-15; rejected candidates (d = 4, 8 only)
fail by z deviation >= 0.9; at d = 2 one z-preserving `G` is rejected on the vectors (best residual
0.18). Ball certification of all 155 non-identity elements of the 70 rays (384 bits): every bound
`|1 - |<phi|U phi>|^2| <= 3.5e-73` (certified numerical bound for every vector in the balls, not an
exact identity; 15 s in total, d = 1024 9 s). Exact: the six elements of the d = 2 stabilizer are
verified in `qqbar`. No exact verification was attempted for d >= 4 (the tower exports would need an
exact Clifford action in the tower field). Which of our d = 16..64 rays corresponds to which
Scott-Grassl label has not been determined.

The production tools use the [shared CLI conventions and full option
reference](library.md#command-argument-contract).

## Schemas

Retained emitted metadata schemas are in [schemas/](schemas/):

- [Ray JSON](schemas/sic-fiducial-json-1.schema.json) and [all-ray
  JSON](schemas/sic-fiducial-json-all-1.schema.json).
- [Multiplet manifests](schemas/sic-multiplet-manifest-1.schema.json), [transport
  parts](schemas/sicx-transport-parts-1.schema.json), and [text-parts
  indexes](schemas/sicx-text-json-parts-1.schema.json).
- [NPZ comments](schemas/sic-npz-bundle-1.schema.json) and [Hoggar exact
  data](schemas/sic-hoggar-exact-json-1.schema.json).
- [CLI version JSON](schemas/sic-tool-version-1.schema.json), [decoder
  provenance](schemas/sic-decode-provenance-1.schema.json), and [numeric fixture
  metadata](schemas/sic-fiducial-fp64-1.schema.json).

Each family has one major-1 schema file; use the exact identifier accepted by its reader. Schema
validity is separate from numerical containment and exact SIC proof.

The following concrete example paths are produced by the export or download
recipes on this page; they need not be present in a source checkout.

<!-- generated-path: data/d8/all.npz -->
<!-- generated-path: data/d8/m1/c0.npy -->
<!-- generated-path: data/d8/m1/c0.json -->
<!-- generated-path: data/d8/m1/c0.mat -->
<!-- generated-path: data/d8/m1/c0.h5 -->
<!-- generated-path: data/d256/m1/multiplet.sicx.parts.json -->
<!-- generated-path: data/d8/m1/c0.csv -->
