# Tests

libaoahid is tested in three layers:

| Layer | Runs against | Where | What it can show |
|---|---|---|---|
| **unit** | nothing but memory | `unit/` | Descriptor bytes, field layouts, option validation and profile state, with no USB transport at all. |
| **sim** | the fake libusb in `fake_libusb/` | `sim/` | The exact AOA control sequence (requests 51 to 57), transfer lifecycles, error paths, threading and teardown, deterministically. |
| **hw** | a real Android phone | [`hw/`](hw/README.md) | That Android accepts the reports and delivers them as input. Manual; not run by ctest or CI. |

Every unit and sim test is a ctest test, built when `AOAHID_BUILD_TESTS=ON`.
The sim tests need `AOAHID_USE_FAKE_LIBUSB=ON`; without it CMake refuses the
test build, because the tests never open a real device.

## Directories

| Directory | Why it exists |
|---|---|
| `unit/` | The `aoahid_tests` runner (`test_main.cpp`) and the unit suites: HID item encoding (`test_item_writer.cpp`), the raw-descriptor validator (`test_raw_validator.cpp`), one file per profile (`test_profiles_<profile>.cpp`, sharing `profile_support.hpp`) and every Spec option field (`test_spec_options.cpp`). |
| `sim/` | The sim suites, linked into the same `aoahid_tests` runner, plus three tests that need an executable of their own (internal thread, serialization check, allocation failure). `sim_support.hpp` opens one fake phone through the public ABI; `transport_support.hpp` holds what the transport files share. |
| `fake_libusb/` | A deterministic libusb replacement: a `libusb.h` with the same API plus `aoahid_fake_libusb_*` knobs to add phones, queue control and transfer results, delay or fail completions, unplug a phone and read back every request sent. It contains no HID policy. |
| `golden/` | The public byte contract: the exact descriptor bytes each representative Spec must generate. A change here is a change to what phones receive. |
| `abi/` | The C ABI as compiled: `layout_oracle.c` prints the size, alignment and offsets the C compiler sees and `check_abi_golden.py` compares them with the recorded schema; `test_c_abi.c` and `test_cpp_wrapper.cpp` call the ABI from C11 and through the C++ facade. |
| `hidtools/` | `export_descriptors.cpp` writes the golden descriptors to files, so `aoahid.golden-descriptors` can compare them and the CI job "hid-tools descriptor parse" can feed them to the pinned upstream hid-tools parser. |
| `fuzz/` | One libFuzzer target over the pure paths: descriptor construction, report serialization and raw-descriptor validation (`AOAHID_BUILD_FUZZ=ON`, Clang). It asserts memory safety only; CI runs a bounded smoke run under ASan and UBSan. |
| `cmake/` | Tests of the build and the installed package: an old libusb is refused, the package's multiarch layout and components, the serialization-check abort, and a Windows `min`/`max` macro fixture. |
| `release/` | `test_release_tools.py`: the release metadata inspectors in `tools/release/` fail closed on bad input. |
| `hw/` | No code: how to check a change on a real phone, and what to record. |

## ctest names

| ctest | What runs |
|---|---|
| `aoahid.unit` | `aoahid_tests item_writer profiles spec_options raw_descriptor` |
| `aoahid.sim` | `aoahid_tests transport identity api_arguments report_state transport_failures` |
| `aoahid.sim-internal-thread` | A session with `AOAHID_EVENT_INTERNAL_THREAD` (its own executable). |
| `aoahid.sim-allocation-failure` | Fails each allocation of a whole session in turn, in both event modes, and expects `AOAHID_ERR_INTERNAL` with no leak (its own executable: it replaces the global allocator). |
| `aoahid.sim-serialization-check-ok`, `-violation` | With `AOAHID_CHECK_SERIALIZATION=ON`, serialized use passes and concurrent use aborts. |
| `aoahid.c-abi`, `aoahid.cpp-wrapper`, `aoahid.abi-layout-oracle` | `abi/` |
| `aoahid.golden-descriptors`, `aoahid.support-table` | `golden/`; the README support table is generated from the same manifests. |
| `aoahid.reject-old-libusb-fallback`, `aoahid.package-*`, `aoahid.windows-libusb-minmax` | `cmake/` |
| `aoahid.example-*` | The examples, when `AOAHID_BUILD_EXAMPLES=ON`. |

`aoahid_tests` with no argument runs every suite; with names it runs those
suites only, and it always ends with `<n> checks, <m> failed`.

## Running

From the repository root:

