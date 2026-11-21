# Small numerical verifier oracles

These unchanged reference vectors are general verifier inputs, not release data.
`d3_simple.txt` is the direction `(0, 1, -1)` in dimension three;
`d4_sic_from_ghost.txt` is a dimension-four numerical SIC oracle from the original
construction. Their `sic-check` CTests check the WH overlap equations. The d3
input also checks catalogue no-match handling outside the power-of-two release
scope. These floating-point checks do not provide an exact SIC proof.

SHA256SUMS authenticates both text inputs; the fixture policy checks it.
The d4 coordinate comment retains its original construction attribution.
