# Fixture purposes

This table lists every file in this tree, including this README. The consumer column names a registered test or states that none was identified; citations use repository-relative paths. The docs fixture inventory check (cpp/cmake/checks/Docs.cmake:12) enumerates every file; it is not repeated in each row. Conditional tests are included. A manifest only pins bytes when its listed entry matches; where no registered test checks it, that limitation is stated. Composed SICX names are resolved through cpp/tests/sicx/tests.cmake:32–41 and the copy step. More converter and provenance detail: [core](core/README.md), [numeric](numeric/README.md).

The generated SICX fixtures are the inputs to public construction, decoding,
transport, protocol and independent certificate tests. The d16, d32 and d64
provenance records supply exact-part SHA-256 oracles for those fixtures.
The standard-order d8 native text oracle uses the retained native construction
with exact-file SHA-256 `97c022fde78a19520af3a770139dec2cf7b8312a11cbf9759198437bb23f6a06`.
The separate byte-oracle checks compare the copied corpus and reference decode
hashes. The anchor test reads the archived RAYS records as advisory oracles.
The d64 verifier-budget fixture uses fixed words and anchors for the mixed
91/92-step boundary. FLINT-free CLI controls use explicit closed-form vectors.

| File | Consumer | Purpose | Pin |
|---|---|---|---|
| `README.md` | `sic_test_support_docs_links, sic_test_support_docs_status, sic_test_support_docs_private (cpp/cmake/checks/Docs.cmake:7; scripts/docs/check.mjs:11)` | `purpose documentation` | `nothing` |
| `core/README.md` | `sic_test_support_docs_links, sic_test_support_docs_status, sic_test_support_docs_private (cpp/cmake/checks/Docs.cmake:7; scripts/docs/check.mjs:11)` | `provenance documentation` | `nothing` |
| `core/SHA256SUMS` | `sic_test_support_docs_fixture_inventory` | `hash pin` | `nothing` |
| `core/d3_simple.txt` | `sic_test_support_cli_verify_d3 (cpp/cmake/checks/UnitTests.cmake:307); sic_test_support_docs_private (cpp/cmake/checks/Docs.cmake:7; scripts/docs/check.mjs:39)` | `input` | `core/SHA256SUMS` |
| `core/d4_sic_from_ghost.txt` | `sic_test_support_cli_verify_d4_oracle (cpp/cmake/checks/UnitTests.cmake:356); sic_test_support_docs_private (cpp/cmake/checks/Docs.cmake:7; scripts/docs/check.mjs:39)` | `input` | `core/SHA256SUMS` |
| `numeric/README.md` | `sic_test_support_docs_links, sic_test_support_docs_status, sic_test_support_docs_private (cpp/cmake/checks/Docs.cmake:7; scripts/docs/check.mjs:11)` | `provenance documentation` | `nothing` |
| `numeric/SHA256SUMS` | `sic_test_data_publication_fixture_hashes (cpp/cmake/checks/UnitTests.cmake:47; cpp/tests/check_fixture_hashes.cmake:3)` | `hash pin` | `nothing` |
| `numeric/d128/m1/c0/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d128 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d128/m1/c1/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d128 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d128/m2/c0/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d128 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d128/m2/c1/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d128 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d128/m2/c2/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d128 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d128/m2/c3/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d128 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d128/m2/c4/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d128 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d128/m2/c5/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d128 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d128/m2/c6/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d128 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d128/m2/c7/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d128 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d128/m2/c8/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d128 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d128/m2/c9/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d128 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d16/m1/c0/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d2_d64 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d16/m1/c0/metadata.json` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `provenance` | `numeric/SHA256SUMS` |
| `numeric/d16/m1/c0/normalized-gauged.balls` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d16/m1/c1/COMPLETED` | `none identified` | `completion marker` | `nothing` |
| `numeric/d16/m1/c1/SHA256SUMS` | `sic_test_support_docs_fixture_inventory` | `hash pin` | `nothing` |
| `numeric/d16/m1/c1/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d2_d64 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d16/m1/c1/metadata.json` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `provenance` | `numeric/SHA256SUMS` |
| `numeric/d16/m1/c1/normalized-gauged.balls` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d2/m1/c0/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d2_d64 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d2/m1/c0/metadata.json` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `provenance` | `numeric/SHA256SUMS` |
| `numeric/d2/m1/c0/normalized-gauged.balls` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d256/m1/c0/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); d256 release-gate Clifford case (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d256/m1/c1/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); d256 release-gate Clifford case (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d256/m1/c10/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); d256 release-gate Clifford case (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d256/m1/c11/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); d256 release-gate Clifford case (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d256/m1/c2/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); d256 release-gate Clifford case (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d256/m1/c3/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); d256 release-gate Clifford case (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d256/m1/c4/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); d256 release-gate Clifford case (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d256/m1/c5/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); d256 release-gate Clifford case (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d256/m1/c6/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); d256 release-gate Clifford case (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d256/m1/c7/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); d256 release-gate Clifford case (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d256/m1/c8/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); d256 release-gate Clifford case (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d256/m1/c9/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); d256 release-gate Clifford case (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d32/m1/c0/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d2_d64 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d32/m1/c0/metadata.json` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `provenance` | `numeric/SHA256SUMS` |
| `numeric/d32/m1/c0/normalized-gauged.balls` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d32/m1/c1/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d2_d64 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d32/m1/c1/metadata.json` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `provenance` | `numeric/SHA256SUMS` |
| `numeric/d32/m1/c1/normalized-gauged.balls` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d4/m1/c0/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d2_d64 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d4/m1/c0/metadata.json` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `provenance` | `numeric/SHA256SUMS` |
| `numeric/d4/m1/c0/normalized-gauged.balls` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d512/m1/c0/COMPLETED` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `completion marker` | `numeric/SHA256SUMS` |
| `numeric/d512/m1/c0/SHA256SUMS` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `hash pin` | `numeric/SHA256SUMS` |
| `numeric/d512/m1/c0/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `input` | `numeric/SHA256SUMS` |
| `numeric/d512/m1/c0/metadata.json` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `provenance` | `numeric/SHA256SUMS` |
| `numeric/d512/m1/c0/normalized-gauged.balls` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d64/m1/c0/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d2_d64 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d64/m1/c0/metadata.json` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `provenance` | `numeric/SHA256SUMS` |
| `numeric/d64/m1/c0/normalized-gauged.balls` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d64/m1/c1/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d2_d64 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d64/m1/c1/metadata.json` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `provenance` | `numeric/SHA256SUMS` |
| `numeric/d64/m1/c1/normalized-gauged.balls` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d64/m1/c2/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d2_d64 (cpp/tests/data/search_fiducials.cmake; numerical oracle); sic_test_search_overlaps_d64 (cpp/cmake/checks/CliffordTools.cmake:34)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d64/m1/c2/metadata.json` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `provenance` | `numeric/SHA256SUMS` |
| `numeric/d64/m1/c2/normalized-gauged.balls` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d64/m1/c3/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d2_d64 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d64/m1/c3/metadata.json` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `provenance` | `numeric/SHA256SUMS` |
| `numeric/d64/m1/c3/normalized-gauged.balls` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d8/m1/c0/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d2_d64 (cpp/tests/data/search_fiducials.cmake; numerical oracle)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d8/m1/c0/metadata.json` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `provenance` | `numeric/SHA256SUMS` |
| `numeric/d8/m1/c0/normalized-gauged.balls` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d8/m2/c0/COMPLETED` | `none identified` | `completion marker` | `nothing` |
| `numeric/d8/m2/c0/SHA256SUMS` | `sic_test_support_docs_fixture_inventory` | `hash pin` | `nothing` |
| `numeric/d8/m2/c0/fiducial-fp64.csv` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3); sic_test_search_clifford_fiducials_d2_d64 (cpp/tests/data/search_fiducials.cmake; numerical oracle); sic_test_data_numeric_formats (cpp/cmake/checks/DataTests.cmake:4; cpp/tests/data/numeric.mjs:51)` | `golden` | `numeric/SHA256SUMS` |
| `numeric/d8/m2/c0/metadata.json` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `provenance` | `numeric/SHA256SUMS` |
| `numeric/d8/m2/c0/normalized-gauged.balls` | `sic_test_data_publication_fixture_hashes (cpp/tests/check_fixture_hashes.cmake:3)` | `golden` | `numeric/SHA256SUMS` |
| `sicx/SHA256SUMS` | `sic_test_sicx_fixture_hashes (cpp/tests/sicx/tests.cmake:130; cpp/tests/sicx/run_fixture_hashes.cmake:3)` | `hash pin` | `nothing` |
| `sicx/canonical/d16.decode.sha256` | private reference decode comparison | `hash pin` | `nothing` |
| `sicx/canonical/d16.provenance.json` | sic_test_sicx_generate_d16 (cpp/tests/sicx/fixtures/generate_inputs.cmake) | `exact-part digest oracle` | `nothing` |
| `sicx/canonical/d16.sicx` | sic_test_decode_anchor_fixtures (RAYS anchor oracle); copied (cpp/cmake/checks/FixtureCorpus.cmake); sic_test_sicx_fixture_hashes (cpp/tests/sicx/tests.cmake; cpp/tests/sicx/run_fixture_hashes.cmake); private reference decode comparison | `golden` | `sicx/SHA256SUMS via copied fixture` |
| `sicx/canonical/d32.decode.sha256` | private reference decode comparison | `hash pin` | `nothing` |
| `sicx/canonical/d32.provenance.json` | sic_test_sicx_generate_d32 (cpp/tests/sicx/fixtures/generate_inputs.cmake) | `exact-part digest oracle` | `nothing` |
| `sicx/canonical/d32.sicx` | sic_test_decode_anchor_fixtures (RAYS anchor oracle); copied (cpp/cmake/checks/FixtureCorpus.cmake); sic_test_sicx_fixture_hashes (cpp/tests/sicx/tests.cmake; cpp/tests/sicx/run_fixture_hashes.cmake); private reference decode comparison | `golden` | `sicx/SHA256SUMS via copied fixture` |
| `sicx/canonical/d64.decode.sha256` | private reference decode comparison | `hash pin` | `nothing` |
| `sicx/canonical/d64.provenance.json` | sic_test_sicx_generate_d64 (cpp/tests/sicx/fixtures/generate_inputs.cmake) | `exact-part digest oracle` | `nothing` |
| `sicx/canonical/d64.sicx` | sic_test_decode_anchor_fixtures (RAYS anchor oracle); copied (cpp/cmake/checks/FixtureCorpus.cmake); sic_test_sicx_fixture_hashes (cpp/tests/sicx/tests.cmake; cpp/tests/sicx/run_fixture_hashes.cmake); private reference decode comparison | `golden` | `sicx/SHA256SUMS via copied fixture` |
| `sicx/d2-c0.json` | `sic_test_sicx_d2_fixture (cpp/tests/sicx/tests.cmake:83; cpp/tests/sicx/run_sicx.cmake); copied (cpp/cmake/checks/FixtureCorpus.cmake:4); sic_test_sicx_fixture_hashes (cpp/tests/sicx/tests.cmake:130; cpp/tests/sicx/run_fixture_hashes.cmake:19)` | `golden` | `sicx/SHA256SUMS via copied fixture` |
| `sicx/d2.jsonl` | `sic_test_sicx_d2_fixture (cpp/tests/sicx/tests.cmake:83; cpp/tests/sicx/run_sicx.cmake); copied (cpp/cmake/checks/FixtureCorpus.cmake:4); sic_test_sicx_fixture_hashes (cpp/tests/sicx/tests.cmake:130; cpp/tests/sicx/run_fixture_hashes.cmake:19)` | `golden` | `sicx/SHA256SUMS via copied fixture` |
| `sicx/d2.sicx` | sic_test_decode_anchor_fixtures (RAYS anchor oracle); copied (cpp/cmake/checks/FixtureCorpus.cmake); sic_test_sicx_fixture_hashes (cpp/tests/sicx/tests.cmake; cpp/tests/sicx/run_fixture_hashes.cmake); private reference decode comparison; sic_test_construct_d2 (cpp/tests/sicx/run_exact.cmake; exact-prefix oracle) | `golden` | `sicx/SHA256SUMS via copied fixture` |
| `sicx/d4-c0.json` | `sic_test_sicx_d4_fixture (cpp/tests/sicx/tests.cmake:32; cpp/tests/sicx/run_sicx.cmake); copied (cpp/cmake/checks/FixtureCorpus.cmake:4); sic_test_sicx_fixture_hashes (cpp/tests/sicx/tests.cmake:130; cpp/tests/sicx/run_fixture_hashes.cmake:19)` | `golden` | `sicx/SHA256SUMS via copied fixture` |
| `sicx/d4.jsonl` | `sic_test_sicx_d4_fixture (cpp/tests/sicx/tests.cmake:32; cpp/tests/sicx/run_sicx.cmake); copied (cpp/cmake/checks/FixtureCorpus.cmake:4); sic_test_sicx_fixture_hashes (cpp/tests/sicx/tests.cmake:130; cpp/tests/sicx/run_fixture_hashes.cmake:19)` | `golden` | `sicx/SHA256SUMS via copied fixture` |
| `sicx/d4.sicx` | sic_test_decode_anchor_fixtures (RAYS anchor oracle); copied (cpp/cmake/checks/FixtureCorpus.cmake); sic_test_sicx_fixture_hashes (cpp/tests/sicx/tests.cmake; cpp/tests/sicx/run_fixture_hashes.cmake); private reference decode comparison | `golden` | `sicx/SHA256SUMS via copied fixture` |
| `sicx/d8-c0.json` | `sic_test_sicx_d8_fixture (cpp/tests/sicx/tests.cmake:32; cpp/tests/sicx/run_sicx.cmake); copied (cpp/cmake/checks/FixtureCorpus.cmake:4); sic_test_sicx_fixture_hashes (cpp/tests/sicx/tests.cmake:130; cpp/tests/sicx/run_fixture_hashes.cmake:19)` | `golden` | `sicx/SHA256SUMS via copied fixture` |
| `sicx/d8-maximal.sicx` | sic_test_decode_anchor_fixtures (RAYS anchor oracle); copied (cpp/cmake/checks/FixtureCorpus.cmake); sic_test_sicx_fixture_hashes (cpp/tests/sicx/tests.cmake; cpp/tests/sicx/run_fixture_hashes.cmake); private reference decode comparison | `golden` | `sicx/SHA256SUMS via copied fixture` |
| `sicx/d8_release_catalogue.json` | `sic_test_sicx_d8_release_layout (cpp/tests/sicx/tests.cmake:37; cpp/tests/sicx/run_sicx.cmake); copied (cpp/cmake/checks/FixtureCorpus.cmake:4); sic_test_sicx_fixture_hashes (cpp/tests/sicx/tests.cmake:130; cpp/tests/sicx/run_fixture_hashes.cmake:19)` | `golden` | `sicx/SHA256SUMS via copied fixture` |
| `sicx/d8.jsonl` | `sic_test_sicx_fixture_hashes (cpp/tests/sicx/run_fixture_hashes.cmake)` | `archived exact presentation` | `sicx/SHA256SUMS via copied fixture` |
| `sicx/d8.sicx` | sic_test_decode_anchor_fixtures (RAYS anchor oracle); copied (cpp/cmake/checks/FixtureCorpus.cmake); sic_test_sicx_fixture_hashes (cpp/tests/sicx/tests.cmake; cpp/tests/sicx/run_fixture_hashes.cmake); private reference decode comparison | `golden` | `sicx/SHA256SUMS via copied fixture` |
| `sicx/d8_native.jsonl` | `sic_test_sicx_d8_fixture; sic_test_sicx_d8_release_layout (cpp/tests/sicx/run_sicx.cmake); sic_test_sicx_fixture_hashes` | `native exact records and exact root oracle` | `sicx/SHA256SUMS via copied fixture` |

The flat build-tree copy contract is enumerated below. Canonical filenames are
flattened only in the build tree; source records retain their canonical folder.

<!-- build-copy: sicx/canonical/d16.sicx -->
<!-- build-copy: sicx/d2-c0.json -->
<!-- build-copy: sicx/d2.jsonl -->
<!-- build-copy: sicx/d2.sicx -->
<!-- build-copy: sicx/canonical/d32.sicx -->
<!-- build-copy: sicx/d4-c0.json -->
<!-- build-copy: sicx/d4.jsonl -->
<!-- build-copy: sicx/d4.sicx -->
<!-- build-copy: sicx/canonical/d64.sicx -->
<!-- build-copy: sicx/d8-c0.json -->
<!-- build-copy: sicx/d8-maximal.sicx -->
<!-- build-copy: sicx/d8_native.jsonl -->
<!-- build-copy: sicx/d8_release_catalogue.json -->
<!-- build-copy: sicx/d8.jsonl -->
<!-- build-copy: sicx/d8.sicx -->
