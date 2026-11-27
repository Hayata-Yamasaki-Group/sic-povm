# Numeric fixtures

Pinned normalized balls and binary64 coordinates of fiducial vectors in dimensions 2 to 64, the 12
fiducial vectors in dimension 128, the 12 in dimension 256 and `c0` of the maximal order in
dimension 512, from an earlier construction of these multiplets. They are test inputs, not the
released data in `data/`, and they are not regenerated. The metadata of the small dimensions and of
dimension 512 keeps the converter coverage. SHA256SUMS pins every copied file.

The labels, orders and certification status of these inputs are listed in
`cpp/tests/data/catalogue.tsv`. No exact SIC-POVM claim is made for these test inputs here
([verification](../../../../docs/verification.md) states the claims about the released data). The numerical files were checked
byte for byte against their source when they were copied. The public metadata is a sanitized
provenance view: SHA-256 artifact identifiers replace local paths. The metadata sidecar checksums
are updated explicitly; the source descriptors remain identified by their unchanged digests.
