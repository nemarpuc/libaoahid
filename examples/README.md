# Compiling these examples

These commands build the examples against an extracted release archive. If a
command fails as written, please open an issue.

## C examples

All C examples link against `libaoahid`, which is written in C++ internally.
Each release archive carries both the static library (`lib/libaoahid_static.a`)
and the shared one. The commands below link the static library, which still
needs the C++ standard library at link time, so every command includes
`-lstdc++`. Forgetting
it produces `undefined reference to std::...` linker errors.

Run the commands from the package root (the directory that holds `include/`
and `lib/`). In a release archive the examples are installed under
`share/doc/libaoahid/examples/`:

```sh
EX=share/doc/libaoahid/examples

# One example per HID profile kind. None of these opens a device; each builds a
# complete Spec, prints the descriptor it produced, and then prints the exact
# call sequence that would send that profile to a real phone.
cc -std=c17 -O2 -Iinclude $EX/c/profiles/keyboard.c    lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -o keyboard
cc -std=c17 -O2 -Iinclude $EX/c/profiles/mouse.c       lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -o mouse
cc -std=c17 -O2 -Iinclude $EX/c/profiles/toggle.c      lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -o toggle
cc -std=c17 -O2 -Iinclude $EX/c/profiles/gamepad.c     lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -o gamepad
cc -std=c17 -O2 -Iinclude $EX/c/profiles/touchscreen.c lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -o touchscreen
cc -std=c17 -O2 -Iinclude $EX/c/profiles/touchpad.c    lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -o touchpad
cc -std=c17 -O2 -Iinclude $EX/c/profiles/pen.c         lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -o pen
cc -std=c17 -O2 -Iinclude $EX/c/profiles/digitizer.c   lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -o digitizer
cc -std=c17 -O2 -Iinclude $EX/c/profiles/keyboard_battery.c lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -o keyboard_battery
cc -std=c17 -O2 -Iinclude $EX/c/profiles/raw.c         lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -o raw

# The full discovery-to-teardown lifecycle example. Opens a real device if one
# is connected.
cc -std=c17 -O2 -Iinclude $EX/c/multi_profile.c lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -o multi_profile

# Programs to watch on a real phone (docs/QUICKSTART.md has the walkthrough).
# The ones that use math.h also need -lm.
cc -std=c17 -O2 -Iinclude $EX/c/verify/verify_keyboard.c lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -o verify_keyboard
cc -std=c17 -O2 -Iinclude $EX/c/verify/verify_touch.c    lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -lm -o verify_touch
cc -std=c17 -O2 -Iinclude $EX/c/verify/verify_mouse.c    lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -lm -o verify_mouse
cc -std=c17 -O2 -Iinclude $EX/c/verify/verify_toggle.c   lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -o verify_toggle
cc -std=c17 -O2 -Iinclude $EX/c/verify/verify_all.c      lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -lm -o verify_all
cc -std=c17 -O2 -Iinclude $EX/c/verify/verify_accessory.c lib/libaoahid_static.a -lstdc++ -lusb-1.0 -lpthread -o verify_accessory
```

## C++ example

`examples/cpp/multitouch_gesture.cpp` includes `aoahid.hpp` (C++20) and opens the first phone
found:

```sh
EX=share/doc/libaoahid/examples
c++ -std=c++20 -O2 -Iinclude $EX/cpp/multitouch_gesture.cpp lib/libaoahid_static.a -lusb-1.0 -lpthread -o multitouch_gesture
```

With CMake, `examples/CMakeLists.txt` builds every example against an installed package
(`find_package(aoahid REQUIRED)`), or from a source checkout with `-DAOAHID_BUILD_EXAMPLES=ON`.

To link the **shared** library from the same archive instead (`lib/libaoahid.so`,
`lib/libaoahid.dylib`, or `aoahid.dll`), replace `lib/libaoahid_static.a` with
`-Llib -laoahid` in every command above, and make sure the shared library is on your loader's search
path at run time (`LD_LIBRARY_PATH=./lib` on Linux, `DYLD_LIBRARY_PATH=./lib`
on macOS, or just keep the `.dll` next to the `.exe` on Windows).

`-lusb-1.0` above finds a system-wide libusb; add `-Llib` to link the one the
archive ships in `lib/` instead. `pkg-config --static --libs aoahid-static` with
`PKG_CONFIG_LIBDIR=lib/pkgconfig` prints the exact flags from the archive's
own metadata. See `docs/PORTING.md` for platform-specific notes.

## Python, Rust, and C# examples

Each is a short enumerate-only program: `examples/python/enumerate.py`,
`examples/csharp/Enumerate.csproj`, and the Rust one at
`bindings/rust/aoahid-sys/examples/enumerate.rs`. The `.csproj` and
`Cargo.toml` name the exact dependency versions they were written against. `docs/EXAMPLES.md` in this
package explains what each example demonstrates.
