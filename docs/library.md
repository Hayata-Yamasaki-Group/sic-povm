<a id="library-c-library-api"></a>
# C++ library API

Build with C++20. `find_package(sic REQUIRED COMPONENTS core)` provides `sic::core` without FLINT.
The `support` and `search` components are also available without FLINT. A FLINT-enabled installation
additionally provides components and targets `sicx`, `decode`, `transport`, `access`, `bounds` and
`data`. The `data` component is also available without FLINT; exact decoding requires FLINT. These
resolve FLINT and optional HDF5 on the consumer host. HTTP access loads the libcurl ABI by its
loader name (`SIC_CURL_SONAME`, default `libcurl.so.4`); the host loader search path determines its
location. The core does not link curl or FLINT. Producer and decoder source identity definitions
are not exported to consumers.

| Header | Ownership and use |
| --- | --- |
| `sic/core/sic_frame.hpp` | `SicFrame` owns its normalized vector. Immutable calls use local scratch. `SicVectorStream` borrows a frame, rejects temporary frames, and must be serialized by its caller. |
| `sic/core/clifford.hpp` | Immutable action owns its discrete Fourier transform and roots. Dimension admission occurs before allocation. Constructor resource limits may be raised within native index bounds. |
| `sic/core/fiducial_io.hpp` | Readers return owned vectors. `read_vector(stream, InputLimits)` bounds bytes and dimensions before storing entries. |
| `sic/support/ball_vectors.hpp` | Ball vectors own initialized FLINT storage. Lvalue `data()` and `span()` borrow mutable or const balls without allocation; keep the owner alive and reacquire after assignment, move or structural mutation of the public storage member `v`. `v` is part of the public contract for storage inspection and FLINT value access; the wrapper remains its sole owner. Borrowers may change values through FLINT, but must not clear, initialize, shallow-copy, resize or replace the raw structs. `ComplexBallVector` copies allocate and deep-copy all payloads; `RealBallVector` is move-only. Move construction is constant time; move assignment also clears the destination's old payload. |
| `sic/support/flint_raii.hpp` | `flint::FmpqMat` and `flint::AcbMat` own matrices and expose `rows()`/`columns()`. Copies are forbidden; moves transfer storage, with assignment exchanging matrices. Borrowed C handles may change entries but must not clear/reinitialize the matrix or replace it with a window. Negative dimensions throw `sic::Error(invalid_argument)`. |
| `sic/sicx/io.hpp` | `Reader` retains shared ownership of its `ByteSource`; returned metadata/index references remain valid until reader destruction. Chunk and `ElementChunk` owners retain immutable buffers; spans require their corresponding owner to remain alive. Cache mutation is synchronized. `StreamWriter` is an exclusive owner, uses private partial files, and validates reader acceptance before publication. Its callbacks borrow the writer's private path for the duration of the call. |
| `sic/sicx/verify.hpp` | `verify_exact` and `verify_all` borrow a reader for the call and return owned reports. `VerificationPolicy` configures precision and valuation tables. They establish the specified container and exact-format checks, not a general exact SIC proof. |
| `sic/decode/decoder.hpp` | `Decoder` borrows its reader and optional progress stream; both must outlive it. Temporaries are rejected. Construct, call, inspect stats, and destroy on one owner thread. `sic::support::ComplexBallVector` results own their Arb balls; pointers into them last until that owner is changed or destroyed. `stats()` returns a const reference to internal statistics, updated by later decoder calls. |
| `sic/access/remote.hpp` | Sources own their transport state. Reads and plans serialize internally. Independent decoder jobs require distinct underlying sources and active cache directories. A source binds to one cancellation context for its lifetime; conflicting bindings are refused. |
| `sic/sicx/exact.hpp` | Exact export owns its output. For an imaginary base, `BuildOptions::conjugation_images` supplies images of every generator in the corresponding field level. All relations, involution and embedding identification are checked. Empty images use automatic discovery for diagonal quadratic conjugation. Other presentations require explicit images and otherwise receive `unsupported`. Search work is admitted by `maximum_conjugation_search`. |
| `sic/data/layout.hpp` | `ReleaseOptions`, `write_exclusive`, `multiplet_from_provenance`, `release_layout` and `release_catalogue` build release trees. Inputs are borrowed for each call; returned paths and JSON values are owned by the caller. Catalogue file hashing, canonical input parsing and directory-name matching are private implementation helpers. |
| `sic/bounds/overlap_bounds.hpp` | Input is borrowed for the call. Results own their intervals. `admit_wh_bounds` checks declared geometry/workspace before input allocation; the full call also admits actual mantissas. Results are rigorous numerical enclosures, not exact-zero certificates. |

