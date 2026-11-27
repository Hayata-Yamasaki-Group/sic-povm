# Configuration inputs

This census lists runtime setting reads and public CMake cache inputs.
The source owner defines the interpretation and accepted range. Producer library
settings use an owned `KernelSettings` map when supplied; otherwise they read the
environment. Documented producer settings are validated for syntax and context-free ranges
before construction starts, including settings for stages that the requested dimension does
not use. Presence switches accept any value, numeric settings require a complete finite
number, and storage selectors accept `0` or `1`. Invalid settings report `invalid_argument`.
The construction tool validates inherited producer settings, then clears them and translates its
explicit options into that map. The [test-hook settings](#test-hook-settings) are admitted only by
a hook build and are not production options. Scheduler allocation and cache-location variables are
external platform conventions. Unset entries describe library reads; the construction tool
explicitly selects its worker and storage settings.
See [resource policy](library.md)
and [command help](tools.md) for the public options and precedence.

| Kind | Name | Meaning | Default when unset | Scope | Read owner |
| --- | --- | --- | --- | --- | --- |
| CMake | `FLINT_PREFIX` | Independent checker FLINT search prefix | system search | standalone verifier configure | `cpp/checkers/verifier/CMakeLists.txt` |
| CMake | `SIC_BUILD_APPS` | Build optional exact-pipeline applications | ON | configure | `cpp/CMakeLists.txt` |
| CMake | `SIC_BUILD_BENCH` | Build developer benchmarks | OFF | configure | `cpp/CMakeLists.txt` |
| CMake | `SIC_BUILD_REGRESSION` | Register golden regression suites | ON | test configure | `cpp/CMakeLists.txt` |
| CMake | `SIC_BUILD_TESTS` | Build and register tests | ON | test configure | `cpp/CMakeLists.txt` |
| CMake | `SIC_CURL_SONAME` | Runtime libcurl loader name | libcurl.so.4 | remote access | `cpp/cmake/components/SicxRemote.cmake` |
| CMake | `SIC_DATA_EXACT_JSON_FACTOR` | Exact JSON disk forecast multiplier | 2 | data target | `cpp/cmake/components/Data.cmake` |
| CMake | `SIC_DATA_JOBS` | Concurrent data downloads | 4 | data target | `cpp/cmake/components/Data.cmake` |
| CMake | `SIC_DATA_MULTIPLETS` | Multiplets requested by explicit data targets | empty; no target workload | data target | `cpp/cmake/components/Data.cmake` |
| CMake | `SIC_DATA_ROOT` | Manifest/data tree | empty; required when selecting multiplets | data target | `cpp/cmake/components/Data.cmake` |
| CMake | `SIC_DATA_SOURCE` | Local mirror or release base URL | manifest release | data target | `cpp/cmake/components/Data.cmake` |
| CMake | `SIC_DECODER_SOURCE_COMMIT` | Decoder provenance identity override | configured source identity | configure | `cpp/cmake/components/Decode.cmake` |
| CMake | `SIC_ENABLE_FLINT` | Enable exact arithmetic components | OFF | configure | `cpp/CMakeLists.txt` |
| CMake | `SIC_ENABLE_HDF5` | Enable HDF5 numeric I/O | AUTO | configure | `cpp/cmake/components/Core.cmake` |
| CMake | `SIC_ENABLE_LTO` | Enable supported optimized-build IPO | ON | configure | `cpp/cmake/components/LinkTimeOptimization.cmake` |
| CMake | `SIC_ENABLE_REMOTE` | Enable HTTP range access | ON when libcurl is found | configure | `cpp/cmake/components/SicxRemote.cmake` |
| CMake | `SIC_ENABLE_TEST_HOOKS` | Enable private fault/test instrumentation | OFF | test configure; installation refused | `cpp/CMakeLists.txt` |
| CMake | `SIC_SICX_GP_D16` | Enable long independent PARI/GP check | OFF | test configure | `cpp/tests/sicx/tests.cmake` |
| CMake | `SIC_SOURCE_COMMIT` | Producer provenance identity override | configured source identity | configure | `cpp/CMakeLists.txt` |
| CMake | `SIC_TEST_WORKERS` | Explicit native parallel-product and nested-build workers | empty; max(3, process allowance) | test runtime | `cpp/cmake/checks/TestLayers.cmake` |
| environment | `GLIBC_TUNABLES` | Allocator override; a `glibc.malloc.` entry disables construction heap settings | inherited | glibc construction tool | `cpp/apps/construct/main.cpp` |
| environment prefix | `MALLOC_*` | Any variable with this prefix, including an empty value, disables construction heap settings | inherited | glibc construction tool | `cpp/apps/construct/main.cpp` |
| environment | `HOME` | User home for fallback cache location | platform supplied | platform | `cpp/src/access/range_cache.cpp` |
| environment | `SIC_EXACT_JSON` | Exact JSON input for the independent PARI/GP checker | unset | PARI/GP checker | `cpp/checkers/gp/sicx_exact_verify.gp` |
| environment | `SIC_RAY` | Ray selected by the PARI/GP checker | all rays | PARI/GP checker | `cpp/checkers/gp/sicx_exact_verify.gp` |
| environment | `SIC_SHIFTS` | Dyadic shift vector for the PARI/GP checker | all shifts from 0 through d/2 | PARI/GP checker | `cpp/checkers/gp/sicx_exact_verify.gp` |
| environment | `SIC_ANALYTIC_CACHE` | Analytic cache path | unset | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_CHECK_ENTRY_GROUPS` | Cross-check entry group calculations | disabled | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_CHECK_SUPPORT_PERIODS` | Cross-check support periods | disabled | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_COMPACT_WORK` | Use compact work storage | follows phased storage; disabled | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_COMPLETED_CLASS_CACHE` | Directory for completed class cache | unset | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_D2_RADICALS` | Dimension-two radical input path | embedded radical record | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_DIRECT_ETA_CHECK` | Cross-check eta entries directly | disabled | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_ENTRY_ARB` | Use Arb entry evaluation; any present value, including 0, enables it | unset; construction refused | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_GENERATOR_BLOCK` | Generator evaluation block size | 262144 | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_GHOST_DEBUG` | Trace reconstruction | disabled | diagnostics | `cpp/src/exact/certification/reconstruction/ghost.cpp` |
| environment | `SIC_GHOST_TRACE_ITERATIONS` | Trace reconstruction in detail | disabled | diagnostics | `cpp/src/exact/certification/reconstruction/ghost.cpp` |
| environment | `SIC_GROUP_CACHE` | Group cache path | unset | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_GROUP_RESIDENT_ENTRIES` | Retain representative group entries in memory | direct path: field degree >= 1048576, or entry-group cross-checks | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_KRAWCZYK_FIRST_BITS` | Initial Krawczyk certificate matrix precision | adaptive | exact library | `cpp/src/exact/certification/reconstruction/ghost.cpp` |
| environment | `SIC_MEMORY_MB` | Explicit library memory budget in MiB | process/cgroup/job allowance | library | `cpp/src/support/resources.cpp` |
| environment | `SIC_NEWTON_MATRIX_BITS` | Override Newton matrix precision | adaptive | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_NEWTON_MEMORY_AUDIT` | Print Newton memory audit | disabled | diagnostics | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_NEWTON_MEMORY_OWNERS` | Audit per-owner Newton memory | disabled | diagnostics | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_NEWTON_PHASE_MEMORY` | Trace phase memory use | disabled | diagnostics | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_ORDER` | Producer base order | standard | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_PARALLEL_GRAIN` | Dynamic parallel-loop chunk size override | max(1, min(1024, (work / workers) / 16)) | exact library | `cpp/src/exact/runtime/parallel.tpp` |
| environment | `SIC_OUT` | Producer output path | unset; output path required | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_FIRST_PASS_BITS` | Initial pass precision | 128 bits | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_PATH_DEBUG` | Trace selected paths | disabled | diagnostics | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_PHASED_STORAGE` | Use phased storage | disabled | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_PROGRESS` | Print progress | disabled | diagnostics | `cpp/src/exact/storage/ghost_class_backing.cpp` |
| environment | `SIC_REP_BAND_BITS` | Log2 representative band count request | 0; planner may increase | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_REQUIRE_COMPLETED_CACHE` | Require cached completed classes | disabled | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_ROOT_BACKING` | Use root backing storage | enabled when tables stream | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_SAVE_STEP_SELECTION` | Step-selection record path | unset | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_SEED_A` | Seed precision growth multiplier | 2.0 | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_SELECTION_THREADS` | Newton per-class kernel worker ceiling | shared worker allowance | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_SICX_RESUME` | Resume SICX construction | disabled | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_SPOOL_LATTICE` | Use lattice coordinates for spooled leaves | disabled | exact library | `cpp/src/exact/storage/leaf_store.cpp` |
| environment | `SIC_STEP_DEBUG` | Trace step selection | disabled | diagnostics | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_STEP_SELECTION` | Step-selection input path | unset | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_STOP_FILE` | Cancellation sentinel path | unset | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_STREAM_ACTIONS` | Stream action data | follows phased storage; disabled | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_STREAM_REPS` | Stream representatives | follows phased storage; disabled | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_STREAM_STEPS` | Stream step data | follows action streaming; disabled | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_STREAM_TABLES` | Stream exact tables | enabled on the phased path | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_TDEBUG` | Trace tower calculations | disabled | diagnostics | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_THREADS` | Explicit producer library worker count | process CPU allowance | producer library | `cpp/src/construct/model/options.cpp` |
| environment | `SLURM_CPUS_PER_TASK` | Job CPU ceiling | unset; affinity/cgroup discovery | platform | `cpp/src/support/resources/platform_snapshot.cpp` |
| environment | `SLURM_MEM_PER_CPU` | Job memory per admitted logical CPU | unset | platform | `cpp/src/support/resources/platform_snapshot.cpp` |
| environment | `SLURM_MEM_PER_NODE` | Job total memory ceiling | unset | platform | `cpp/src/support/resources/platform_snapshot.cpp` |
| environment | `XDG_CACHE_HOME` | Base directory for the remote byte cache | HOME/.cache fallback | platform | `cpp/src/access/range_cache.cpp` |

## Test-hook settings

These settings are read only by builds configured with `SIC_ENABLE_TEST_HOOKS=ON`, which the
installation refuses. They inject faults and force rare paths in the test suite and are not
production inputs.

| Kind | Name | Meaning | Default when unset | Scope | Read owner |
| --- | --- | --- | --- | --- | --- |
| environment | `SIC_FAULT_KILL_AFTER_CHUNKS` | Abort after a chosen chunk count | unset; inactive | hook build only | `cpp/src/sicx/container/stream_writer.cpp` |
| environment | `SIC_FAULT_KILL_AT_BYTE` | Abort at a byte offset | unset; inactive | hook build only | `cpp/src/sicx/container/stream_writer.cpp` |
| environment | `SIC_TEST_ABORT_AFTER_PARTS` | Abort after part writes | unset; inactive | hook build only | `cpp/src/transport/parts.cpp` |
| environment | `SIC_CLASS_THREADS` | Override class worker count | unset; inactive | hook build only | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_DEV_KEEP_ALL` | Retain all developer intermediates | unset; inactive | hook build only | `cpp/src/construct/runtime/command_line.cpp` |
| environment | `SIC_DEV_MEMORY_MB` | Developer memory override | unset; inactive | hook build only | `cpp/src/construct/runtime/command_line.cpp` |
| environment | `SIC_DEV_SPILL` | Force developer spill policy | unset; inactive | hook build only | `cpp/src/construct/runtime/command_line.cpp` |
| environment | `SIC_DIRECT_ETA_TEST_FAULT` | Inject eta disagreement | unset; inactive | hook build only | `cpp/src/construct/stages/entries.cpp` |
| environment | `SIC_TEST_CEIL1_FORCE_PRECISION_REFUSAL` | Force precision admission refusal | unset; inactive | hook build only | `cpp/src/exact/arithmetic/qk.cpp` |
| environment | `SIC_TEST_FAIL_SIC_CHECK` | Force final SIC check failure | unset; inactive | hook build only | `cpp/src/construct/stages/sic_check.cpp` |
| environment | `SIC_TEST_FINALIZATION_BATCH_RAYS` | Inject a resource refusal above this finalization batch size | unset; inactive | hook build only | `cpp/apps/decode/command.cpp` |
| environment | `SIC_TEST_FINALIZATION_PACK_RAYS` | Limit simultaneously packed finalization rays | unset; inactive | hook build only | `cpp/apps/decode/command.cpp` |
| environment | `SIC_TEST_FINALIZATION_REFUSE_FIRST_BATCH` | Inject a precision refusal on the first shared finalization pass | unset; inactive | hook build only | `cpp/src/decode/scheduling.cpp` |
| environment | `SIC_TEST_FINALIZATION_REPLAY_FIRST_OUTPUT` | Replay the first finalization coordinate at the next precision | unset; inactive | hook build only | `cpp/apps/decode/command.cpp` |
| environment | `SIC_TEST_INSPECT_EXACT_RENAME_FAIL` | Inject exact-JSON rename failure | unset; inactive | hook build only | `cpp/apps/inspect/command.cpp` |
| environment | `SIC_TEST_KILL_AFTER_WRITE` | Abort after output writes | unset; inactive | hook build only | `cpp/src/construct/output/output.cpp` |
| environment | `SIC_TEST_NEWTON_DEFER_CLASS` | Force class deferral | unset; inactive | hook build only | `cpp/src/construct/stages/table/newton_table.cpp` |
| environment | `SIC_TEST_NEWTON_DEFER_ENTRY_AFTER_HINT` | Force entry deferral after a hint | unset; inactive | hook build only | `cpp/src/construct/model/options.cpp` |
| environment | `SIC_TEST_PRECISION_RETRY` | Force precision retry | unset; inactive | hook build only | `cpp/src/construct/stages/final_pass.cpp` |
| environment | `SIC_TEST_REP_BAND_BITS` | Force log2 representative band count | unset; inactive | hook build only | `cpp/src/construct/runtime/resources/final_memory.cpp` |
