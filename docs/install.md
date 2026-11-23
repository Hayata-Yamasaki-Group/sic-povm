# Installation and quick start

CMake 3.20 or newer and a C++20 compiler are required. The default test-enabled build also requires
Node.js 20 or newer, Bash 4 or newer, and `nm` or `llvm-nm`. A build configured with
`-DSIC_BUILD_TESTS=OFF` needs only the build dependencies.

Only the C++ formatting test needs clang-format 14 (`clang-format-14` on Ubuntu 24.04).
CTest reports that test as skipped when a working version 14 is unavailable.
Exact tests require OpenSSL Crypto development files. Install `pari-gp` to run the GP controls and
libcurl development files to run the HTTP Range controls.
The plain configuration builds the numerical core and tools without FLINT:

<!-- shell:template -->
```sh
cmake -S cpp -B build-plain -DCMAKE_BUILD_TYPE=Release -DSIC_ENABLE_FLINT=OFF
cmake --build build-plain -j2
ctest --test-dir build-plain --output-on-failure
cmake --install build-plain --prefix "$PWD/install-plain"
```

For exact construction, SICX, decoding and certification, install FLINT 3 with Arb, GMP and MPFR.
Install `m4` when building FLINT from its release tarball. Configure a separate build:

<!-- shell:template -->
```sh
cmake -S cpp -B build-exact -DCMAKE_BUILD_TYPE=Release -DSIC_ENABLE_FLINT=ON
cmake --build build-exact -j2
ctest --test-dir build-exact --output-on-failure
cmake --install build-exact --prefix "$PWD/install-exact"
```

The source archive includes its source commit, so it configures without Git metadata.
`-DSIC_SOURCE_COMMIT=<full-commit>` and `-DSIC_DECODER_SOURCE_COMMIT=<full-commit>` override the
recorded identities when needed.

For dependencies in a nonstandard location, set `CMAKE_PREFIX_PATH` when configuring. HDF5 is
detected automatically when available. Installed files carry no absolute source, build or dependency
paths except dependency RUNPATH entries. Put the installation's `bin` directory on `PATH`; see
[Command-line tools](tools.md) for available commands.

An independent CMake project can consume a relocated installation with:

```cmake
find_package(sic CONFIG REQUIRED COMPONENTS core)
target_link_libraries(my_application PRIVATE sic::core)
```

Set the consumer's `CMAKE_PREFIX_PATH` to the installation prefix and any separate dependency
prefixes. See the [library API](library.md) for components and ownership rules. Tests are enabled by
default. Test hooks require a separate build with `SIC_ENABLE_TEST_HOOKS=ON`; installation from that
build is refused. See [Contributing](../CONTRIBUTING.md) for validation.

The installation includes the d2 radical record and the Hoggar files under
`${CMAKE_INSTALL_DATADIR}/sic/data`. Identification and catalogue-mode stabilizer
search require an explicit `--catalogue FILE`. A JSON catalogue resolves its members
relative to its own directory. For TSV paths, `--repo DIR` selects the base directory.
Test fixtures are excluded from installation.

Installed ELF RUNPATH entries may contain external dependency prefixes outside
the system linker path: the build uses `CMAKE_INSTALL_RPATH_USE_LINK_PATH`.
The installation path scan exempts dependency paths only inside complete RUNPATH
strings; it rejects source and build paths, and dependency paths elsewhere.

The [installation inventory](install_contract.json) is the checked public
contract for each configuration: executable names, exported component targets
and every installed header. Setting `SIC_BUILD_APPS=OFF` excludes the tools listed
in `optional_apps`; the remaining exact tools and components remain available.
The relocated consumer test checks exact membership
and runs each installed tool's `--help` and `--version`. The instrumented hook
configuration instead tests the mandatory installation refusal.

See [test selection and resource settings](../CONTRIBUTING.md#test-selection-and-independence)
for test worker counts and oversubscribed controls.