`ResourceLimits` separates reader/writer resource policy from SICX 1.0 wire limits. Pass the same
policy to writer and reader when raising defaults. `ValuationPolicy` admits each dense table and
their total; verification uses the same estimator. `PrecisionPolicy` and decoder `maximum_precision`
are computational limits. BALL retains its normative maximum of 1024 limbs, independently of
informative working precision. A higher computational ceiling does not widen the wire format.
`InputLimits` configures core text input. HDF5 reads admit dimensions and vector bytes through the
default `InputLimits` before allocation. `RemoteOptions::limits` configures transport resource
bounds.

A decoder retains its own shared cancellation token. Call `request_stop()` from another thread, or
construct the token with a stop-file path. Workers inherit this context; cancellation is sticky and
a cancelled decoder should be destroyed. The command-line tool binds its stop file during bootstrap and decoding.
The decoder reads no environment variables and writes progress only to its supplied stream.
Applications must serialize callbacks/shared streams themselves. A failed decode yields no partial
result; caches and stats may have advanced and a precision refusal may trigger a retry. Option
validation happens before pool creation. Use a new decoder after cancellation or a resource refusal.

Each owner must have one native FLINT worker configured before constructing a decoder. Pool workers
configure only their own FLINT thread-local worker setting. Do not change the FLINT runtime or call
`flint_cleanup()` while readers, decoders or ball results are alive. Decoder `pin=true` temporarily
pins its owner and restores affinity at destruction; embedding applications normally select
`pin=false`. `tune_process_allocator` is an explicit process-wide glibc opt-in which cannot be
undone by decoder destruction. The command-line tool enables this tuning. Independent
instances use separate readers, sources, cancellation tokens and progress streams. Constructor
kernels still support the process-global stop-file API; decoder worker scopes isolate their
cancellation from it.

