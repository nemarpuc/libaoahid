# Building and Installing

This page covers compiler and dependency requirements, CMake options and
presets, running the unit and `sim` test suites, sanitizers, coverage, fuzzing,
and consuming an installed `libaoahid` package from CMake, `pkg-config`, or
standalone compiler commands.

For a first run on a physical Android phone (cables, udev rules, Windows
drivers, and human-observable verification programs), see
[QUICKSTART.md](QUICKSTART.md). For prebuilt binary archives, see
[RELEASES.md](RELEASES.md).

## Requirements

- **C++20 compiler and C99 compiler** (GCC, Clang, or MSVC). The library
  implementation is C++20; the public header `include/aoahid.h` and C examples
  compile as C99/C17 and C++20.
- **CMake 3.20 or later.**
- **libusb 1.0.30 or later** in the `1.0` API line (for real-USB builds where
  `AOAHID_USE_FAKE_LIBUSB=OFF`). CMake checks the headers it finds
  (`LIBUSB_API_VERSION` at least `0x0100010C`, `cmake/aoahid-check-libusb.cmake`),
  and `aoahid_context_create` checks the runtime: it reads `libusb_get_version`
  and refuses a library outside the `1.0` line or older than `1.0.30` with
  `AOAHID_ERR_UNSUPPORTED` (`supported_libusb_runtime` in
  `src/transport/discovery.cpp`). [QUICKSTART.md](QUICKSTART.md#1-install-the-build-tools-and-libusb-1030)
  shows how to get 1.0.30 where the distribution packages an older one.
- **Platform USB access.** On Linux, install `udev/51-aoahid.rules` (see
  [PORTING.md](PORTING.md#linux)). On Windows, the phone must use a
  WinUSB-compatible driver (see [PORTING.md](PORTING.md#windows)).

## CMake options

Every project-specific CMake option is declared in `CMakeLists.txt`:

| Option | Default | Effect |
| --- | --- | --- |
| `AOAHID_WERROR` | `ON` | Treat compiler warnings as errors (`/WX` on MSVC, `-Werror` on GCC and Clang). Turn `OFF` when testing with a newer compiler that adds warnings not yet in CI. |
| `AOAHID_BUILD_SHARED` | `ON` | Build the shared library (`aoahid` target: `libaoahid.so`, `libaoahid.dylib`, or `aoahid.dll`). |
| `AOAHID_BUILD_STATIC` | `ON` | Build the static archive (`aoahid_static` target: `libaoahid_static.a` or `aoahid_static.lib`). At least one of `AOAHID_BUILD_SHARED` or `AOAHID_BUILD_STATIC` must be `ON`. |
| `AOAHID_BUILD_TESTS` | `OFF` | Build the unit, `sim`, and ABI verification executables and register CTest targets. Requires `AOAHID_USE_FAKE_LIBUSB=ON`. |
| `AOAHID_BUILD_EXAMPLES` | `OFF` | Build the C and C++ example programs under `examples/` (into `<build>/examples/`; on Windows next to `aoahid.dll`). |
| `AOAHID_BUILD_FUZZ` | `OFF` | Build the Clang libFuzzer target `aoahid_fuzz_descriptor`. Requires Clang and cannot be combined with `AOAHID_COVERAGE`. |
| `AOAHID_SANITIZE` | `OFF` | Enable AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined -fno-omit-frame-pointer`). GCC and Clang only: with MSVC the option adds no flag. Mutually exclusive with `AOAHID_TSAN` and `AOAHID_COVERAGE`. |
| `AOAHID_TSAN` | `OFF` | Enable ThreadSanitizer (`-fsanitize=thread -fno-omit-frame-pointer`). Linux only, with GCC 12 or later or Clang. Mutually exclusive with `AOAHID_SANITIZE`, `AOAHID_COVERAGE`, and `AOAHID_BUILD_FUZZ`. |
| `AOAHID_CHECK_SERIALIZATION` | `OFF` | Abort the process with the offending and active call names if two threads enter the same `aoahid_context` serialization domain concurrently (see [ARCHITECTURE.md](ARCHITECTURE.md#checking-the-serialization-rule)). |
| `AOAHID_USE_FAKE_LIBUSB` | `OFF` | Link against the deterministic in-memory `tests/fake_libusb` backend instead of the system `libusb-1.0`. `AOAHID_BUILD_TESTS` requires it. |
| `AOAHID_COVERAGE` | `OFF` | Instrument targets for `gcov` (`--coverage -O0 -fprofile-update=atomic` on GCC/Clang). Mutually exclusive with `AOAHID_SANITIZE`, `AOAHID_TSAN`, and `AOAHID_BUILD_FUZZ`. |

## CMake presets

`CMakePresets.json` defines four configure, build, and test presets:

| Preset | Build directory | Key flags | Purpose |
| --- | --- | --- | --- |
| `dev` | `build/dev` | `Debug`, `-DAOAHID_BUILD_TESTS=ON`, `-DAOAHID_BUILD_EXAMPLES=ON`, `-DAOAHID_USE_FAKE_LIBUSB=ON`, `-DAOAHID_CHECK_SERIALIZATION=ON` | Fast local development and full unit + `sim` test suite with no USB hardware. |
| `release` | `build/release` | `Release`, `-DAOAHID_BUILD_TESTS=OFF` (the other options keep their defaults: real libusb, no examples) | Shared and static libraries against the system `libusb-1.0`. |
| `tsan` | `build/tsan` | `RelWithDebInfo`, `-DAOAHID_BUILD_TESTS=ON`, `-DAOAHID_BUILD_EXAMPLES=ON`, `-DAOAHID_USE_FAKE_LIBUSB=ON`, `-DAOAHID_TSAN=ON` | ThreadSanitizer verification of internal-thread mode, Channels, and teardown. |
| `coverage` | `build/coverage` | `Debug`, `-DAOAHID_USE_FAKE_LIBUSB=ON`, `-DAOAHID_CHECK_SERIALIZATION=ON`, `-DAOAHID_COVERAGE=ON`, `-DAOAHID_BUILD_EXAMPLES=OFF` | Line and branch coverage measurement over `src/` with `gcovr`. |

### Release build against system libusb

```sh
cmake --preset release
cmake --build --preset release
sudo cmake --install build/release --prefix /usr/local
```

Or without presets:

```sh
cmake -S . -B build/release \
  -DCMAKE_BUILD_TYPE=Release \
  -DAOAHID_USE_FAKE_LIBUSB=OFF
cmake --build build/release
```

### Development build and tests (fake libusb)

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

To run individual suites in `aoahid_tests` (`item_writer`, `profiles`,
`spec_options`, `raw_descriptor`, `transport`, `identity`, `api_arguments`,
`report_state`, `transport_failures`):

```sh
./build/dev/aoahid_tests
./build/dev/aoahid_tests profiles spec_options
```

## Sanitizers, ThreadSanitizer, coverage, and fuzzing

### AddressSanitizer and UndefinedBehaviorSanitizer

```sh
cmake -S . -B build/asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAOAHID_BUILD_TESTS=ON \
  -DAOAHID_USE_FAKE_LIBUSB=ON \
  -DAOAHID_CHECK_SERIALIZATION=ON \
  -DAOAHID_SANITIZE=ON
cmake --build build/asan
ctest --test-dir build/asan --output-on-failure
```

### ThreadSanitizer

```sh
cmake --preset tsan
cmake --build --preset tsan
TSAN_OPTIONS="halt_on_error=1" ctest --preset tsan --output-on-failure
```

See [PORTING.md](PORTING.md#threadsanitizer) for the Linux `vm.mmap_rnd_bits`
workaround when TSan fails at startup under 32-bit ASLR entropy.

### Coverage (`AOAHID_COVERAGE`)

```sh
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage --output-on-failure
gcovr --root . --object-directory build/coverage --filter 'src/' \
  --exclude-throw-branches --exclude-unreachable-branches \
  --print-summary --fail-under-line 87 --fail-under-branch 75
```

See [VERIFICATION.md](VERIFICATION.md#coverage) and
[tests/README.md](../tests/README.md#tests) for what the coverage gate checks and
what is deliberately left out.

### Fuzzing (`AOAHID_BUILD_FUZZ`)

With Clang:

```sh
cmake -S . -B build/fuzz -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DAOAHID_USE_FAKE_LIBUSB=ON \
  -DAOAHID_BUILD_SHARED=OFF \
  -DAOAHID_BUILD_STATIC=ON \
  -DAOAHID_BUILD_TESTS=OFF \
  -DAOAHID_BUILD_EXAMPLES=OFF \
  -DAOAHID_BUILD_FUZZ=ON \
  -DAOAHID_SANITIZE=ON
cmake --build build/fuzz --target aoahid_fuzz_descriptor
./build/fuzz/aoahid_fuzz_descriptor -runs=10000 -max_len=4096 -timeout=5
```

## Using the installed package

### CMake (`find_package`)

An installed tree or unpacked release archive provides the `aoahid` CMake
package under `lib/cmake/aoahid/` with two optional components, `shared` and
`static`:

```cmake
find_package(aoahid 0.1 CONFIG REQUIRED COMPONENTS shared)
target_link_libraries(your_c_target PRIVATE aoahid::aoahid)
target_link_libraries(your_cpp_target PRIVATE aoahid::cpp)
```

For static linking of `libaoahid` (while still linking `libusb-1.0`
dynamically; see [RELEASES.md](RELEASES.md#licenses)):

```cmake
find_package(aoahid 0.1 CONFIG REQUIRED COMPONENTS static)
target_link_libraries(your_c_target PRIVATE aoahid::aoahid_static)
target_link_libraries(your_cpp_target PRIVATE aoahid::cpp_static)
```

- If no `COMPONENTS` are listed, `aoahid-config.cmake` loads `shared` when
  present and falls back to `static`.
- While the major version is `0`, `aoahid-config-version.cmake` requires the
  same `0.minor` line (`0.1.x` satisfies `0.1`, not `0.2`).
- Point CMake at an unpacked release archive with
  `-DCMAKE_PREFIX_PATH=/path/to/unpacked/archive`.

### `pkg-config`

Both `aoahid.pc` (shared) and `aoahid-static.pc` (static) are installed under
`lib/pkgconfig/` and use prefix-relative paths so an unpacked archive works from
any directory:

```sh
# Shared library
cc -std=c17 your_app.c \
  $(PKG_CONFIG_PATH=/path/to/archive/lib/pkgconfig pkg-config --cflags --libs aoahid) \
  -o your_app

# Static library (links libaoahid_static.a and dynamic libusb-1.0)
cc -std=c17 your_app.c \
  $(PKG_CONFIG_PATH=/path/to/archive/lib/pkgconfig pkg-config --static --cflags --libs aoahid-static) \
  -o your_app
```

### Building the bundled examples standalone

`examples/CMakeLists.txt` can be configured as a top-level project against an
installed package or unpacked release archive:

```sh
cmake -S examples -B build/examples \
  -DCMAKE_PREFIX_PATH=/path/to/unpacked/archive
cmake --build build/examples
```

### `vcpkg`

There is no `libaoahid` port in the public `vcpkg` registry. The repository's
root `vcpkg.json` manifest installs `libusb` (`>= 1.0.30`) when building
`libaoahid` from source on Windows or another platform managed with `vcpkg`:

```powershell
vcpkg install --triplet x64-windows --x-manifest-root=. --x-install-root=build/vcpkg_installed
cmake -S . -B build/release -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_INSTALLED_DIR="$PWD/build/vcpkg_installed" `
  -DVCPKG_MANIFEST_INSTALL=OFF `
  -DAOAHID_USE_FAKE_LIBUSB=OFF
cmake --build build/release --config Release
```

## Language bindings

- **Python** ([bindings/python/](../bindings/python)): pure `ctypes` package
  (`aoahid`). Pass the path of `libaoahid.so`, `libaoahid.dylib`, or
  `aoahid.dll` to `aoahid.load(path)`; the package looks for no library by
  itself. Build a wheel with
  `python3 -m build --wheel bindings/python`.
- **C#** ([bindings/csharp/AoaHid/](../bindings/csharp/AoaHid)): .NET 8 P/Invoke
  binding (`AoaHid`). Pack with
  `dotnet pack bindings/csharp/AoaHid/AoaHid.csproj -c Release`.
- **Rust** ([bindings/rust/](../bindings/rust)): workspace with the raw FFI
  crate (`aoahid-sys`) and safe wrapper crate (`aoahid`). Set `AOAHID_LIB_DIR`
  to the directory that holds the library before running `cargo build` or
  `cargo test` (`bindings/rust/aoahid-sys/build.rs`).

The FFI declarations and ABI layout tests are generated from
[include/aoahid.h](../include/aoahid.h) by `tools/bindings/generate.py`. Verify
that the checked-in bindings match the header with:

```sh
python3 tools/bindings/generate.py --check
```