```sh
# Debug, tests, fake libusb, serialization check
cmake --preset dev && cmake --build --preset dev && ctest --preset dev

# One suite, or one ctest
./build/dev/aoahid_tests report_state
ctest --test-dir build/dev -R aoahid.sim --output-on-failure

# AddressSanitizer + UndefinedBehaviorSanitizer
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DAOAHID_BUILD_TESTS=ON \
  -DAOAHID_USE_FAKE_LIBUSB=ON -DAOAHID_CHECK_SERIALIZATION=ON -DAOAHID_SANITIZE=ON
cmake --build build/asan && ctest --test-dir build/asan --output-on-failure

# ThreadSanitizer
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan

# Release-tool tests and the source checks CI runs
python3 tests/release/test_release_tools.py
python3 tools/check_input_provenance.py
python3 tools/check_public_contracts.py
python3 tools/check_license_headers.py
python3 tools/symbol-lint/symbol_lint.py --header include/aoahid.h
```

## Coverage

`AOAHID_COVERAGE=ON` (GCC or Clang; not combinable with the sanitizers or
fuzzing) builds everything with `--coverage`. The `coverage` preset is the
dev preset with it on:

```sh
cmake --preset coverage && cmake --build --preset coverage && ctest --preset coverage
gcovr --root . --object-directory build/coverage --filter 'src/' \
  --exclude-throw-branches --exclude-unreachable-branches \
  --html-details build/coverage/coverage/index.html --print-summary
```

The CI job "Coverage" runs the same and fails below 87% of lines or 75% of
branches of `src/`; the table is in the job summary and the HTML report is
the `coverage` artifact. Coverage counts the unit and sim layers only.

### What is deliberately not covered

These lines stay uncovered on purpose; a test for them would test the test
harness, not the library:

- **`catch (...)` at the ABI boundary** (`AOAHID_C_RESULT_CATCH` and the
  catch blocks in `src/api/error_detail.cpp` and `c_api_context.cpp`). They
  turn an exception into `AOAHID_ERR_INTERNAL` so none crosses the C ABI.
  `aoahid.sim-allocation-failure` reaches the ones an allocation can reach;
  the rest guard calls that cannot throw today. That test is not run with MSVC: with the
  global `operator new` replaced it ends in a fast-fail (0xc0000409) on Windows that has not
  been traced yet, so the failure paths it covers are checked on Linux and macOS only.
- **Null results of `new (std::nothrow)`** in the transport and Channel
  (`port.cpp`, `channel.cpp`, `transport.cpp`). The allocation-failure test
  fails them, but some sit behind a branch only one platform takes.
- **Defensive `AOAHID_ERR_INTERNAL` checks on generated data**: the
  generated-descriptor checks in `src/hid/validator.cpp`
  (`validate_generated`) and the builder's `good_ = false` paths in
  `descriptor_builder.cpp`. They catch a bug in the generator; a correct
  generator never reaches them, and forging their input would only test the
  forgery.
- **Arithmetic overflow guards** past `size_t`, `uint32_t` or `int64_t` (bit
  counts, usage counts, pending deltas). Reaching them needs inputs larger
  than any descriptor the 16-bit AOA length allows.
- **Log line truncation** in `error_detail.cpp`, and the libusb log
  callback, which the fake libusb never calls.
- **Real-libusb behaviour**: electrical faults, backend-specific statuses and
  timing. That is the hw layer.

## Adding a test

1. Pick the layer. Pure descriptor, layout or state logic goes in `unit/`;
   anything that sends a request or touches a transfer goes in `sim/`; what
   only a phone can show goes in `hw/` as a manual step.
2. Put it in the file for its area (`unit/test_profiles_mouse.cpp`,
   `sim/test_channel.cpp`, ...). A new area gets a new file: start it with
   the `SPDX-License-Identifier: MIT` and copyright lines and a comment that
   says what it checks, add it to the `aoahid_tests` sources in
   `CMakeLists.txt`, and either call it from the suite it belongs to or add
   a suite to `kSuites` in `unit/test_main.cpp` and to the `aoahid.unit` or
   `aoahid.sim` command.
3. Use `AOAHID_CHECK(condition)` from `test.hpp`; it counts every check and
   records each failure with its file and line. A sim test starts and ends
   with `aoahid_fake_libusb_reset()` and checks
   `aoahid_fake_libusb_open_handle_count() == 0U` after teardown.
4. Nothing is inferred by the library, so a test passes every option and
   timeout explicitly; `sim_support.hpp` has the ones the sim tests share.
5. A test that needs its own process (it replaces the allocator, aborts, or
   needs a thread mode for the whole process) gets its own executable and
   ctest next to `aoahid.sim-internal-thread` in `CMakeLists.txt`.
6. Run the dev, ASan and TSan builds, and `clang-format-14` on the files you
   touched.