`sic::Error` requires an explicit `ErrorKind` at construction and has a stable `kind()` (`invalid_argument`, `invalid_format`, `integrity`, `io`,
`resource`, `precision`, `cancelled`, `unsupported`, `construction`). Three failure classes are
distinct: `construction` means a construction premise, recognition or exact/SIC acceptance check
failed, including structural refusal of the specialized construction; `precision`/`resource` mean a
computational allowance was exhausted and do not establish that a premise is false;
`invalid_argument`/`invalid_format` mean bad input. JSON syntax errors derive from `sic::Error`.
Cancellation, transport I/O and digest failures retain their own categories. The installed support
durability functions—both forms of `sync_file` (descriptor and path), `sync_parent`,
`publish_replace`, `publish_no_replace` and both `atomic_write` overloads—throw `sic::Error` with
kind `io` for I/O failures. Allocation failures, native arithmetic overflow and internal invariant
failures remain standard C++ exceptions; catch `std::exception` at application boundaries. Checked
core and search caller preconditions throw `sic::Error(invalid_argument)`, including invalid
dimensions, vector sizes, checked aliases, discrete Fourier transform signs, nonunit inverses, determinant mismatches and
normalization input. `verify_fiducial` rejects dimensions above `INT_MAX`; `CliffordAction` rejects
dimensions outside `1..min(maximum_dimension, 2^30)` as caller preconditions. The normalization
operations `core::normalized_copy` and `core::SicFrame` throw `sic::Error(invalid_argument)` for
empty, zero or nonfinite normalization input; frame dimension limits throw `sic::Error(resource)`.
Core interchange readers report malformed content as `invalid_format`, CRC mismatches as
`integrity`, I/O failures as `io`, unsupported profiles as `unsupported`, and input limits as
`resource`. Writers reject invalid caller input, including nonfinite coordinates, as
`invalid_argument`; write/flush/close failures are `io`, size limits are `resource`, and unsupported
output profiles are `unsupported`. `core::interchange::write_file` publishes atomically: it
creates a new inode with mode `0666` filtered by the process umask, and replaces an existing
regular file without changing its other hard links. An existing symlink (including a dangling
symlink), device or FIFO target is refused as `io`. HDF5 read/write calls report `unsupported` when `have_h5()` is
false. `sic::sicx::fail(kind, message)` throws `sic::Error` with the supplied kind. The `SICX: `
prefix and other diagnostic strings are not API. The shared command-line reporter prints the category in
brackets; see the [command argument contract](#command-argument-contract) for exit codes.

A refused shared worker-team growth from `parallel_for` throws `AdmissionError`
(a `sic::Error` with kind `resource`) before dispatch; the existing team remains usable.
Native thread startup failures, including batch-encoder startup, retain
`std::system_error`. Point inversion retains its serial startup fallback after lazy
regrowth; optional linear refinement declines its proposal on the same startup or
admission failures. Callback failures propagate unchanged. Native helper workers release their
FLINT caches when the region ends; row-team and decoder-pool workers retain caches
until their facade releases the shared team or is destroyed. Row-facade destruction
also joins idle native workers; surviving facades lazily regrow the shared team.
Regrowth after an idle release or facade destruction repeats admission against
live resident set size. A facade admitted at construction can therefore throw `AdmissionError`
at a later dispatch before any callbacks run. Idle workers use a spin deadline of 50
microseconds, then sleep.

MAT Level-5 output dimensions and element sizes are admitted before narrowing;
input tags and numeric payload sizes are checked before numeric array allocation. NPZ uses the
documented store-only non-ZIP64 profile and refuses aggregate output beyond its named profile bound
before NPY serialization. Split large bundles or choose HDF5.

## Numeric text conversion

`sic/support/parse_number.hpp` provides `sic::support::parse_number<T>(text, name,
kind, low, high, base)` for signed and unsigned integers and finite floating values.
Bounds are inclusive and default to the native type's limits. Conversion consumes
the entire string, is locale independent, and rejects overflow, whitespace and a
leading plus; signed values may have a leading minus. Integer bases are 2–36
(default 10), without prefixes; floating values use decimal general format.
Callers retain format tokenization and canonical spelling checks. Use
`ErrorKind::invalid_argument` for options and `invalid_format` for file or wire
records. Failures throw `sic::Error`. The installed `sic/support/cli.hpp`
`number<T>` facade retains its default lower bound of zero, `UsageError` subtype
and usage exit code 2.

<a id="library-compiled-core-example"></a>
## Compiled core example

This example compares the immutable frame API with the direct Weyl–Heisenberg API on a
normalized input. It is a floating-point diagnostic, with no SIC claim. CTest
extracts, compiles and runs this block in both plain and FLINT builds.


<!-- example -->
```cpp
#include "sic/core/sic_frame.hpp"

int main() {
  const sic::core::Vector input{{1.0, 0.0}, {0.0, 0.0}};
  const sic::core::SicFrame frame(input);
  const auto overlaps = frame.overlaps();
  const auto direct = sic::core::WeylHeisenberg(2).overlaps(input);
  return frame.dim() == 2 && overlaps == direct ? 0 : 1;
}
```

<a id="library-link-time-optimization-and-static-consumers"></a>
## Link-time optimization and static consumers

`SIC_ENABLE_LTO` defaults to ON. CMake checks IPO support and enables LTO only
in Release and RelWithDebInfo; disable it with `-DSIC_ENABLE_LTO=OFF`. Other configurations and sanitizer
builds do not enable LTO. Independent checker executables use LTO within each
checker's own sources and external dependencies; the norm checker instead uses
private inline definitions for its measured hot functions. Checkers do not link
production code or other checkers. Test translation units retain their own settings.

GCC builds retain native code in fat LTO static archives. Installed consumers
can compile and link with `-fno-lto`; both installed-consumer tests exercise
that path. With other compilers, LTO archives require the matching compiler
and linker plugin, and consumers must link with the toolchain LTO flag
(for Clang, `-flto`). Build the installation with `SIC_ENABLE_LTO=OFF` when
distributing archives to consumers without that toolchain. These compiler
requirements do not change the public C++ API.

## Overlaps and streaming verification

<a id="overlaps-and-streaming-verification"></a>
The convention is
`D_(a,b) = tau^(a*b) X^a Z^b`, with `tau = -exp(pi*i/d)`,
`X|j> = |j+1>` and `Z|j> = exp(2*pi*i*j/d)|j>`.

```cpp
#include "sic/core/verify.hpp"
#include "sic/core/weyl_heisenberg.hpp"

sic::core::Vector psi = {0.0, 1.0, -1.0};
sic::core::WeylHeisenberg wh(static_cast<int>(psi.size()));
sic::core::Vector row;
wh.overlap_row(1, psi, row);  // row[b] = <psi|D_(1,b)|psi>
auto overlaps = wh.overlaps(psi);  // overlaps[a*d+b]
auto result = sic::core::verify_fiducial(psi);  // normalizes a copy, streams rows
bool numerically_consistent = result.is_fiducial(1e-12);
```

Overlap values retain the scale of the supplied vector. For power-of-two dimensions, an unscaled
positive-exponent radix-two fast Fourier transform computes each row in `O(d log d)` arithmetic operations. Other
dimensions retain direct summation. The immutable root/permutation tables take `O(d)` storage and
are reused by calls on the same `WeylHeisenberg` object. The fast Fourier transform is shared by `Dft`,
`WeylHeisenberg`, `SicFrame` and Clifford element application. Independent calls are safe when their
output vectors are distinct. `overlap_row` resizes and reuses its output, which must be a different
vector object from the input. Negative and large integer shift labels preserve the displacement's
phase, including its even-dimensional sign under a shift by `d`.

`overlaps` returns `d*d` complex numbers and needs `O(d^2)` output storage.
`verify_fiducial` instead retains one row and uses `O(d)` total storage, with
`O(d^2 log d)` work at power-of-two dimensions. It rejects empty, zero and
nonfinite input, and an input norm that cannot be represented as a finite
double. Scaled normalization avoids intermediate norm-square overflow or
underflow. Acceptance also requires a finite nonnegative tolerance. Floating
rounding errors are not enclosed by this API; use the certified construction
or independent exact checker for mathematical certification.

The regression suite compares fast Fourier transform rows with independently applied displacement
matrices, checks all labels of the basis vector in dimension 2048 and the Fourier norm identities,
and checks scaled known SIC-POVM fiducial vectors and the rejection of nonfinite input. The measured
resources of the release are given in [reproduction](reproduce.md#measured-resources).

<a id="clifford-1-library-api-link-siccore-headers-in-cppincludesiccore"></a>
<a id="frame-dft-and-clifford-signatures"></a>
## Frame, discrete Fourier transform and Clifford signatures

| header | contents |
| --- | --- |
| `core/fft.hpp` | `sic::core::Dft(n)`: unscaled in-place discrete Fourier transform, `transform(data, sign)` with `sign = -1` (`exp(-2 pi i jk/n)`) or `+1`; radix-2 for power-of-two `n`, direct sum otherwise. |
| `core/sic_frame.hpp` | `sic::core::SicFrame(psi)`: normalizes a copy of the fiducial. `displace(p1, p2, v, out)` applies `D_p` to any vector in O(d); `vector(p1, p2, out)`; `row_block(p1, out)` (the d vectors with fixed `p1`); `for_each_vector(fn)` and `sic::core::SicVectorStream` stream all d^2 vectors with O(d) memory; `overlap_row(p1, out)` gives `<psi|D_(p1,p2)|psi>` for all `p2` with one fast Fourier transform; `cross_overlap_row(p1, phi, out)` gives `<D_(p1,p2) psi|phi>`; `overlaps()` the full d x d table. `sic::core::check_sic(frame)` returns `SicCheck{d, input_norm, norm_defect, max_overlap_error, worst_p1, worst_p2, parseval_defect}`; `passes(tol)`. |
| `core/clifford.hpp` | `sic::core::clifford`: 2x2 matrices mod `n` (`Mat2`, `mul`, `det`, `inverse`, `canonical_lift`); `CliffordAction(d).apply(F, v, out)` applies Appleby's `U_F` (`F` in ESL(2, Z_dbar), `dbar = 2d` for even d; det -1 means `U_F = U_(FJ) K`, `J = diag(1,-1)`, `K` complex conjugation) in O(d log d) for power-of-two d (direct discrete Fourier transform otherwise); `factorize`; `apply_element` (element application only). |
| `core/fiducial_io.hpp` | `sic::core::files::read_fiducial(path)`: strict readers for `.csv` (`index,real,imag`), `.npy` (`<c16`, shape `(d,)`) and the `.txt` vector format; `sic::data::read_catalogue` in `<sic/data/catalogue.hpp>` reads an explicitly supplied JSON or TSV catalogue. |

`sic::core` owns the shared Weyl–Heisenberg and fast Fourier transform implementation and the binary64 reader family.
The target `sic::search` and header `sic/search/clifford.hpp`
provide `phase_table`, `clifford_moments`, `z_error_bound` and `clifford_maps`.
The stabilizer certifier links FLINT directly and uses its own modular arithmetic,
catalogue reader and right-factor decomposition; it shares no production headers
or libraries. The `independence` CTest label enforces this boundary.

`U_F` (Appleby2005symmetric): for det F = 1 and `b` a unit mod `dbar`,
`U_F = d^(-1/2) sum_{u,v} tau^(b^(-1)(a v^2 - 2uv + e u^2)) |u><v|`, which is a chirp, a
length-d fast Fourier transform and a chirp; otherwise `F = F1 F2` with `F1 = [[0,-1],[1,x]]`. The unit test
checks `U_F D_p U_F^dagger = D_(F p)` and unitarity for random F at d = 2, 3, 4, 5, 6, 8, 16, 32.

Tests (`sic-test-search-clifford`, CTest `sic_test_search_clifford_*`) compare fast Fourier transforms with direct sums
at selected dimensions through 64, and frame, displacement, overlap and cross-overlap operations
at selected dimensions through 16. The small-fixture selection contains 12 binary64 fiducial vectors at d = 2..64
and compares them with brute-force overlaps and `check_sic`. Recorded maximum overlap error was
< 1e-13 and maximum `check_sic` deviation <= 1.4e-16.

<a id="bounds-bounded-numerical-wh-overlap-deviation-bounds"></a>
## Certified bounds on Weyl–Heisenberg overlap deviations

The compiled C++20/FLINT target `sic::bounds` exposes
`<sic/bounds/overlap_bounds.hpp>`, independently of command-line tool headers. It accepts
caller-owned complex balls and returns rigorous numerical squared-norm and
maximum squared-overlap-deviation intervals. It does not return an exact-zero
SIC certificate or assert a new SIC/classification result.

Root builds enable it with `SIC_ENABLE_FLINT=ON`. Installed consumers use `find_package(sic CONFIG
REQUIRED COMPONENTS bounds)` and link `sic::bounds`; installation also provides the numerical
`sic::core` component. The source-tree consumer at `cpp/tests/install/bounds` uses
`add_subdirectory` and links the public `sic::bounds` target; it does not test installed
`find_package` discovery.

```cpp
#include <sic/bounds/overlap_bounds.hpp>

// Application owns initialized input[0..dimension), native worker setup and
// FLINT lifetime. It must not mutate the input/runtime during this call.
flint_set_num_threads(1);
{
  auto bounds = sic::bounds::bound_wh_overlap_deviations(input, dimension);
  // All original input vectors have norm and maximum deviation in these balls:
  arb_srcptr norm = bounds.squared_norm();
  arb_srcptr deviation = bounds.maximum_squared_overlap_deviation();
  // A caller can prove deviation below an outward-rounded positive tolerance
  // using arb_lt; a small upper bound remains a numerical tolerance statement.
  (void)norm;
  (void)deviation;
}
flint_cleanup();
```

The move-only result owns its bounds; moved-from access throws. It retains
no borrowed input pointer. One native FLINT worker is required in the caller's
thread-local budget. This check does not reserve an exclusive process lease;
concurrent calls/resource admission are not guaranteed. No global worker or
allocator setting is changed by the library.

<a id="bounds-formula-and-enclosure-contract"></a>
### Formula and enclosure contract

Write omega=exp(2*pi*i/d), tau=-exp(pi*i/d), and

    D_(a,b) = tau^(a*b) X^a Z^b,
    N(v) = sum_j (Re(v_j)^2 + Im(v_j)^2),
    f_a(j) = conj(v_j) v_(j-a),
    F_a(b) = sum_j f_a(j) omega^(-b*j).

The negative-exponent FLINT discrete Fourier transform obeys

    <v|D_(a,-b)|v> = tau^(-a*b) omega^(a*b) F_a(b).

Both phase factors have modulus 1, including even dimensions. Consequently
|F_a(b)|^2/N(v)^2 covers every squared overlap of the normalized vector.
The checked deviation is

    delta_(a,b) = abs(|F_a(b)|^2/N(v)^2 - 1/(d+1)).

All d^2-1 nonidentity labels are visited, with b reindexed by its negative.
There is no loss of even-dimensional tau signs: only the mathematically
unit-modulus factors are omitted after taking squared modulus.

Every input real/imaginary midpoint is first enclosed using `acb_set_round`
at the requested precision, after admission. The norm and every product use
that same bounded owner. Each original input vector is therefore represented
throughout the interval computation. A provably positive norm lower bound is
mandatory, so normalization applies uniformly to every represented vector.
Dependency overestimation widens bounds and never invalidates them.
`arb_max` encloses the maximum for every represented vector; it does not
recover an exact maximum over the input box or an exactly vanishing residual.
All intermediates/results must be finite.

The maximum-deviation stored interval radius must be at most
2^(-minimum_absolute_accuracy_bits). This is an absolute interval-radius gate,
not relative midpoint accuracy or a SIC acceptance threshold. A narrow,
provably positive deviation is a successful measurement of a non-SIC control.
An uncertain norm or an overly wide deviation interval refuses. No precision
clamp, automatic retry or conversion of uncertainty to success is performed.

<a id="bounds-limits-and-workspace"></a>
### Limits and workspace

`BallVerificationLimits` has defaults of 256 working bits, 64 absolute radius bits, maximum 4096
working bits, maximum dimension 2048, 64 Mi total input midpoint bits, 512 MiB admitted workspace
and a 10,000,000,000 work score. Callers may raise the dimension and precision limits up to
`LONG_MAX/2` and `sic::kNativeMaximumPrecision`, respectively, subject to work and workspace
admission. Dimension is at least 2. Working precision is at least 64 bits, with requested absolute
radius accuracy in 1..working_bits-32. Exceeding any limit throws.

Input shape, finite Lagom exponents/radii and total midpoint-bit metadata are
checked without copying input mantissas. Excessively wide input refuses
before any owner/discrete Fourier transform allocation; accepted wide inputs are precision-rounded,
never exactly copied into the working owner. Input bits also contribute to
this explicit declared admission score:

    d^3 + 8*d^2 + ceil(total_input_midpoint_bits / FLINT_BITS).

This is engineering work admission, not a native-instruction bound or a
bit-complexity theorem for FLINT. The default score admits dimension 2048, including
its 4,194,303 nonidentity labels. Lower caller scores still refuse explicitly.

The shared resource budget admits an additional forecast

    64MiB + 256*d*complex_bytes(working_bits+128)

before allocating owners or the discrete Fourier transform plan. The caller's workspace cap bounds
this engineering allowance; it is not a universal hard cap on native
allocations. Fresh process/cgroup memory inspection, the shared reserve and
`SIC_MEMORY_MB` are retained. Explicit owned storage is three length-d complex
vectors plus a constant number of real bounds/scalars. One immutable per-call
FLINT discrete Fourier transform plan is reused by all serial rows. No d^2 overlap array is allocated.
For dyadic dimensions FLINT selects radix-two transforms. A host memory or
cgroup ceiling remains a separate operating-system control.

<a id="bounds-build-and-tests"></a>
### Bounds validation

The public API tests compare independent tau-phased direct sums at d = 2,3,4,5,6,7,8,9,10,12,16,32.
Controls cover the exact vector in dimension 2, valid and mutated qutrits, basis maxima d/(d+1), extreme scaling,
nonzero-radius boxes, positive-norm and nonfinite refusals, all precision/work/storage/input limits,
worker budgets, and enclosing rounding of million-bit input midpoints. The basis control in
dimension 2048 checks all 4,194,303 labels and 2048 rows; it is a known non-SIC-POVM vector and
establishes no result about SIC-POVMs.

<a id="producer-components"></a>
<a id="producer-native-producer-components"></a>
## Producer components

`cpp/apps/construct/main.cpp` parses no kernels: it calls `sic::construct::run`. `Construction` owns
one attempt/session and is neither copied nor moved. The stage files share its declarations in
`cpp/src/construct/model/session_state.hpp`; small hot accessors remain inline to preserve the call
costs.

| Module | Responsibility |
| --- | --- |
| `initialize`, `group_stage`, `structure` | options, admission, arithmetic/group, certified support and table kernels |
| `tower`, `step_data` | adapted tower, base candidates and certified generator evaluation |
| `entries` | entry formulas, representative channels, forward transforms and recognition helpers |
| `precision`, `branch_path`, `branch/branch_values` | empirical precision proposal, branch storage and bounded inverse-tree path |
| `final_pass`, `output` | attempt lifetime, recognition retries, canonical generator rescaling, shared writer and authenticated resume |
| `sic_check` | certified Weyl–Heisenberg overlap bound and publication from the quarantine path |
| `alignment`, `alignment_selector`, `alignment_search` | conjugate alignment input services, selector/admission and deterministic memory-reduced search |

`components.hpp` separates generator selection, exact representation transformations, rescaling, and
the SICX writer. The native defaults are conjugate alignment, no representation transformations, the
shared greedy dyadic rule, and the shared SICX writer. `--generator-selector step-selection` selects
the step-selection presentation. Nonempty representation transformations and nondefault native
rescaling rules are explicitly refused until their required written-output checks are implemented.
`NoRescalingRule` exists as an internal interface/conformance control.

Conjugate alignment proposes only one bare Galois conjugate of an eligible real quadratic generator.
The core checks eligibility and the exact Smith word, applies a signed permutation before generator
spilling, and preserves the base generator's certified conjugate/separation bounds. Choices are
frozen at the original coarse precision and reused at every final precision retry. General linear
Galois combinations are represented by the interface but are not accepted by this implementation. The search
is an empirical accelerator; it proves no uniform precision or Newton conditioning bound and changes
none of the construction's imported mathematical premises.

The search budget includes logarithm inputs, samples, retained F tables or replay tiles,
scoring batches, candidate workers and temporary evaluation data. Admission also includes the
resident coarse evaluator, table/entry/representative owners and measured live resident set size.
Cache mode is selected only if the complete incremental and global live sets both fit; otherwise
F is replayed. Eligibility is refined after the coarse inputs, followed by a second admission
before search allocation. Each Galois choice supplies `forecast_at(bits)` for its permutation
scratch at the precision actually used. Arithmetic counts are checked. Native/allocator
workspace coefficients are conservative engineering allowances, not proved allocator bounds.
`--alignment-trace` checks observed resident set size against the admitted search ceiling.

Every retention plan evaluates the same coarse inputs before parking final data. The shared planner
chooses exact retention, spills and deterministic recomputation under the discovered budget. Compact
towers, including odd steps, support bounded analytic-table and generator reloads, folded real top
traces and projected physical dual bases. Reduced-order layouts with k > 1 keep their full analytic
table while exact leaves are planned independently. Canonical bytes and authenticated resume are
independent of the plan. Storage, cache, batch and worker controls do not enter semantic provenance.
Search seed, training count, candidate cap, sweeps, base candidates and exact Galois terms do; their
digest and component versions bind durable resume journals and plans. Group, analytic and class
cache semantics precede generator selection. Completed-class caches retain the project's stricter
executable/source pinning.

At quiescent phase transitions, free allocator pages are returned before measuring the
next phase's headroom. Memory pressure can evict backed or recomputable caches before
worker admission. I/O contention uses cumulative stall time from the cgroup's
[PSI interface](https://docs.kernel.org/accounting/psi.html), with a system fallback;
quiet traffic does not change the contention threshold. Missing, reset or changed-scope
samples restart the observation window. I/O-only contention lowers workers while retaining
reusable caches. The separate transfer-cost estimate influences retention choices.

`QuarantineOutput` serializes one output target, owns only a pinned private inode, refuses
clobbering the final path, and preserves foreign/unowned paths. A completed replay requires the
prior regular spool and journal plus authenticated plan/component notes before the completed
private file is removed. The producer publishes only after the shared SIC bound passes. Internal
test hooks exercise refusal and completion-before-publication crashes.

`sic-container reencode IN OUT [--threads N]` is the identity file route: `stored_element_source`
plus the inverse recorded dyadic shifts, recomputation of the shared default rescaling, and checked
serialization. It preserves all chunks, physical grouping and optional records byte for byte. Its
preflight includes both Reader caches, directory/metadata growth, codec windows, valuation tables
and inverse-tree scratch. Tests reject noncanonical C and insufficient budgets. This route preserves
the tower presentation and accepts no nonempty representation transformations or nondefault
rescaling rules.

`cpp/tests/construct` covers component contracts, ownership/no-clobber publication, completed
replay, bounded branch forecasts, the file route and conjugate alignment cache/replay/storage/resume
controls. Canonical native goldens pin the selected presentation.

## Threads and resources

Default library producer threads use distinct physical cores in the process CPU
allowance, capped by cgroup CPU quotas/cpusets and SLURM_CPUS_PER_TASK. The producer command-line tool
also applies its dimension-dependent work cap; the decoder command-line tool caps its default
at 32. Linux topology is cached once per CPU for the process lifetime. On macOS, logical
allowance uses hw.logicalcpu (hardware_concurrency() as fallback) and physical defaults
use hw.physicalcpu (or one). Other platforms without affinity use hardware_concurrency()
for logical allowance and one physical core for defaults. Unknown Linux cgroup-v2
ancestry limits defaults to one worker only. Memory discovery still fails closed.
Malformed readable quotas are errors.

Explicit SIC_THREADS in the library and --threads in the producer command-line tool take precedence
over the default and are never clamped to the CPU allowance; invalid counts are refused.
Decoder teams retain the requested count, subject to `maximum_threads` and memory
admission. Physical compute dispatch uses a subset on distinct placement cores when
placement CPUs are known; otherwise it uses the full team. There is no process-allowance
cap on explicit counts. Internal producer fan-out uses its caller's worker budget,
bounded by available work and memory.
Snapshot::cpus retains logical allowance and SLURM_MEM_PER_CPU accounting. The production command-line tool
captures its options, validates documented inherited producer settings, and clears inherited
SIC_* settings before running the producer. Invalid unused settings are refused before output
probes or construction work.

<a id="producer-memory"></a>
## Producer memory

On glibc, `sic-construct` sets the process heap trim threshold to 16 MiB and the
mmap threshold to 32 MiB before starting construction threads. Ordinary frees
can then release free top chunks in worker arenas while medium-sized temporaries
remain on the heap. If mmap threshold setup is refused, the command-line tool skips trim setup
and continues with allocator defaults. If trim setup is refused, it continues
with the mmap setting. Any environment
variable whose name starts with `MALLOC_`, or any colon-separated entry starting
with `glibc.malloc.` in `GLIBC_TUNABLES`, disables the entire command-line default, including
when its value is empty. This includes unrelated allocator settings such as
`MALLOC_ARENA_MAX` and `glibc.malloc.tcache_count`.

Library construction does not install these thresholds. An embedding application
that owns its process allocator policy can opt in on glibc by calling
`mallopt(M_MMAP_THRESHOLD, 32 * 1024 * 1024)` and then
`mallopt(M_TRIM_THRESHOLD, 16 * 1024 * 1024)` from `<malloc.h>` before starting
any threads, checking that both calls return 1 and respecting its own allocator
overrides. These process-wide settings disable glibc's adaptive threshold growth.
Without an explicit policy, each worker arena can retain a free top chunk of tens
of MiB even after an explicit `malloc_trim`; this can accumulate across workers.

The producer obtains its budget from RAM headroom, cgroup/job limits and the user cap,
with a safety reserve. It forecasts each phase at the actual tower and precision,
then keeps, spills or recomputes optional data. Compact odd-step towers use the same
folded real traces and projected physical dual bases as other compact towers.
Measured memory and I/O can release caches and reduce workers at phase boundaries.
Canonical SICX bytes and certification gates are independent of these decisions.
Precision retries and allocation admission remain active. Memory forecasts are
estimates used for admission and planning, not proved bounds.

## Ray-list selection

`sic-construct --rays-out FILE` projects its completed Group in memory.
The selector uses only the principal-base images at the identity and Smith
axes. Selection storage scales with the class count and Smith rank. The
producer reports start and completion timestamps with `--progress` and
publishes the list after the exact SICX passes its checks. The output uses
[`sic-ray-list/1.0`](schemas/sic-ray-list-1.schema.json). It contains the
dimension, exact SICX root, SHA-256 of the Group source identity, Smith
factors, words and their consecutive labels. The dimension-two source is `closed-form`.
The exact SICX and the list use separate atomic publications.

`sic-decode --finalize --ray-list FILE` requires the list's dimension, Smith
factors and exact root to match its SICX. It computes the anchors from the
same raw enclosures used for normalization. The manual form
`--ray LABEL WORD ANCHOR` accepts an explicit coordinate or `auto` during
finalization. Both produce the same RAYS and BALL records for the same
anchor. The list is construction metadata. Full verification and applicable
independent certificate checks use the finalized SICX.

[`sic-ray-selector`](tools.md#sic-ray-selector) is available with FLINT and
`SIC_BUILD_APPS=ON`. It consumes a native `SICGROUP` version 1 checkpoint from
`sic-construct --group-cache`. The selector accepts the exact Group source
identity of its own build. Supply the checkpoint's lowercase SHA-256 from the
trusted producer or run record via `--sha256` or the
producer-written `.sha256` sidecar.

<!-- shell:template -->
```sh
sic-ray-selector derive --group group.bin --threads 8 > selection.json
sic-ray-selector check --group group.bin --threads 8 --words selection.json
sic-ray-selector check --group group.bin --threads 8 --words rays.json
```

Use `--dimension-two` for dimension two. Its Smith factors are `[2]` and its
sole word is `"0"`.

`derive` chooses the first native mixed-radix element in each ordinary class
with Smith coordinate 0 varying fastest. Its output is one compact JSON
object with integer `smith` factors followed by `words`, each a comma-separated
string of unsigned decimal residues, and a final LF. As in
[SICX RAYS](sicx.md#spec-51a-rays-first-finalization-chunk), output words are sorted
lexicographically by their numeric coordinates. `check` reads this entry
shape, with a 1 MiB limit and optional `map` or `smithSource` metadata. It
validates the Smith factors, ranks, residues, class count and distinctness,
and writes the supplied transversal in the same sorted form. With a bound
producer list, `check` also compares the dimension, Group source and words
against the checkpoint selection. The decoder checks the exact-root binding
against the SICX.

The authenticated Group read streams the entire digest while keeping only
the prefix and the principal-base images of the identity and Smith axes.
The ordinary class computation uses certified ideal arithmetic on those
axes. The checkpoint's producer owns certification of the full Group action.
The exact source identity fixes the class enumeration convention, and
the trusted SHA-256 pin binds the bytes that use those labels.
Physical ray acceptance uses the separate SICX verification gates.
The requested thread count caps worker dispatch, including read-ahead.
Omit `--threads` to use the process CPU allowance, capped at 1024 workers.
The tool uses the process memory allowance with the optional
`--memory-mib` cap.

`--receipt FILE` writes a new
[`sic-ray-selection-receipt/1.0`](schemas/sic-ray-selection-receipt-1.schema.json)
JSON document before writing stdout. It binds `output_sha256` to
the exact bytes prepared for stdout and, for `check`, `words_sha256` to the input bytes.
The receipt is valid only when the command exits with status 0.
The receipt records the same `version` and `source_commit` as `--version`,
along with `mode`, `dimension`, `threads`, `group_source`,
`group_sha256`, `group_bytes`, `group_read_us` (authentication and prefix
extraction), and `selection_us` (selection or check through serialization).
Numbers are decimal strings. In the dimension-two path the Group strings
are empty and the Group counts are zero.

With `sic-construct --progress --group-cache FILE`, a new Group emits
`group closure complete [<elapsed>s]` just before saving. Subtract this
stamp from the existing `validated exact group checkpoint saved` stamp
to measure the save interval, including SHA-256 and sidecar publication.

## Command argument contract

Production tools use the C++20 parser in `sic/support/cli.hpp`. Each tool's
`--help` prints its usage and complete option list to stdout and exits 0.
Malformed arguments print a diagnostic and usage to stderr and exit 2; failed
checks and runtime/input errors exit 1. Successful checks exit 0.

Classified failures name their [`sic::Error` category](#library-c-library-api) in stderr,
for example `[construction]`.

Use separate tokens for option names and values (`--threads 8`). Unknown
options, missing values and duplicate options are errors, except for the
explicitly repeatable ray and exceptional-member options. There are no short
option aliases. `--` ends option parsing for positional filenames starting
with a hyphen; a bare `-` is accepted as a positional argument.
Numeric tokens must be complete, in range and finite, with no leading plus
sign or whitespace. Unsigned values cannot be negative. Worker counts and
byte limits must be positive unless explicitly documented otherwise.

The decoder has no options with optional operands: each `--ray` takes exactly
three operands, `LABEL WORD ANCHOR`. It interprets ray operands using the presence
of `--finalize`, irrespective of that flag's order. Enrolled independent checkers
(`sic-verify-independent`, `sic-certify-crt`, `sic-certify-norm`,
`sic-certify-stabilizer`) retain their own argument handling.

Complete syntax and option defaults are generated in [Command-line tools](tools.md).
