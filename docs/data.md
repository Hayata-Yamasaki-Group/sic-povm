# Data guide

This guide describes the released data: the folder layout, every file format, loading recipes,
the exact-data downloads and the integrity checks. The basis, qubit order, displacement operators,
normalization, anchors and labels are defined normatively in the
[mathematical conventions](sicx.md#mathematical-conventions). [Verification](verification.md)
states what is proved, certified or conditional for each file, and [reproduction](reproduce.md)
gives the commands that construct the data.

## 1. The data tree and `catalogue.json`

The release consists of two parts:

- the numerical data in the repository folder `data/`: for each Weyl–Heisenberg SIC-POVM fiducial
  vector, its coordinates as correctly rounded binary64 and binary32 numbers and as certified
  decimal ball enclosures, the multiplet manifests, the symmetry tables and the catalogue;
- the exact data: one SICX file per multiplet, distributed as parts attached to the GitHub Release
  `v1.0.0` ([exact data downloads](#transport-getting-the-exact-data)).

| Multiplet | Dimension `d` | Quadratic order | Fiducial vectors | SICX file bytes | Release asset parts |
| --- | ---: | --- | ---: | ---: | ---: |
| `d2-m1` | 2 | standard | 1 | 3,211 | 1 |
| `d4-m1` | 4 | standard | 1 | 6,361 | 1 |
| `d8-m1` | 8 | maximal | 1 | 7,821 | 1 |
| `d8-m2` | 8 | standard | 1 | 11,992 | 1 |
| `d16-m1` | 16 | standard | 2 | 23,379 | 1 |
| `d32-m1` | 32 | standard | 2 | 55,110 | 1 |
| `d64-m1` | 64 | standard | 4 | 438,417 | 1 |
| `d128-m1` | 128 | maximal | 2 | 676,549 | 1 |
| `d128-m2` | 128 | standard | 10 | 7,173,685 | 1 |
| `d256-m1` | 256 | standard | 12 | 47,745,553 | 1 |
| `d512-m1` | 512 | maximal | 6 | 77,752,179 | 1 |
| `d512-m2` | 512 | standard | 18 | 665,733,653 | 1 |
| `d1024-m1` | 1024 | maximal | 10 | 748,193,718 | 1 |
| `d1024-m2` | 1024 | standard | 40 | 11,726,032,122 | 8 |
| `d2048-m1` | 2048 | standard | 64 | 128,506,739,686 | 69 |

The 15 multiplets contain 174 fiducial vectors, pairwise inequivalent under the extended Clifford
group within each dimension ([verification](verification.md#verification-1-claims-and-their-status));
the 90 parts total 141,780,593,436 bytes. The Hoggar lines in `data/d8/hoggar/` are an additional exceptional
entry ([section 8](#data-8-the-hoggar-lines-datad8hoggar)).

```
data/
  catalogue.json                     every dimension, multiplet, fiducial vector and file (sizes, SHA-256)
  sha256sums.txt                     SHA-256 of every other file in data/, including the catalogue
  d<d>/
    all.npz                          all fiducial vectors of the dimension
    stabilizers.txt                  extended Clifford stabilizers (symmetry table)
    m<N>/                            one multiplet
      manifest.json                  multiplet id, fiducial vectors, Smith invariant factors, SICX digests
      multiplet.sicx.parts.json      parts manifest of the exact SICX file (GitHub Release assets)
      c<N>.json                      one fiducial vector: binary32, binary64, decimal balls and metadata
      c<N>.npy  c<N>.mat  c<N>.h5  c<N>.csv
  d2/d2.sicrad                       exact radical input of the dimension-two closed form
  d8/hoggar/                         the Hoggar lines (not Weyl–Heisenberg covariant)
```

Names use lowercase letters, digits and underscores, with dots only before file extensions. The
dimension appears once as a `d<d>` folder without padding; `d2/d2.sicrad` keeps its basename because
the SICX provenance of dimension 2 records it. Multiplets `m<N>` are numbered by increasing conductor
of the real quadratic order used in the construction (so the maximal order is `m1` where it differs
from the standard order); `sic-construct S --orders` lists the orders and their conductors. The
labels `d<d>`, `m<N>`, `c<N>` and the full id `d<d>-m<N>-c<N>` are defined in the
[label convention](sicx.md#conventions-7-labels). Test inputs live in `cpp/tests/fixtures/`; they are
not release data and are never installed as such.

**`catalogue.json`** (schema `sic-release-catalogue/1.0`, canonical JSON, every number a decimal string):

| key | content |
| --- | --- |
| `release` | `repository`, `tag` |
| `dimensions[]` | `d`; `files[]` (per-dimension files); `multiplets[]` |
| `multiplets[]` | `id` (`d<d>-m<N>`), `labels`, `smith` (Smith invariant factors of `Gal(L/K)`), `files[]`, `sicx` (`size_bytes`, `sha256`, `merkle_root`, `exact_root`, `parts_file`, `assets[]` with `asset`, `size_bytes`, `sha256`), `rays[]` |
| `rays[]` | `id` (`d<d>-m<N>-c<N>`), `label` (`c<N>`), `sicx_ray_label`, `exponents` (Galois element), `accuracy_bits`, `files[]` |
| `exceptional[]` | the Hoggar entry: `id`, `d`, `group`, `status`, `provenance`, `manifest`, `files[]`; `wh_covariant` and `counted_in_ray_total` are `"false"` |
| `files[]` | `path` (relative to `data/`), `bytes`, `sha256` |

To find a fiducial vector, select the dimension by `d`, the multiplet by `id` and the fiducial
vector by `id`; its `files[]` give the paths and SHA-256 digests of `c<N>.json` and the binary
files.

`manifest.json` (schema `sic-multiplet-manifest/1.0`) holds the same multiplet record for one
folder: `id`, `d`, `multiplet`, `labels`, `smith`, `rays[]` (`id`, `label`, `sicx_ray_label`,
`exponents`, `accuracy_bits`) and `sicx` (`parts_file`, `size_bytes`, `sha256`, `merkle_root`,
`exact_root`).

<a id="data-2-the-ray-file-cjson"></a>
## 2. The ray file `c<N>.json`

Schema `sic-fiducial-json/1.0`
([schemas/sic-fiducial-json-1.schema.json](schemas/sic-fiducial-json-1.schema.json)). One canonical
JSON document per fiducial vector, generated from the verified SICX file. **Every number is a
string**; complex values are pairs `[re, im]`; coordinates are in the order `0, …, d−1`; the vector
is the normalized, anchor-phased fiducial vector `w` of the
[normalization convention](sicx.md#conventions-4-sic-condition-normalization-and-phase). "Exact
coordinate" below means `w_j` computed from the exact SICX data in the embedding of the
[exact-data convention](sicx.md#conventions-8-exact-data-summary-of-sicx-spec-2);
[verification](verification.md) states on what basis that vector is a SIC-POVM fiducial vector.

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
| `galois_element` | `smith` (invariant factors `n_l`) and `exponents` (`c_l`, `0 ≤ c_l < n_l`): the Galois automorphism `Π τ_l^(c_l)` applied to the entry-rule base vector to obtain this ray; the label `c0` need not have the identity word |
| `labels` | catalogue labels, plus `anchor` (the anchor index `a`, 0-based) and `sicx_ray_label` (the ray label inside the SICX file) |
| `sicx` | `name`, `sha256`, `merkle_root` of the SICX file this ray was generated from |
| `exact` | present only when `exact.json` exists: `file` (`"exact.json"`) and `ray` (the id) |

<a id="data-22-numerical-levels-and-what-each-guarantees"></a>
### 2.2 Numerical levels and what each guarantees

| level | fields | guarantee |
| --- | --- | --- |
| `fp64` | `values` (`[hex_re, hex_im]`, 16 hexadecimal digits: the IEEE 754 binary64 bit pattern), `decimal` (`%.16e`, 17 significant digits, round-trips to the same binary64 number), `error_bound`, `rounding` | each real and each imaginary part is the **correct rounding** (round to nearest, ties to even) of the exact coordinate. `error_bound` is a power of two `0x1p−N` strictly above `|value − exact|` for every real and imaginary part, or `0x0p+0` if all represented components are exact. |
| `fp32` | as `fp64`, with 8 hexadecimal digits and 9 significant digits (`%.8e`) | the correct rounding to binary32 of the exact coordinate, rounded **from the certified ball, never from `fp64`**; values are normal or zero. |
| `hp` | `accuracy_bits`, `decimal_places` `P`, `values` (`[mid_re, rad_re, mid_im, rad_im]`, decimal strings with exactly `P` decimals) | each decimal ball (midpoint ± radius) **contains** the stored certified ball, hence the exact coordinate; every radius is `< 2^−accuracy_bits`. |

Each guarantee is checked when the file is written; writing fails otherwise.

- The SICX file stores a certified ball (`BALL` chunk) for every coordinate of every fiducial
  vector, containing the exact coordinate. In this release `accuracy_bits` ranges from 131 to 189.
- `fp64` and `fp32`: both endpoints of the ball must round to the same floating-point number; that
  number is then the correct rounding of every point of the ball, in particular of the exact
  coordinate, with zero encoded as positive zero.
- `hp`: the decimal ball is rounded outward from the binary ball (midpoint to nearest, radius
  rounded up plus one unit in the last place), so it contains the binary ball. `P = ⌈accuracy ·
  log10 2⌉ + 3` for the ball accuracy.
- The anchor coordinate has imaginary part exactly 0 in `fp64` and `fp32`; in `hp` its imaginary
  midpoint is 0 and its radius is one unit in the last decimal place (outward rounding).
- The binary64 vector does not have norm exactly 1; renormalize it if you need unit norm to
  rounding accuracy.

<a id="data-23-example"></a>
### 2.3 Example

From `data/d4/m1/c0.json` (its anchor is coordinate 1):

```json
"fp64": {"values": [["bfb1a4156f0ca66d","bfdec56992bdf6e5"], ["3fe8025562d25ae8","0000000000000000"], ...],
         "decimal": [["-6.8909968959496928e-02","-4.8079909639623813e-01"], ["7.5028485585320670e-01","0.0000000000000000e+00"], ...],
         "error_bound": "0x1p-54",
         "rounding": "certified round-to-nearest-even of the exact normalized coordinate"}
```

<a id="data-3-binary-files-npy-npz-mat-h5-csv"></a>
## 3. Binary files: `.npy`, `.npz`, `.mat`, `.h5`, `.csv`

The binary files carry the binary64 (complex128) vector of `c<N>.json`. `sic-data write` writes them
from a ray JSON file of a Weyl–Heisenberg fiducial vector or of the Hoggar lines. It checks the
dimension, the finite binary64 words and the exact agreement of the hexadecimal words with their
decimal spelling, and preserves those words. It does not re-establish the high-precision enclosure
of the JSON file or any SIC-POVM property: correct rounding and containment originate in the
finalization of the SICX file and the export of the ray JSON file (section 2.2). `sic-data check`
compares written numerical files with the JSON file through separate readback codecs.

| file | layout |
| --- | --- |
| `c<N>.npy` | NumPy format 1.0, dtype `<c16` (little-endian complex128), shape `(d,)`, C order. No pickle. |
| `all.npz` | ZIP archive of `.npy` members, one per fiducial vector, named `<ray id>.npy`, sorted by id; stored without compression with fixed timestamps (byte-deterministic). The ZIP archive comment is JSON (`"schema": "sic-npz-bundle/1.0"`, dimension, dtype, member count, source digests or, for many members, their digest); NumPy ignores it. |
| `c<N>.mat` | MATLAB Level 5, uncompressed. Variables: `psi` (complex `d × 1`), `d`, `wh_convention`, `tau_phase`, `ray_id`, `rounding`, `error_bound`, `source_sha256`; `anchor` (0-based) and `anchor_index_1based` are present when the source JSON records an anchor. |
| `c<N>.h5` | HDF5 dataset `psi`, shape `(d,)`, compound type `{r: float64, i: float64}` (the h5py complex layout); attributes `d`, `ray_id`, `rounding`, `error_bound`, `source_sha256`. The anchor is not stored; read it from the JSON file. No timestamps. |
| `c<N>.csv` | header `index,real,imag`, then `d` rows; 17 significant digits. |

<a id="data-4-loading-recipes"></a>
## 4. Loading recipes

Replace `d8`, `m1` and `c0` as needed. Paths are relative to the repository root.

<a id="data-41-numpy"></a>
### 4.1 NumPy

```python
import numpy as np
bundle = np.load("data/d8/all.npz", allow_pickle=False)     # one member per fiducial vector
print(bundle.files)                                         # member names: ray ids, here also d8-hoggar-c0
psi = bundle["d8-m1-c0"]                                    # complex128, shape (d,)
psi = np.load("data/d8/m1/c0.npy", allow_pickle=False)      # the same vector from its own file
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

From the CSV file: `c = np.loadtxt("data/d8/m1/c0.csv", delimiter=",", skiprows=1)`, then
`psi = c[:, 1] + 1j * c[:, 2]`.

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
a = S.anchor_index_1based;                              % psi(a) is real and positive
```

Use the `.mat` or `.csv` files in MATLAB and GNU Octave: common MATLAB readers of `.npy` files do not
support the complex dtype `<c16`.

<a id="data-44-hdf5-h5py-julia-hdf5jl-matlab-h5read"></a>
### 4.4 HDF5 (h5py, Julia HDF5.jl, MATLAB `h5read`)

```python
import h5py
with h5py.File("data/d8/m1/c0.h5", "r") as f:
    psi = f["psi"][()]                 # h5py reads the compound {r, i} as complex128
```

```julia
using HDF5
psi = h5read("data/d8/m1/c0.h5", "psi")   # Vector{ComplexF64} with complex support enabled
```

```matlab
s = h5read('data/d8/m1/c0.h5', '/psi');  psi = s.r + 1i * s.i;
```

<a id="data-45-julia-npy-mat"></a>
### 4.5 Julia (`.npy`, `.mat`)

```julia
using NPZ, MAT
psi = npzread("data/d8/m1/c0.npy")
psi = vec(matread("data/d8/m1/c0.mat")["psi"])
```

The decimal balls of `c<N>.json` can be read with an arbitrary-precision ball arithmetic library,
for example Arblib.jl, from the midpoint and radius strings.

<a id="data-46-other-software"></a>
### 4.6 Other software

Software that reads JSON or HDF5 can use the hexadecimal binary64 words of `c<N>.json` for
bit-exact reconstruction, or import the real and imaginary components of the HDF5 compound type
`{r, i}` explicitly.

<a id="data-47-c-library-reader"></a>
### 4.7 C++ (library reader)

The `read-fiducial` example is compiled and run on a CSV fixture in CTest.
Library reader tests separately exercise NPY and text input.
See also the [compiled library example](library.md#library-compiled-core-example).

### 4.8 Qubit software

For `d = 2^n` the vector can be read as an `n`-qubit state; the
[qubit ordering convention](sicx.md#conventions-1-hilbert-space-basis-and-qubit-ordering) states the
correspondence of bits and tensor factors.

```python
from qiskit.quantum_info import Statevector
sv = Statevector(psi)                         # qubit 0 = least significant bit of the index j

import qutip
n = int(np.log2(psi.size))
q = qutip.Qobj(psi.reshape(-1, 1), dims=[[2] * n, [1] * n])   # tensor factor 0 = most significant bit
```

<a id="data-5-checking-a-fiducial-yourself"></a>
## 5. Checking a fiducial vector yourself

`sic-overlaps FILE` prints the maximum of `| |⟨ψ|D_p ψ⟩|² − 1/(d+1) |` over `p ≠ 0` in binary64
arithmetic (a diagnostic, not a certificate). The same quantity in NumPy, with one fast Fourier
transform per shift `p1` and the formula of the
[displacement-operator convention](sicx.md#conventions-3-displacement-operators):

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

The phase `τ^n` is evaluated as `exp(πi·(n(d+1) mod 2d)/d)` with the exponent reduced in exact
integers; evaluating `τ**n` in floating point loses phase accuracy at large `d`. Certified bounds
and exact checks are described in [verification](verification.md).

<a id="formats-5-reconstructing-a-fiducial-vector"></a>
### 5.1 The SIC-POVM of a fiducial vector

The SIC-POVM generated by `ψ` consists of the `d²` operators `(1/d) |D_p ψ⟩⟨D_p ψ|`,
`p ∈ {0, …, d−1}²`, with `(D_p ψ)_k = τ^(−p1·p2) ω^(p2·k) ψ_((k−p1) mod d)`,
`τ = −exp(πi/d)` and `ω = exp(2πi/d)`. Do not form the `d²` projectors at large `d` (`d⁴` entries,
1.1 TB at `d = 512`); even all `d²` vectors need `16 d³` bytes (17 GB at `d = 1024`). Compute the
required quantities without building the vectors, with one fast Fourier transform per shift:

```python
import numpy as np
def tau_pow(n, d):
    """tau^n = exp(i pi n (d+1)/d) with the exponent reduced mod 2d in exact integers."""
    return np.exp(1j * np.pi * (((np.asarray(n, dtype=np.int64) * (d + 1)) % (2 * d)) / d))
def overlaps_with_state(psi, phi):
    """<D_(a,b) psi | phi> for all a, b (array [a, b]); O(d^2 log d) operations."""
    d = psi.size; b = np.arange(d)
    out = np.empty((d, d), complex)
    for a in range(d):
        u = np.conj(np.roll(psi, a)) * phi                 # conj(psi_(k-a)) phi_k
        out[a] = tau_pow(a * b, d) * np.fft.fft(u)
    return out        # outcome probabilities of the SIC-POVM for the unit vector phi: |out|^2 / d
```

For tomography or channel simulation, stream the vectors with the
[frame API](library.md#frame-dft-and-clifford-signatures) and accumulate the required quantities.
The binary64 diagnostic of `sic-check` reads a text vector, for example from a CSV file:

<!-- shell:template -->
```sh
{ echo 8; awk -F, 'NR>1 {print $2, $3}' data/d8/m1/c0.csv; } | sic-check - 1e-10
```

<a id="clifford-2-sic-overlaps"></a>
### 5.2 Overlap-table output

```
sic-overlaps FILE [--tol T=1e-10] [--vectors] [--table OUT.bin]
```
Prints `d`, the input norm, the norm defect, `max_{p ≠ 0} | |⟨ψ|D_p ψ⟩|² − 1/(d+1) |` with the worst
label, the Parseval defect `|Σ_p |⟨ψ|D_p ψ⟩|² − d|` and timings. `--vectors` streams all frame
vectors (norm defect, checksum); `--table` writes the overlaps as raw native-endian complex128,
index `p1*d + p2`. Exit 0 within tolerance, 1 for failed checks or input or runtime errors, 2 for
usage errors.

<a id="data-6-symmetry-tables-and-the-cross-reference"></a>
<a id="stab"></a>
<a id="clifford-4-stabilizer-data-sic-stabilizer-datasymmetry"></a>
## 6. Symmetry tables

`d<d>/stabilizers.txt` lists the stabilizer of each fiducial vector of the dimension in the
projective extended Clifford group `PEC(d)` (unitary and antiunitary elements), one `elem` line per
element. The elements are defined in the
[Clifford group convention](sicx.md#conventions-5-clifford-group-extended-clifford-group-stabilizer-and-zauner-symmetry).

```
sic-stabilizer FILE [--tol T] [--entry-error E]
sic-stabilizer --dim D --catalogue FILE [--repo DIR] [--out FILE]
```
The tool computes `S(φ) = {[U] ∈ PEC(d) : |⟨φ|U φ⟩| = 1}` (Scott and Grassl 2010, equation (4.4)).
`S` embeds into `ESL(2, Z_d) × {±1}` via `(F mod d, det)`: if two elements share `(F mod d, det)`,
their quotient is unitary with `F = I mod d`, hence a displacement `D_χ` (Appleby 2005), which fixes
no fiducial vector unless `χ = 0` because `|⟨φ|D_χ φ⟩|² = 1/(d+1)`; the sign is kept explicitly
since `det = −1 = 1 mod 2` at `d = 2`. Group order, element orders and the closure check are
therefore exact integer computations on `(F mod d, det)`.

The table `d<D>/stabilizers.txt` (`sic-stabilizers/1.0`) has `#` comments, then per fiducial vector

```
ray LABEL d D order ORDER status STATUS group_order N unitary_order Nu closed yes|NO exhaustive yes|NO
    max_overlap_error X z_tol T z_error_bound B max_element_z_deviation Z max_element_residual R
    min_rejected_z_deviation Zr min_rejected_residual Rr level_set_sizes L candidate_pairs C
elem LABEL INDEX det +1|-1 F a b c e chi x y order K trace t [zauner] z_dev Z residual R
```
(the `ray` record is one line). `F = [[a,b],[c,e]]` is the canonical lift in `ESL(2, Z_d̄)`
(lexicographically smallest entries in `[0, d̄)` with the right determinant) and the element is
`U = D_χ U_F`; `order` is the order in the group; `trace` is `tr F mod d̄`; `zauner` marks unitary
elements of order 3 with `tr F ≡ −1 (mod d)` (Appleby's canonical order-3 elements, Scott and
Grassl 2010, section 4). `exhaustive yes` means `2 · z_error_bound ≤ z_tol`; `min_rejected_*` are
−1 when no candidate was rejected.

Certification with `sic-certify-stabilizer STABFILE --catalogue FILE --dim D [--d2-exact data/d2/d2.sicrad]`
(FLINT) computes, for each non-identity element, a ball enclosure (384 bits, Appleby's formula as a
direct sum) of `1 − |⟨φ|U φ⟩|²/⟨φ|φ⟩²` and passes if the bound is below `2^−100`. The table must
declare every catalogue fiducial vector of the requested dimension, with consecutive element
indices, matching total and unitary counts and a closing `end` record. A JSON catalogue lists the
generated `c<N>.json` records; the checker reads their decimal midpoints and radii as exact
rationals and encloses them outward. Explicit TSV catalogues select adjacent
`normalized-gauged.balls` inputs. With `--d2-exact`, the elements in dimension 2 are also verified
exactly with FLINT algebraic numbers (`qqbar`) from the radical input (`U ψ` proportional to `ψ`).

<a id="data-7-which-file-should-i-use"></a>
## 7. Which file should I use?

| need | file |
| --- | --- |
| all fiducial vectors of a dimension in Python | `all.npz` |
| one fiducial vector, bit-exact binary64 with metadata | `c<N>.json` (`fp64`) |
| more than 53 bits, with a rigorous enclosure | `c<N>.json` (`hp`) |
| MATLAB or GNU Octave | `c<N>.mat` |
| spreadsheets and plain-text tools | `c<N>.csv` |
| exact algebraic data | the SICX file ([exact data downloads](#transport-getting-the-exact-data)) |

<a id="data-8-the-hoggar-lines-datad8hoggar"></a>
## 8. The Hoggar lines (`data/d8/hoggar/`)

The Hoggar lines (64 lines in `C^8`, covariant under the three-qubit Pauli group) are an exceptional
entry. They are **not** one of the 174 Weyl–Heisenberg fiducial vectors and are not counted in the
catalogue totals. The files were added directly as data from the closed-form fiducial vector
`(−1+2i, 1, …, 1)/√12`; they are not generated by code in this repository and are checked by the
CTest `sic_test_data_hoggar_data` and `cpp/checkers/gp/hoggar_verify.gp`.

```
data/d8/hoggar/
  manifest.json   schema sic-hoggar-manifest/1.0: group, convention, status, provenance, id d8-hoggar-c0
  exact.json      Gaussian integers of v = sqrt(12) psi (all numbers strings), normalization 1/sqrt(12)
  c0.json         fp32, fp64, hp as in section 2 (schema sic-hoggar-fiducial-json/1.0; no multiplet, galois_element or sicx)
  c0.npy  c0.mat  c0.h5  c0.csv   the binary64 vector of c0.json (fp64), in the formats of section 3
```

- `c0.json`: `fp64` and `fp32` are the correct roundings of the exact coordinates; `hp` has 80
  decimals, radius 0 exactly where the coordinate is exactly 0 and one unit in the last place
  otherwise.
- Load it like a Weyl–Heisenberg fiducial vector (section 4), for example
  `np.load("data/d8/hoggar/c0.npy")`. The 64 lines are `P_{a,b} ψ` with
  `P_{a,b}|x⟩ = (−1)^{b·x}|x ⊕ a⟩` (`a, b` in `0..7`, bit 2 = tensor factor 1): for a 3-bit index
  `x`, `(P ψ)[x ^ a] = (−1)^{popcount(b & x)} ψ[x]`. Do **not** apply the Weyl–Heisenberg
  displacement operators `D_p`.
- Check it: `ctest --test-dir cpp/build -R hoggar` (exact norm, 63 overlaps equal to `1/9`, 2016
  line pairs, and agreement of `c0.json` (hexadecimal values and `hp`), `c0.csv`, `c0.npy` and
  `c0.mat` with `exact.json`; `c0.h5` has its HDF5 signature checked and, when HDF5 is enabled, its
  `psi` schema attribute); `gp -q -f cpp/checkers/gp/hoggar_verify.gp` from the repository root
  prints `PASS`.
- `d8/all.npz` also contains the Hoggar vector as the member `d8-hoggar-c0.npy`. Its archive comment
  marks that member as exceptional (three-qubit Pauli group, not Weyl–Heisenberg covariant), and its
  `ray_count` counts only the Weyl–Heisenberg members.

<a id="hoggar"></a>
<a id="conventions-11-hoggar-lines-exceptional-entry-at-d--8"></a>
### Hoggar covariance and exact proof

The Hoggar lines are 64 equiangular lines in `C^8`. They form a SIC-POVM (`d² = 64` lines,
`|⟨ψ|φ⟩|² = 1/(d+1) = 1/9`), but the group that generates them is the **three-qubit Pauli group**,
not the cyclic Weyl–Heisenberg group. They are listed separately as an exceptional entry.

- **Group and convention.** `C^8 = (C²)^{⊗3}`, `|x⟩ = |x_1⟩ ⊗ |x_2⟩ ⊗ |x_3⟩` with
  `x = 4 x_1 + 2 x_2 + x_3`: tensor factor 1 is the most significant bit, following the
  [basis convention](sicx.md#conventions-1-hilbert-space-basis-and-qubit-ordering). With
  `X|x⟩ = |x+1 mod 2⟩` and `Z|x⟩ = (−1)^x |x⟩` on one qubit,
  `P_{a,b} = (X^{a_1} Z^{b_1}) ⊗ (X^{a_2} Z^{b_2}) ⊗ (X^{a_3} Z^{b_3})`, `a, b ∈ F_2³`, that is
  `P_{a,b}|x⟩ = (−1)^{b·x} |x ⊕ a⟩`: real signed permutations without an extra phase. Packed labels:
  `a = 4 a_1 + 2 a_2 + a_3` (the same for `b`).
- **Fiducial vector.** `ψ_H = (−1+2i, 1, 1, 1, 1, 1, 1, 1)^T / √12`, with the global phase as written
  (no anchor rule). The 64 lines are `P_{a,b} ψ_H`.
- **What is proved.** `⟨v|v⟩ = 12` for `v = √12 ψ_H`, `|⟨ψ_H|P_{a,b} ψ_H⟩|² = 1/9` for all 63
  `(a,b) ≠ 0`, and the 64 lines are pairwise equiangular with `1/9`; checked in exact integer
  arithmetic over `Z[i]` (status **proved**) by the CTest `sic_test_data_hoggar_data` (C++) and the
  optional `cpp/checkers/gp/hoggar_verify.gp`.
- **Handling.** Weyl–Heisenberg fiducial vectors in dimension 8 use the general producer, decoder
  and SICX format. The Hoggar lines have a closed-form fiducial vector: no SICX file, producer or
  decoder is involved.
- **Relation to the Weyl–Heisenberg SIC-POVMs.** The Weyl–Heisenberg SIC-POVMs in dimension 8 are
  covariant under `Z_8 × Z_8`; the Hoggar lines are covariant under a different group, which contains
  the three-qubit Pauli group. The Weyl–Heisenberg displacement, Clifford label and anchor rules do
  not apply to them.

<a id="transport-getting-the-exact-data"></a>
## 9. Exact data downloads

Each exact object is one complete `.sicx` file. The GitHub Release assets carry contiguous
transport parts, each **at most 1,900,000,000 bytes**, cut at physical chunk boundaries. Part
offsets are explicit and part sizes vary. A small file has one part. The last part holds all
finalization data, the logical and physical directories (LDIR, PDIR) and the footer. Concatenating
the parts in index order reproduces the file. The README gives a
[download recipe without the tools](../README.md#exact-data); the tools below verify every part and
the whole file.

The normative contract is the [SICX specification](sicx.md#spec-sicx-10-final-exact-weylheisenberg-sic-fiducial-format),
especially sections 5, 6 and 10b. SICX 1.0 readers reject every undefined chunk type regardless of
flags; extensions require `required_features` bits in a later version.

<a id="transport-the-exact-data-build-target"></a>
### 9.1 The `exact-data` build target

Needs a FLINT build. Off by default; a normal build and CTest never touch the network.

    cmake -S cpp -B build -DSIC_ENABLE_FLINT=ON -DSIC_DATA_ROOT="$PWD/data" -DSIC_DATA_MULTIPLETS="d8/m1;d16/m1"
    cmake --build build --target exact-data

The target also writes the power-basis JSON file of each selected multiplet, so select small
dimensions with it; fetch the large exact files with `sic-container fetch`.

For each multiplet this fetches a verified SICX into `build/data/d<d>/m<N>/`
and generates `exact.json` on demand with `sic-inspect --exact-json` where
power-basis conversion is practical (measured through d16, not large d).
Options: `SIC_DATA_SOURCE` (base URL/local parts directory, default the release
in the manifest), `SIC_DATA_ROOT` (explicit release tree), `SIC_DATA_JOBS` (parallel
downloads, default 4), `SIC_DATA_EXACT_JSON_FACTOR` (disk forecast multiplier,
default 2). Disk admission covers the SICX, parts in flight and the JSON
forecast; that multiplier is not a proved size bound for large power bases.

<a id="transport-sic-container-fetch-repair-split"></a>
### 9.2 `sic-container fetch`, `repair`, `split`

<!-- shell:template -->
```sh
sic-container fetch d8/m1 --data-root data
sic-container repair d8/m1 --data-root data
sic-container split multiplet.sicx --out parts --asset-prefix d8-m1
```

All tools support `--help` (stdout, exit 0). See the [complete command-line
reference](library.md#command-argument-contract) for option scopes and exit codes. Fetch and repair
require `--data-root DIR` or `--parts FILE` and default to `--out build/data/d<d>/m<N>`, `--name multiplet.sicx` and `--threads
4` (range 1..64). Only fetch accepts `--force` and `--extra-bytes`. Split uses the fixed
1,900,000,000-byte ceiling; `--part-size` accepts only that value.

- **fetch** validates the trusted parts manifest, works under a private name, and reuses only
  ranges whose complete part SHA-256 matches. It downloads each missing part, checks its digest,
  writes it at the manifest's explicit offset and syncs. After the whole-file SHA-256, both roots
  and full container verification pass, it syncs and atomically renames the file to its final name.
  An interrupted fetch resumes when run again. Extra temporary disk use is at most `J` part-sized
  files, in addition to the work file.
- **repair** works under a private `.repairing` name, checks each explicit part range, fetches only
  missing or damaged parts again and repeats all final checks before renaming back. It reports the
  repaired indices. A partial or sparse work file is never exposed as a verified SICX file.
- **split** validates the complete file and deterministically packs whole chunks up to the
  ceiling, reserving the complete finalization, directory and footer suffix as its own last part
  unless the entire file fits one part ([specification section 10b.1](sicx.md#spec-10b1-parts-manifest-and-deterministic-splitting)). It writes
  `d<d>-m<N>.sicx.part-NNNN` (at least four index digits) and `multiplet.sicx.parts.json`, the
  latter last. An indivisible chunk or suffix larger than the ceiling is a split error.
  `--emit-and-upload CMD` writes, hashes, uploads, confirms the remote size and deletes one part at
  a time; `--confirm-size-cmd` receives the asset name in `SIC_PART_ASSET`. Resumption skips only
  the parts of the same snapshot that were already uploaded and confirmed.

Full fetch and repair use the `curl` executable without a shell. The asset URL is
`https://github.com/<repository>/releases/download/<tag>/<asset>`; `--source` supplies a mirror base
URL or a local directory. A full fetch and an HTTP Range read are separate strategies; a server that
ignores Range never silently switches a partial reader to a full download.

<a id="transport-parts-manifest"></a>
### 9.3 Parts manifest

`sicx-transport-parts/1.0` is canonical JSON, all numbers strings, with exactly `schema`,
`alignment: "sicx-chunks/1.0"`, `file: "multiplet.sicx"`, `size_bytes`, `sha256`, `exact_root`,
`file_root`, `part_size_bytes: "1900000000"`, `release {repository, tag}` and `parts`. Each part has
exactly `index`, `asset`, `offset`, `size_bytes` and `sha256`. [Specification section 10b.1](sicx.md#spec-10b1-parts-manifest-and-deterministic-splitting) gives the
bounds and the name grammar.

Readers check indices 0 through n−1, positive sizes no larger than the ceiling, `offset[0]=0`,
`offset[i+1]=offset[i]+size[i]`, a final end equal to the file size, correct unique asset names and
no gaps or overlaps. They check cut positions against PDIR and that the entire finalization, LDIR,
PDIR and footer suffix is in the last part. There is **no** `offset = i·part_size` invariant and no
rule that earlier parts have equal size: validate each explicit offset and chunk boundary rather
than deriving offsets from a fixed stride. Full fetch checks every part digest and the whole-file
digest; a partial read cannot claim either from a small range.

The trusted catalogue binds a manifest snapshot, exact root, file root, file size and whole-file
digest. The exact root identifies the canonical embedded exact presentation, independently of the
selection of fiducial vectors, physical grouping and transport. The file root binds PDIR, LDIR and
all physical payload digests, including finalization and provenance. A root obtained only from the
same untrusted server proves self-consistency, not authenticity. A trusted file root, after
verification of its complete LDIR binding, also makes the recomputed exact root trusted
transitively. A trusted whole-file SHA-256 makes both roots trusted only after every byte of that
same snapshot has been hashed. Trust in the exact root alone does not authenticate RAYS or BALL.

<a id="transport-remote-partial-access"></a>
### 9.4 Remote partial access

A remote reader fetches the file header, the last 112-byte footer and the complete LDIR and PDIR
chunks (including their headers), checks the geometry and both roots, establishes trust by one of
the routes above, authenticates EXMT and derives the required leaves from the requested words and
coordinates. Bootstrap does not fetch every ordinary chunk header: each is checked against PDIR when
its chunk is read. For unread finalization, the reader checks the directory's `RAYS BALL+ PREC?`
skeleton, BALL indices and coordinate spans, and defers the checks that depend on RAYS until the
required payloads are read ([specification section 6.2](sicx.md#spec-62-partial-verification)). It then fetches whole physical chunks and
checks both the encoded-payload digest in PDIR and every decoded logical leaf's domain-separated
digest in LDIR before using values. No per-request audit paths or published sidecar index are
needed. A partial result lists unread headers and payloads and deferred checks; it does not claim
full verification or a whole-file digest from a subset. Container verification alone establishes
neither BALL-to-exact containment, nor binary64 agreement, nor the field and group premises: BALL
containment is an obligation of the certified finalizer ([specification section 5.2](sicx.md#spec-52-ball-one-required-chunk-per-ray)), and algebraic
certificates are separate.

Map logical ranges with the manifest's actual part offsets. Send one part-local inclusive Range per
request with `Accept-Encoding: identity` and no transparent decompression. Require **206** and
exactly one matching `Content-Range: bytes start-end/part_size`, a known total equal to that part's
size, and an exact body length. Reject **200 at its headers**, missing, wrong or duplicate
Content-Range, multipart or encoded bodies, truncation and extra bytes. Follow bounded HTTPS
redirects with normal TLS checks and do not forward credentials across origins. Pin one immutable
source snapshot; a source change requires a fresh bootstrap or an explicit error. Specification
[section 10b.3](sicx.md#spec-10b3-authenticated-http-range-reads) gives the full HTTP contract and the bounded-buffer requirements.

Cold decoding of one **complete fiducial vector needs essentially all exact data**, because every
representative is used; selecting one fiducial vector does not divide the transfer by their number.
Coordinate subsets and ratio spot checks can save substantial transfer. Reads of BALL records alone
avoid the exact elements and require **file-level trust** (a trusted file root or a fully verified
trusted whole-file digest). The access planner can choose a full sequential fetch when the
dependency set and bandwidth favour it.

Cache canonical exact leaves by `(exact_root, LDIR index, full LDIR entry)`; reuse them across
groupings and finalizations only after digest validation. Cache encoded chunks, directory and
footer snapshots, finalization and pending ranges by file root with their PDIR entry and matching
source descriptor. Never reuse BALL solely because the exact root matches. Sparse holes are missing
bytes; incomplete fragments stay unverified, and cached bytes are hashed again on use. A disk cache
is private bookkeeping, not a partial `.sicx` publication format.

<a id="remote-decoding"></a>
<a id="transport-remote-decoding-and-random-access"></a>
### 9.5 Remote decoding and random access

The reader uses the SICX 1.0 EXMT, LDIR and PDIR layout, the 112-byte footer and the chunk-aligned
parts manifest. Opening validates both complete directories and their roots, EXMT and the derived
logical and physical coverage. A requested physical chunk passes its header, payload digest, every
logical digest and the encoding checks before any constituent leaf is consumed.

The SICX reader and decoder also accept a **local parts manifest** with remote byte access. The SICX
bytes, exact root and certificates are unchanged. Build with libcurl >= 7.81 development headers for
HTTP support (`SIC_ENABLE_REMOTE` defaults to ON when libcurl is found). The numeric core library
does not link libcurl. The command-line tools load shared libcurl when opening an HTTP source, so local decoding
does not load it. `-DSIC_ENABLE_REMOTE=OFF` still supports local files and local parts mirrors.
Tests use a local Node HTTP server and never contact GitHub.

API callers using libcurl older than 7.84 must construct HTTP sources before starting application
threads; subsequent reads are thread-safe. The command-line tools do this before creating decoder
and verification workers. The loaded HTTP runtime stays initialized for the process lifetime, which
avoids resolver-thread unload races.

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

See [command-line tools](tools.md) for the shared remote controls and the decoder selection syntax.
For example, `--coordinates 0-3,7,2` selects the sorted coordinates 0, 1, 2, 3 and 7. A noncontiguous
selection writes `c<N>.raw-selected.balls` and lists its indices in `decode.provenance.json`.
Subsets are raw enclosures; full normalization and binary64 rounding require all coordinates. To
compare scheduled and on-demand access, repeat a decode with separate empty caches and
`--prefetch-mib 8` and `--prefetch-mib 0`; local and remote numerical output bytes agree.

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
The resident set size remains subject to allocator and library overhead. `--check-budget` uses the operating
system's process high-water resident set size. For finalization it checks again after private validation
and whole-file SHA-256, before publishing, and reports the peak through publication.
This measure covers the process rather than filesystem cache or cgroup-wide memory.
Linux publication links the checked file descriptor without replacing an output.
Other platforms use the private pathname, with inode checks on either side of publication.
`--cache-max-ranges` bounds cache and read-plan metadata
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

The full fallback streams to a resumable work file, verifies the part and whole-file SHA-256 digests
and the local reader checks, then atomically publishes a complete cached `full.sicx`. Later
processes can use that verified file offline. Its roots must also agree with any identity already
opened through sparse reads. `--finalize` requires a local complete SICX file; remote access is
read-only. See [full-file transport](#transport-sic-container-fetch-repair-split) for `split`,
`fetch` and `repair`.

Remote decoding of complete fiducial vectors streams BODY chunks under the decoder budget and still
transfers essentially all exact data. When all exact elements have been consumed, the decoder also
completes the global exact canonicality checks, under a serial admitted scratch window, without
fetching unrelated BALL or PROV chunks. A subset reports those checks as deferred. The field, group,
action and SIC-POVM premises remain separate. The library uses `Reader(local_byte_source(path))` or
`Reader(remote_byte_source(manifest, options))` with the same `Decoder` API.
`Decoder(reader, options, requests)` admits the full set before loading the tower; the two-argument
constructor admits the towers first and the request dependencies when `decode` runs.
`Decoder::input_chunks` exposes the common dependency planner for additional BALL preflight.
Library callers set `RemoteOptions::memory_budget` to the decoder's memory limit before opening the
Reader, so that an early full-fetch fallback uses the same aggregate verification budget as decoding.

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

Each extent is one atomic record: magic number, logical offset, length, SHA-256 integrity digest and
bytes. The digest binds the manifest identity, the record header and the range bytes. Every reused
record is hashed again; damaged or truncated records are discarded and fetched again. Temporary
writes use unique names, sync before rename and sync the directory. File-root and canonical-manifest
namespaces prevent fragments from being spliced between physical layouts, finalizations or parts
manifests that share the same exact root. Transport fragments remain unverified until the Reader
checks their complete physical unit and every logical leaf. Cache contents can be deleted to reclaim
disk space; there is no automatic eviction.

The manifest carries both roots. It must itself come from a trusted source to authenticate them;
self-consistent server metadata alone is not a trust anchor. A sparse subset does not establish the
whole-file SHA-256 or a part digest. Supply independently trusted roots when opening an untrusted
manifest:

<!-- shell:template -->
```sh
sic-verify --remote PARTS.json --quick --expect-exact-root HEX --expect-root HEX
```

The file root also pins finalization; the exact root alone permits different finalizations of the
same exact data. For whole-file SHA-256 verification against a trusted manifest, use
`sic-container fetch` (or the explicitly enabled full fallback). A trusted exact root authenticates
logical leaves; authenticating finalization additionally requires a trusted file root or a verified
trusted whole-file SHA-256.

<a id="integrity-and-package-checks"></a>
## 10. Integrity checks

A digest identifies bytes; it proves no SIC-POVM property. Check the numerical data tree from the
`data` folder, against which the paths in `sha256sums.txt` are written:

<!-- shell:template -->
```sh
cd data
sha256sum -c sha256sums.txt
```

`catalogue.json` records the size and SHA-256 digest of every file it lists; `sha256sums.txt`
additionally covers the catalogue and excludes itself. For a downloaded archive, verify its trusted
checksum list first, extract it into a fresh folder and check every listed entry from its declared
base folder. Missing entries mean incomplete verification, and entries absent from a checksum list
are not authenticated by it.

Transport manifests authenticate explicit part offsets and lengths; concatenation is valid only
after all part digests and the whole-file SHA-256 agree. Full fetch verifies both roots and
publishes atomically ([section 9](#transport-getting-the-exact-data)).

<a id="data-tiers"></a>
## 11. Reference vectors and test fixtures

The test fixtures are inputs of the test suite, not release data: decimal reference vectors in
`cpp/tests/fixtures/core/`, exact SICX fixtures in `cpp/tests/fixtures/sicx/`, and normalized balls,
CSV files and provenance in `cpp/tests/fixtures/numeric/`. The
[fixture guide](../cpp/tests/fixtures/README.md) records their purpose and provenance. The test
fixtures under `cpp/tests/` are part of the software and use [AGPL-3.0-or-later](../LICENSE). The
datasets in `data/`, including the radical input of dimension 2, and the datasets distributed as
GitHub Release assets are made available under the [ODbL v1.0](../LICENSE-ODbL); any rights in
their individual contents are licensed under the [DbCL v1.0](../LICENSE-DbCL).

<a id="reference-vectors"></a>
### 11.1 Reference text vectors

`cpp/tests/fixtures/core/` contains test oracles rather than algorithm output (a closed form in
dimension 3; dimension 4 derived from published ghost values, cited in the file). The text format
read by `sic-check` is:

```text
d
re_0 im_0
re_1 im_1
...
re_{d-1} im_{d-1}
```

Lines starting with `#` are comments, which state the source and the Weyl–Heisenberg convention.
Coordinates use the standard basis; the vector need not be normalized.

<a id="dimension-two-radical-input"></a>
### 11.2 Dimension-two radical input

`data/d2/d2.sicrad` begins with `sic-exact-d2-radicals/1.0` and defines

\[
\psi=(\alpha,(1+i)\beta),\qquad
\alpha=\sqrt A>0,\quad \beta=\sqrt B>0,\quad \sqrt3>0.
\]

The records `alpha_squared a b` and `beta_squared a b` mean `A=a+b*sqrt(3)` and `B=a+b*sqrt(3)`,
respectively. Coefficients are exact rationals. The pinned input uses exactly six lines in the order
shown by the record. The independent stabilizer radical reader enforces that layout, including the
dimension and radicand and the absence of extra records. The producer's input parser accepts a more
permissive record order and stops at `end`; use the pinned record for reproducibility. Equivalent
rational spellings denote the same values. No decimal approximation defines the input.

For the released data,

\[
A=(3+\sqrt3)/6,\qquad B=(3-\sqrt3)/12.
\]

The radical proof uses positivity and these identities on the **parsed** coefficients in
`Q(sqrt(3))`:

\[
A+2B=1,\qquad4AB=1/3,\qquad(A-2B)^2=1/3.
\]

Under `D_p=tau^(p1*p2) X^p1 Z^p2` with `tau=-i`, the non-identity displacement operators are
`X`, `Z` and `-Y`. Their expectation values are `2*alpha*beta`, `A-2B` and `-2*alpha*beta`. The
identities therefore prove the normalization and all three exact SIC-POVM overlap equations. The
four orbit projectors divided by two form the SIC-POVM. Arb independently checks consistency with
the same positive branches; its small residual is supplementary to the exact proof.

`sic-construct 1 --out d2.sicx --radicals data/d2/d2.sicrad` reads this record (the default uses the
embedded pinned record). `sic-certify-stabilizer STABFILE --catalogue FILE --d2-exact data/d2/d2.sicrad` checks the
symmetries in dimension 2 exactly. The optional exact-JSON check with PARI/GP also exercises the
SICX file of dimension 2; its embedding limitations are stated in verification. The parsed
polynomial identities above are the mathematical basis of the radical proof, distinct from the
supplementary Arb residual.

Writing refuses an existing destination. This record is the explicit base case of dimension 2.

<a id="formats-2-enclosing-vectors-balls-and-collections"></a>
### 11.3 Decoded ball-vector format

Decoded coordinates are stored as rigorous Arb balls in the text format `sic-vector-balls/1.0`:

```
sic-vector-balls/1.0
d 1024
RE_DUMP | IM_DUMP          one row per coordinate j = 0 .. d-1 (d rows)
...
end
```
Each `*_DUMP` is Arb's `arb_dump_str`: midpoint mantissa and exponent, then radius mantissa and
exponent, four whitespace-separated hexadecimal tokens (value = `mantissa * 2^exponent`; a zero
midpoint prints as `0`). Load it with `arb_load_str` or the public data reader. A file of `d`
coordinates has `d + 3` lines.

<a id="formats-3-fp64-data-the-files-most-users-want"></a>
### 11.4 Numerical reference fixtures

The numerical test fixtures use the following schema and error contract, which is weaker than the
correct rounding of the released ray JSON files. Each enrolled fixture folder contains
`fiducial-fp64.csv`; selected folders also contain the other files below (the
[fixture guide](../cpp/tests/fixtures/README.md) lists them):

| file | content |
| --- | --- |
| `fiducial-fp64.csv` | header `index,real,imag`, then `d` rows; 17-significant-digit decimal; coordinate `j` of the normalized, phase-fixed fiducial vector |
| `normalized-gauged.balls` | the `sic-vector-balls/1.0` enclosure of the exact normalized vector with its phase fixed |
| `metadata.json` | schema `sic-fiducial-fp64/1.0`; see below |
| `SHA256SUMS` | `sha256sum` list of the three files above (relative names) |
| `COMPLETED` | `sic-fiducial-data-complete/1.0` plus the SHA-256 of `SHA256SUMS` |

**Normalization.** For the exact fiducial vector `v`, with anchor index `k` (a nonzero coordinate,
recorded in the metadata as `anchor`): `w_j = v_j / ||v|| * conj(v_k)/|v_k|`. Thus `||w|| = 1` and
`w_k = |v_k|/||v|| > 0` (the anchor's imaginary part is exactly 0).

**Error contract.** Each `(real_j, imag_j)` is the nearest binary64 midpoint of an enclosure of
`w_j`, and Arb certifies `|w_j - q_j| <= 2^-50` for every coordinate (complex distance; the ceiling
is recorded as `coordinate_complex_error_ceiling "2^-50"`). It is not a claim of correct rounding,
and the binary64 vector does not have norm exactly 1.

**`metadata.json` keys:** `schema`, `dimension`, `basis` (the status and scope sentence),
`selector` (order, class label, Smith word, coordinate order), `association_contract`,
`wh_convention`, `gauge`, `anchor`, `working_bits` (256), `native_workers`,
`coordinate_complex_error_ceiling`, `rounding`, `zero_policy`, `data_files`,
`converter_binary_object_id`, `native_flint_version`, `reproduction_command_template`,
`portable_provenance`, `fp64_point_squared_norm_arb_dump`,
`fp64_point_maximum_squared_overlap_deviation_arb_dump`, `point_verification_scope`,
`descriptor_sha256`, `pins` (role, SHA-256 artifact identifier, `sha256`, `object_id` for the
vector, export, producer binary, producer source, source mapping and premises file) and
`components`.

**Audit columns.** `components[j]` carries `index`, `real_binary64_hex`, `imag_binary64_hex`
(IEEE 754 hexadecimal encoding of the CSV values, exact) and `complex_error_arb_dump` (the Arb upper
bound on `|w_j - q_j|`). Independent readback parses the CSV file again and checks the error and the
phase. Its log has the form

```
PASS CSV independent reparse/error/gauge; d=1024 anchor=569 complex error<=2^-50
maximum_complex_error=<arb dump>
binary64_point_squared_norm=<arb dump>
binary64_point_deviation=<arb dump>
Diagnostic of approximation only; no exact SIC equality inferred.
```
`binary64_point_deviation` encloses the overlap deviation of the stored binary64 point; it is a
diagnostic of the approximation, not an exact identity.

<a id="formats-8-using-the-data-in-other-software"></a>
## 12. Numerical interchange operations

`sic-data write RAY.json OUTDIR --formats npy,mat,csv` writes the binary64 values of a ray JSON file.
Optional HDF5 output is selected at configure time. `sic-data check RAY.json FILE...` compares each
numerical file bit for bit, using separate readback readers. Inputs must have consistent
hexadecimal and decimal binary64 values. These operations do not establish an exact SIC-POVM proof.

`sic-data bundle OUT.npz D RAY.json...` writes a deterministic, sorted, store-only ZIP archive of
complex128 NPY members with a JSON archive comment. Use `--exceptional RAY.json GROUP` for
exceptional covariance groups and `sic-data check-bundle FILE.npz RAY.json... --exceptional RAY.json`
to read back all members. `sic-check-balls VECTOR.normalized.balls --bits 100` checks a rigorous
numerical overlap bound and requires complete ball data.

The production tools follow the
[shared command-line conventions and full option reference](library.md#command-argument-contract).

## 13. Schemas

The schemas of the emitted metadata are in [schemas/](schemas/):

- [ray JSON](schemas/sic-fiducial-json-1.schema.json) and [all-ray
  JSON](schemas/sic-fiducial-json-all-1.schema.json);
- [multiplet manifests](schemas/sic-multiplet-manifest-1.schema.json), [release
  catalogue](schemas/sic-release-catalogue-1.schema.json), [transport
  parts](schemas/sicx-transport-parts-1.schema.json) and [text-parts
  indexes](schemas/sicx-text-json-parts-1.schema.json);
- [NPZ archive comments](schemas/sic-npz-bundle-1.schema.json), the [Hoggar
  manifest](schemas/sic-hoggar-manifest-1.schema.json), [Hoggar exact
  data](schemas/sic-hoggar-exact-json-1.schema.json) and the [Hoggar ray
  JSON](schemas/sic-hoggar-fiducial-json-1.schema.json);
- [tool version JSON](schemas/sic-tool-version-1.schema.json), [decoder
  provenance](schemas/sic-decode-provenance-1.schema.json), [ray lists](schemas/sic-ray-list-1.schema.json),
  [ray-selection receipts](schemas/sic-ray-selection-receipt-1.schema.json), [exact
  JSON](schemas/sic-exact-json-1.schema.json), [SICX text JSON](schemas/sicx-text-json-1.schema.json)
  and [numerical fixture metadata](schemas/sic-fiducial-fp64-1.schema.json).

Each family has one major-1 schema file; use the exact identifier accepted by its reader. Schema
validity is separate from numerical containment and from any exact SIC-POVM proof.
