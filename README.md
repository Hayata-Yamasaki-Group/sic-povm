# A scalable algorithm for SIC-POVMs

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.24599009.svg)](https://doi.org/10.5281/zenodo.24599009)

This repository provides software implementing a scalable algorithm for symmetric informationally
complete positive operator-valued measures (SIC-POVMs) in power-of-two dimensions `d = 2^s`, and
data in these dimensions.

- **Floating-point data.** The 174 fiducial vectors of Weyl–Heisenberg SIC-POVMs in the dimensions
  `d = 2, 4, 8, …, 2048`, and the Hoggar lines in dimension 8, are in the folder [`data/`](data/)
  of this repository: correctly rounded binary64 and binary32 coordinates with certified decimal
  enclosures as JSON, and the binary64 coordinates as CSV, NumPy (`.npy`, and `.npz` with all
  fiducial vectors of a dimension), MATLAB (`.mat`) and HDF5 (`.h5`) files. They can be used
  directly after `git clone https://github.com/Hayata-Yamasaki-Group/sic-povm` by reading these
  files; the [data guide](docs/data.md#1-the-data-tree-and-cataloguejson) describes the layout.
- **Exact construction.** The producer computes an exact algebraic presentation of a Galois
  multiplet of Weyl–Heisenberg fiducial vectors for each dimension `d = 2^s ≤ 2048` and each real
  quadratic order that it supports; [below](#contents) states the status of the SIC-POVM property in
  each dimension.
- **Certified evaluation.** The decoder evaluates the exact data to certified numerical data at a
  requested accuracy, within its precision and resource limits.
- **Verification.** Checkers verify the integrity of the exact data, certified bounds on the
  SIC-POVM overlap conditions, the exact SIC-POVM property and the symmetry groups of the
  fiducial vectors.
- **Exact data in the SICX format.** The exact data of each multiplet are one file in the SICX 1.0
  format ([specification](docs/sicx.md)), attached as assets to the GitHub Release `v1.0.0`; the
  [data guide](docs/data.md#transport-getting-the-exact-data) describes how to obtain and check
  them.

## Data

The data of version 1.0.0 are the numerical files in the folder [`data/`](data/) of the tag `v1.0.0`
and the exact files attached to the
[GitHub Release `v1.0.0`](https://github.com/Hayata-Yamasaki-Group/sic-povm/releases/tag/v1.0.0).
Obtain the numerical files with
`git clone --depth 1 --branch v1.0.0 https://github.com/Hayata-Yamasaki-Group/sic-povm.git`, from
the source archive of the release or from its
[Zenodo archive](https://doi.org/10.5281/zenodo.24599010); the commands below run in the top folder
of that copy.

### Contents

The release contains 174 Weyl–Heisenberg fiducial vectors in the dimensions `d = 2, 4, 8, …, 2048`,
grouped into 15 Galois multiplets, and the Hoggar lines in dimension 8. The fiducial vectors of each
dimension are pairwise inequivalent under the extended Clifford group, so each represents a
different extended Clifford orbit; the conjectural classification of Kopp and Lagarias (2024)
predicts exactly 174 orbits in these dimensions. The exact SIC-POVM property is proved by exact
certificates for `4 ≤ d ≤ 256` and by a closed form for `d = 2`. For `d ≥ 512` it follows from a
construction theorem, which [Verification](docs/verification.md#verification-construction-theorem)
derives from results of the cited preprints for both quadratic orders:

| Dimension `d` | Multiplets (quadratic order) | Fiducial vectors | Exact SICX file size | Exact SIC-POVM property |
| ---: | --- | ---: | --- | --- |
| 2 | `m1` (standard) | 1 | 3.2 kB | proved (closed form) |
| 4 | `m1` (standard) | 1 | 6.4 kB | proved (exact certificate) |
| 8 | `m1` (maximal), `m2` (standard) | 1 + 1 | 7.8 kB, 12.0 kB | proved (exact certificate) |
| 16 | `m1` (standard) | 2 | 23.4 kB | proved (exact certificate) |
| 32 | `m1` (standard) | 2 | 55.1 kB | proved (exact certificate) |
| 64 | `m1` (standard) | 4 | 438 kB | proved (exact certificate) |
| 128 | `m1` (maximal), `m2` (standard) | 2 + 10 | 677 kB, 7.2 MB | proved (exact certificate) |
| 256 | `m1` (standard) | 12 | 47.7 MB | proved (exact certificate) |
| 512 | `m1` (maximal), `m2` (standard) | 6 + 18 | 77.8 MB, 666 MB | proved (construction theorem) |
| 1024 | `m1` (maximal), `m2` (standard) | 10 + 40 | 748 MB, 11.7 GB | proved (construction theorem) |
| 2048 | `m1` (standard) | 64 | 128.5 GB | proved (construction theorem) |

Sizes use decimal units (1 GB = 10^9 bytes). For every Weyl–Heisenberg fiducial vector `ψ` in the
release, ball arithmetic certifies `| |⟨ψ|D_p ψ⟩|² − 1/(d+1) | < 2^−129` for all `d² − 1`
displacements `p ≠ 0`. In dimensions 4 to 256 an exact certificate proves that every Galois
conjugate in each multiplet is an exact SIC-POVM fiducial vector; in dimension 2 the closed form
proves it. In dimensions 512 to 2048 no exact certificate of the stored data was computed: the exact
property follows from the construction theorem and from the
certified computations of each construction run.
[Verification](docs/verification.md) defines these status words and states the scope of every
claim.

### Numerical files

Each multiplet folder `data/d<d>/m<N>/` holds one set of files per fiducial vector `c<N>`:

| File | Content |
| --- | --- |
| `c<N>.json` | correctly rounded binary64 and binary32 coordinates, decimal ball enclosures with 131 to 189 bits of accuracy, and metadata |
| `c<N>.csv` | `index,real,imag`, binary64 values with 17 significant digits |
| `c<N>.npy`, `c<N>.mat`, `c<N>.h5` | the binary64 vector for NumPy, MATLAB and GNU Octave, and HDF5 |
| `manifest.json`, `multiplet.sicx.parts.json` | multiplet record; parts manifest of the exact SICX file |

Each dimension folder also holds `all.npz` (all fiducial vectors of that dimension) and
`stabilizers.txt` (the symmetry group of each fiducial vector). [`data/catalogue.json`](data/catalogue.json)
lists every file with its size and SHA-256 digest, and `data/sha256sums.txt` checks the whole tree
(on macOS, use `shasum -a 256 -c sha256sums.txt`):

<!-- shell:template -->
```sh
(cd data && sha256sum -c sha256sums.txt)
```

The vectors are unit vectors in the standard basis `|0⟩, …, |d−1⟩`, with the phase fixed so that
the anchor coordinate `labels.anchor` in `c<N>.json` is real and positive. The SIC-POVM consists of
the `d²` operators `(1/d) |D_p ψ⟩⟨D_p ψ|` with the displacement operators

```text
D_p = τ^(p1·p2) X^p1 Z^p2,   τ = −exp(πi/d),   X|j⟩ = |j+1 mod d⟩,   Z|j⟩ = exp(2πij/d)|j⟩,
```

for `p = (p1, p2)` with `p1, p2 ∈ {0, …, d−1}`. For example, in Python with NumPy:

```python
import numpy as np

psi = np.load("data/d8/m1/c0.npy")      # fiducial vector d8-m1-c0: complex128, shape (8,)
d = psi.size

def overlaps(psi):
    """Return a[p1, p2] = <psi|D_(p1,p2) psi> for all displacements."""
    j = np.arange(d)
    a = np.empty((d, d), complex)
    for p1 in range(d):
        row = np.fft.ifft(np.conj(np.roll(psi, -p1)) * psi) * d     # sum_j conj(psi_(j+p1)) psi_j omega^(p2 j)
        a[p1] = np.exp(1j * np.pi * ((p1 * j * (d + 1)) % (2 * d)) / d) * row   # times tau^(p1 p2)
    return a

deviation = np.abs(np.abs(overlaps(psi).ravel()[1:]) ** 2 - 1 / (d + 1)).max()
print(deviation)                        # of the order of 1e-16 for binary64 data
```

The [data guide](docs/data.md) describes every file and gives loading recipes for NumPy, SciPy,
MATLAB, GNU Octave, HDF5 and Julia. The [mathematical conventions](docs/sicx.md#mathematical-conventions)
are normative.

### Exact data

Each multiplet's exact SICX file is attached to the GitHub Release `v1.0.0` as one or more parts of
at most 1,900,000,000 bytes; `multiplet.sicx.parts.json` lists them with their offsets and SHA-256
digests. Without installing the tools, download the parts into a new folder, check every part,
concatenate them in index order and check the digest of the whole file before using it (here for
`d = 64`; the subshell stops at the first failed command, and each run uses a new working folder):

<!-- shell:template -->
```sh
(
  set -eu
  top=$PWD
  m="$top/data/d64/m1/multiplet.sicx.parts.json"
  base=https://github.com/Hayata-Yamasaki-Group/sic-povm/releases/download/v1.0.0
  work=$(mktemp -d "$top/d64-m1-download.XXXXXX")
  cd "$work"
  jq -r '.parts[] | "\(.sha256)  \(.asset)"' "$m" > parts.sha256
  for a in $(jq -r '.parts[].asset' "$m"); do curl -fL -o "$a" "$base/$a"; done
  sha256sum -c parts.sha256
  cat $(jq -r '.parts[].asset' "$m") > multiplet.sicx.partial
  echo "$(jq -r .sha256 "$m")  multiplet.sicx.partial" | sha256sum -c -
  mv multiplet.sicx.partial "$top/d64-m1.sicx"
)
```

The commands need `curl`, `jq` and `sha256sum` (on macOS, replace `sha256sum` by `shasum -a 256`).

With the tools installed, `sic-container fetch d64/m1 --data-root data` performs the same transfer
with per-part verification, resumption and a full container check, and `sic-decode --remote` reads
selected coordinates without downloading the whole file. The [SICX specification](docs/sicx.md)
defines the format.

## Full reproduction

The tools build with CMake and a C++20 compiler on Linux. The exact components require FLINT 3
(with Arb), GMP, MPFR and OpenSSL. The release was built with GCC 13.3, CMake 3.28 and FLINT 3.6.0
on Ubuntu 24.04 (x86-64). [Installation](docs/install.md) gives the dependencies and build
commands. With the exact tools installed, the following constructs the standard-order multiplet in
dimension 8, decodes it to certified values and checks the file:

<!-- shell:template -->
```sh
sic-construct 3 --out d8.sicx --rays-out d8.rays.json --order standard --threads 2
sic-decode d8.sicx d8-finalize --finalize --ray-list d8.rays.json --threads 2
sic-verify d8.sicx --rescaling
sic-certify-crt d8.sicx --threads 2
```

[Reproduction](docs/reproduce.md) gives the command for every multiplet of the release, the checks
applied to each file, the commands that rebuild `data/`, and the measured time and memory of the
construction and finalization of every multiplet. The release was computed on Google Cloud H4D
machines with 192 AMD EPYC 9B45 cores and about 1.4 TiB of memory each:
the largest multiplet, `d = 2048`, took 12.8 hours to construct with a peak of 373 GiB of memory,
and 2.9 hours to finalize.

## Repository layout

```text
cpp/
  include/sic/     installed C++ headers, one folder per component
  src/             library implementations
  apps/            command-line tools (sic-construct, sic-decode, sic-verify, ...)
  checkers/        independent checkers with their own SICX readers (Chinese remainder theorem,
                   norm, second reader, stabilizer) and PARI/GP reference scripts
  tests/           tests and test fixtures
  examples/        compiled example programs
  bench/           optional benchmarks
  cmake/           build components, checks and package configuration
data/              released numerical data, catalogue and parts manifests of the exact files
docs/              documentation, JSON schemas and machine-checked documentation contracts
scripts/           documentation and repository policy checks; exact-data download helper
.github/           continuous integration workflow and issue templates
```

## Documentation

| Document | Content |
| --- | --- |
| [Installation](docs/install.md) | dependencies, build, test and installation; use as a CMake package |
| [Command-line tools](docs/tools.md) | usage, options and exit codes of every tool, generated from `--help` |
| [Data guide](docs/data.md) | release data layout, file formats, loading recipes, downloads and integrity checks |
| [SICX specification](docs/sicx.md) | the normative exact-data format and the mathematical conventions |
| [Verification](docs/verification.md) | status of every claim about the data and how to check it |
| [Reproduction](docs/reproduce.md) | per-multiplet construction and check commands, measured resources |
| [C++ library](docs/library.md) | library components, ownership rules, failure categories and resource policy |
| [Configuration](docs/configuration.md) | CMake cache inputs and environment variables |
| [JSON schemas](docs/schemas/) | schemas of every JSON file written by the tools |
| [Command-line contract](docs/cli_contract.json), [defaults](docs/default_contract.json), [installation inventory](docs/install_contract.json) | machine-checked inventories of options, defaults and installed files |
| [Test fixtures](cpp/tests/fixtures/README.md) | purpose and provenance of the test inputs |
| [Contributing](CONTRIBUTING.md) | development rules, test selection and documentation checks |
| [Changelog](CHANGELOG.md) | release notes |

## Citation

If you use this software or its data, please cite it as

> Hayata Yamasaki, *A scalable algorithm for SIC-POVMs*, version 1.0.0 (2026),
> Zenodo, https://doi.org/10.5281/zenodo.24599010.

The Zenodo record holds the source and the numerical files of version 1.0.0, without the exact
files; the DOI [10.5281/zenodo.24599009](https://doi.org/10.5281/zenodo.24599009) refers to all
versions and resolves to the latest one. GitHub's "Cite this repository" gives the same reference
in other formats.

## Acknowledgements

This work was supported by Faculty Research Funding from Google Quantum AI, JST PRESTO Grant No. JPMJPR23FC, JST CREST Grant No. JPMJCR25I5, and JST Moonshot R&D Grant No. JPMJMS256J.

## License

Copyright (C) 2026 Hayata Yamasaki and contributors.

The software (including scripts and the test fixtures under `cpp/tests/`), documentation, formats
and schemas are licensed under the GNU Affero General Public License, version 3 or any later
version ([LICENSE](LICENSE)), except where otherwise noted. The datasets in `data/` and the datasets
distributed as GitHub Release assets are made available under the Open Database License v1.0
([LICENSE-ODbL](LICENSE-ODbL)); any rights in their individual contents are licensed under the
Database Contents License v1.0 ([LICENSE-DbCL](LICENSE-DbCL)).
