# Releases and Licensing

This page describes how `libaoahid` releases are built, what each release
asset contains, the platform runtime baselines, the language-binding packages,
and the license files shipped in every binary archive.

For building from source or consuming the CMake and `pkg-config` files, see
[BUILDING.md](BUILDING.md). For internal release-script gates, see
[INTERNALS.md](INTERNALS.md#version-and-release).

## Release triggers and dry runs {#release-triggers-and-dry-runs}

`.github/workflows/release.yml` has these jobs: `validate`, `build` (one per
target), `binding-packages`, `assemble` (after `validate` and `build`),
`publish` (after `validate`, `assemble` and `binding-packages`), and
`publish-pypi` and `publish-nuget` (after `publish`). It runs in two modes:

- **Tag push (`v*`).** `validate` checks that every version-bearing file in
  the repository matches the tag (`tools/release/version_files.py`) and that
  `CHANGELOG.md` has a dated section for that version containing the word
  `hardware-verified` (`tools/release/validate_release.py`). The other jobs
  build and test every target, assemble the release, and `publish` creates the
  GitHub Release. `publish-pypi` and `publish-nuget` run only when the
  repository variable `AOAHID_PUBLISH_BINDINGS` is `true`.
- **Manual dry run (`workflow_dispatch`, `dry_run: true`; `false` is
  refused).** Takes the version from the newest dated section in
  `CHANGELOG.md`, runs `validate`, `build`, `binding-packages` and `assemble`
  with every binary and SPDX check, and uploads the assembled set as the
  `release-dist` workflow artifact. `publish`, `publish-pypi` and
  `publish-nuget` do not run.

## The 10 release assets

Every GitHub Release has **exactly 10 uploaded assets**:

| Asset | Description |
| --- | --- |
| `libaoahid-X.Y.Z-linux-x86_64-ubuntu22.04.tar.gz` | Linux x86-64 shared + static binary archive (`GLIBC_2.35` baseline) |
| `libaoahid-X.Y.Z-linux-aarch64-ubuntu22.04.tar.gz` | Linux AArch64 shared + static binary archive (`GLIBC_2.35` baseline) |
| `libaoahid-X.Y.Z-macos-arm64.tar.gz` | macOS Apple Silicon (`arm64`) shared + static binary archive (macOS 11.0 baseline) |
| `libaoahid-X.Y.Z-macos-x86_64.tar.gz` | macOS Intel (`x86_64`) shared + static binary archive (macOS 11.0 baseline) |
| `libaoahid-X.Y.Z-windows-x86_64.zip` | Windows x64 shared + static binary archive (MSVC) |
| `libaoahid-X.Y.Z-windows-arm64.zip` | Windows ARM64 shared + static binary archive (MSVC) |
| `libaoahid-X.Y.Z.spdx.json` | Release-wide SPDX 2.3 SBOM covering the source archive and all six binary archives |
| `libaoahid-X.Y.Z-source.tar.gz` | Deterministic source archive matching the tagged commit |
| `release-manifest.json` | Machine-readable manifest of archives, SHA-256 digests, target architectures, and glibc baselines |
| `SHA256SUMS` | SHA-256 checksums for the archives, SBOM, and manifest |

No bare `.so`, `.dylib`, or `.dll` files and no separate `-shared`, `-static`,
or `-runtime` bundles are uploaded. Shipping one self-contained archive per
platform guarantees that the headers, CMake targets, `pkg-config` metadata,
`libusb` runtime, and LGPL-2.1 license and source material stay together.

## Binary archive layout {#binary-archive-layout}

Each platform archive unpacks to a single top-level directory named after the
archive stem (`libaoahid-X.Y.Z-<platform>-<arch>[-ubuntu22.04]/`):

| Path inside archive | Contents |
| --- | --- |
| `include/aoahid.h`, `include/aoahid.hpp` | Public C99/C++20 headers |
| `include/libusb-1.0/libusb.h` | Public header of the bundled `libusb 1.0.30` |
| `lib/` (and `bin/` on Windows) | Shared library (`libaoahid.so.0.1.0` + symlinks, `libaoahid.0.1.0.dylib` + symlinks, or `bin/aoahid.dll` + `lib/aoahid.lib`), static archive (`libaoahid_static.a` or `aoahid_static.lib`), and the bundled `libusb-1.0` shared runtime and link library (`libusb-1.0.so*`, `libusb-1.0.0.dylib`, or `bin/libusb-1.0.dll` + `lib/usb-1.0.lib`) |
| `lib/cmake/aoahid/` | `aoahid-config.cmake`, `aoahid-config-version.cmake`, `aoahid-shared-targets.cmake`, `aoahid-static-targets.cmake` |
| `lib/pkgconfig/` | Relocatable `aoahid.pc`, `aoahid-static.pc`, and `libusb-1.0.pc` |
| `share/doc/libaoahid/examples/` | C and C++ example source files and standalone `CMakeLists.txt` |
| `share/doc/libaoahid/` | `LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md`, `build-metadata.json`, `sbom.spdx.json` (plus `glibc-requirements.json` on Linux) |
| `share/doc/libaoahid/third-party/` | `libusb-copyright` (LGPL-2.1-or-later), `source/libusb-1.0.30.tar.bz2`, and `vcpkg-LICENSE.txt` (Windows archives) |

## Platform baselines

- **Linux (`x86_64`, `aarch64`).** Built on Ubuntu 22.04 against `glibc 2.35`.
  `tools/release/glibc_versions.py` verifies that no exported or imported
  symbol in `libaoahid.so` or the bundled `libusb-1.0.so` requires a symbol
  version newer than `GLIBC_2.35`, and records the result in
  `share/doc/libaoahid/glibc-requirements.json`. `RUNPATH` is set to `$ORIGIN`
  so `libaoahid.so` finds the bundled `libusb-1.0.so.0` in the same `lib/`
  directory without `LD_LIBRARY_PATH` changes. The C++ standard library
  (`-static-libstdc++ -static-libgcc`) is linked statically into
  `libaoahid.so` (`CMakeLists.txt`).
- **macOS (`arm64`, `x86_64`).** Built with `MACOSX_DEPLOYMENT_TARGET=11.0`,
  `LC_RPATH` set to `@loader_path`, and install names `@rpath/libaoahid.0.dylib`
  and `@rpath/libusb-1.0.0.dylib`. Every `.dylib` is ad-hoc signed
  (`codesign --force --sign -`); the binaries are
  not notarized with an Apple Developer ID.
- **Windows (`x86_64`, `arm64`).** Built with MSVC. `aoahid.dll` and `libusb-1.0.dll` are placed in `bin/`; import
  libraries (`aoahid.lib`, `usb-1.0.lib`) and the static library
  (`aoahid_static.lib`) are in `lib/`. CI verifies that `aoahid_static.lib`
  contains no `/EXPORT:aoahid_` linker directives so linking it into an
  executable or another DLL does not re-export `libaoahid` symbols.

## Language binding packages

The Python (`bindings/python`), C# (`bindings/csharp`), and Rust
(`bindings/rust`) bindings are generated from `include/aoahid.h` by
`tools/bindings/generate.py`:

- **PyPI (`aoahid`) and NuGet (`AoaHid`).** Contain the pure language
  declarations (`ctypes` and P/Invoke) only; they do not bundle native
  binaries. Point them at `libaoahid.so`, `libaoahid.dylib`, or `aoahid.dll`
  from a release archive or source build: in Python pass its path to
  `aoahid.load(path)`; in .NET put it on the native library search path.
- **Rust (`aoahid-sys` and `aoahid`).** Built from the repository workspace
  against `AOAHID_LIB_DIR`; crates are not published to `crates.io` yet.

## Licenses

`libaoahid` is released under the MIT License ([LICENSE](../LICENSE)). Every
tracked C, C++, Python, C#, Rust, and shell source file carries an
`SPDX-License-Identifier: MIT` header, verified by
`tools/check_license_headers.py`.

Both the shared library (`libaoahid`) and the static archive
(`libaoahid_static`) link against `libusb-1.0` dynamically (`libusb-1.0.so.0`,
`libusb-1.0.0.dylib`, or `libusb-1.0.dll`); `libusb` object code is never
merged into `libaoahid_static`. Every binary archive includes the complete
license and source material required for the bundled `libusb 1.0.30` binary:

| Component | License | Files in repository / binary archive |
| --- | --- | --- |
| `libaoahid` (library, headers, bindings, examples, tools) | MIT | `LICENSE`, `NOTICE`, `share/doc/libaoahid/LICENSE`, `share/doc/libaoahid/NOTICE` |
| `libusb 1.0.30` (bundled shared runtime, header, link library) | LGPL-2.1-or-later | `THIRD_PARTY_NOTICES.md`, `share/doc/libaoahid/THIRD_PARTY_NOTICES.md`, `share/doc/libaoahid/third-party/libusb-copyright`, `share/doc/libaoahid/third-party/source/libusb-1.0.30.tar.bz2`, and `libusb_build` in `share/doc/libaoahid/build-metadata.json` |
| `vcpkg` port recipe used during Windows dependency build | MIT | `share/doc/libaoahid/third-party/vcpkg-LICENSE.txt` (Windows archives only) |
| Release and archive SBOMs | SPDX 2.3 | `libaoahid-X.Y.Z.spdx.json` (release asset) and `share/doc/libaoahid/sbom.spdx.json` (inside each binary archive) |
