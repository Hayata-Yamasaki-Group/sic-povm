# Small numerical verifier oracles

These reference vectors are general verifier inputs, not release data.
`d3_simple.txt` is the direction `(0, 1, -1)` in dimension three;
`d4_sic_from_ghost.txt` is a numerical SIC-POVM fiducial vector in dimension four, derived from
published ghost values cited in the file. Their `sic-check` CTests check the Weyl–Heisenberg
overlap equations. The input in dimension three also checks the handling of a catalogue without a
match outside the power-of-two dimensions. These floating-point checks do not provide an exact
SIC-POVM proof.

SHA256SUMS authenticates both text inputs; the fixture policy checks it.
