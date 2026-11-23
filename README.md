# SIC fiducials in power-of-two dimensions

C++20 tools construct exact Weyl–Heisenberg SIC fiducials for `d = 2^s`, decode certified
enclosures and export numerical vectors. SICX 1.0 also specifies general-dimension containers;
a general-dimension producer and a compact-output complexity theorem remain research goals.

Read [installation](docs/install.md) for dependencies and a first run,
[tools](docs/tools.md) for command syntax, [library](docs/library.md) for C++
embedding, [data](docs/data.md) and [SICX](docs/sicx.md) for formats,
[verification](docs/verification.md) for claim scope, and
[reproduction](docs/reproduce.md) for the from-scratch workflow and per-multiplet recipes.
The source data exceptions are the d2 closed form and the d8 Hoggar lines.
Each release run builds its WH catalogue and stabilizer tables from the rays it computes.

With the exact tools installed:

<!-- shell:template -->
```sh
sic-construct 3 --out d8.sicx --order standard --threads 2
sic-verify d8.sicx --rescaling
```

The recorded collection has 70 rays: 36 proved for their identified inputs and 34 conditional on
the stated construction premises. Publication requires checked per-ray root bindings.
d1024 standard and d2048 results are not published. A small numerical residual or a container
hash is not an exact SIC proof. See [status and evidence](docs/verification.md) and
[available data](docs/data.md#availability-and-storage).

See [Contributing](CONTRIBUTING.md). Copyright (C) 2026 Hayata Yamasaki and contributors.
The software (including scripts and the test fixtures under `cpp/tests/`), documentation, formats
and schemas are licensed under [AGPL-3.0-or-later](LICENSE), except where otherwise noted. The
datasets in `data/` and the datasets distributed as GitHub Release assets are made available under
the [Open Database License (ODbL) v1.0](LICENSE-ODbL) (https://opendatacommons.org/licenses/odbl/1-0/);
any rights in their individual contents are licensed under the
[Database Contents License (DbCL) v1.0](LICENSE-DbCL) (https://opendatacommons.org/licenses/dbcl/1-0/).
