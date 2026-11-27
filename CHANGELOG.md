# Changelog

## 1.0.0

- Exact construction of Weyl–Heisenberg fiducial vectors in dimensions `d = 2^s` up to
  `d = 2048`, one Galois multiplet per real quadratic order, with certified decoding and
  finalization, and the SICX 1.0 binary, text and JSON interchange formats.
- Released data: 174 pairwise inequivalent Weyl–Heisenberg fiducial vectors in 15 multiplets for
  `d = 2, 4, …, 2048` (exact SIC-POVM property proved by exact certificates for `4 ≤ d ≤ 256`, by
  a closed form for `d = 2`, and for `d ≥ 512` by the
  [construction theorem](docs/verification.md#verification-construction-theorem))
  and the Hoggar lines, as correctly rounded binary64 and binary32 values with certified decimal
  enclosures (JSON, CSV, NumPy, MATLAB, HDF5), symmetry tables and a catalogue in `data/`, and the
  exact SICX files as GitHub Release assets.
- Independent checkers: exact SIC-POVM certificates by the Chinese remainder theorem method and by
  the norm method, a second SICX reader, and stabilizer certificates.
- Verification of the released data: certified overlap bounds for every fiducial vector, exact
  certificates for `4 ≤ d ≤ 256`, certified pairwise inequivalence under the extended Clifford
  group, and certified stabilizer elements.
- Numerical overlap, Clifford identification and stabilizer tools.
- Authenticated chunk-aligned transport, range access, download and repair, resumable construction
  and bounded resource policies.
- Relocatable C++20 CMake components, separation of production code and test hooks, and fixture,
  independence, help and documentation checks.
