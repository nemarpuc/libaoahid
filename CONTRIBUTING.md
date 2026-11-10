# Contributing

Use the canonical
[issue tracker](https://github.com/nemarpuc/libaoahid/issues) for proposed
changes and submit patches through
[pull requests](https://github.com/nemarpuc/libaoahid/pulls).

## Design Rules

- **Primary sources required.** Every protocol or platform statement must cite
  an official primary source with an immutable revision where applicable,
  section or symbol, and retrieval date. Do not infer Usage IDs, wire values,
  Android classification, or libusb behavior.
- **No inferred product or target policy.** Changes must keep product, target,
  HID descriptor, and parser-policy choices caller-supplied. A required field or
  timeout left zero must be rejected with `AOAHID_ERR_UNSET_FIELD` (or
  `AOAHID_ERR_PARAM`); the `aoahid_*_options_init` functions in `include/aoahid.h`
  fill in one working set, while the library itself never substitutes a fallback
  for zero.
- **Allocation-free hot path.** Report serialization (`src/profiles/state.cpp`),
  transfer-pool acquisition and completion (`src/transport/transport.cpp`), and
  Bulk Channel read/write (`src/transport/channel.cpp`) must remain free of heap
  allocations.
- **Generated language bindings.** `bindings/*/` declarations and
  `tests/abi/layout_oracle.c` are generated from `include/aoahid.h` by
  `python3 tools/bindings/generate.py`. Regenerate them after any header change;
  CI runs `generate.py --check`.

## Code Style and Build

- **Standards and formatting.** The public C header (`include/aoahid.h`) and C
  examples must compile as clean C99/C11 and C++20; the library implementation,
  C++ header (`include/aoahid.hpp`), and tests use C++20 (`-fno-rtti`, no
  exceptions across the C ABI). Format with `clang-format` 14 and keep
  `clang-tidy` 14 clean.
- **Build and test.** Configure and run the deterministic unit and `sim` test
  suites with CMake presets (see [docs/BUILDING.md](docs/BUILDING.md)):
  ```sh
  cmake --preset dev
  cmake --build --preset dev
  ctest --preset dev --output-on-failure
  ```

## Releasing

`python3 tools/release/sync_version.py X.Y.Z` writes the version into every
file that carries it (the list is `tools/release/version_files.py`;
`tools/release/validate_release.py` reads the same list) and then runs
`tools/bindings/generate.py`, so the version constants in the generated
bindings follow (`--no-generate` skips that step). It does not touch
`CHANGELOG.md`: add the dated `## [X.Y.Z] - YYYY-MM-DD` section yourself and
say in it whether the release is hardware-verified, using that word, as the
`[0.1.0]` entry does; `validate_release.py` refuses a section without it. Run
the release workflow by hand with `dry_run` set to `true` to build and check
everything without publishing, then push the tag `vX.Y.Z`. What each job does
is in [docs/RELEASES.md](docs/RELEASES.md#release-triggers-and-dry-runs).

## Submitting Changes

- Add boundary and failure tests for every new validation rule or transport path,
  and run formatting, `tools/check_input_provenance.py`,
  `tools/check_public_contracts.py`, `tools/check_license_headers.py`,
  `tools/check_doc_references.py`, and the complete CTest suite before opening a
  pull request.
- A hardware verification claim requires the four observations listed in
  [docs/PROFILES.md](docs/PROFILES.md#checking-a-profile-on-a-target), recorded
  on one device and build, and goes into the release's `CHANGELOG.md` section
  before [docs/TARGET_MATRIX.md](docs/TARGET_MATRIX.md) lists it. Source review
  or passing fake-libusb tests alone is not hardware evidence.
