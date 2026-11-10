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
# The archive's own libusb, found again at run time through the RPATH.
LIBUSB='-Llib -lusb-1.0 -Wl,-rpath,$ORIGIN/lib'

# One example per HID profile kind. None of these opens a device; each builds a
# complete Spec, prints the descriptor it produced, and then prints the exact
# call sequence that would send that profile to a real phone.
cc -std=c17 -O2 -Iinclude $EX/c/profiles/keyboard.c    lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -o keyboard
cc -std=c17 -O2 -Iinclude $EX/c/profiles/mouse.c       lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -o mouse
cc -std=c17 -O2 -Iinclude $EX/c/profiles/toggle.c      lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -o toggle
cc -std=c17 -O2 -Iinclude $EX/c/profiles/gamepad.c     lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -o gamepad
cc -std=c17 -O2 -Iinclude $EX/c/profiles/touchscreen.c lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -o touchscreen
cc -std=c17 -O2 -Iinclude $EX/c/profiles/touchpad.c    lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -o touchpad
cc -std=c17 -O2 -Iinclude $EX/c/profiles/pen.c         lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -o pen
cc -std=c17 -O2 -Iinclude $EX/c/profiles/digitizer.c   lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -o digitizer
cc -std=c17 -O2 -Iinclude $EX/c/profiles/keyboard_battery.c lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -o keyboard_battery
cc -std=c17 -O2 -Iinclude $EX/c/profiles/raw.c         lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -o raw

# The full discovery-to-teardown lifecycle example. Opens a real device if one
# is connected.
cc -std=c17 -O2 -Iinclude $EX/c/multi_profile.c lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -o multi_profile

# Programs to watch on a real phone (docs/QUICKSTART.md has the walkthrough).
# The ones that use math.h also need -lm.
cc -std=c17 -O2 -Iinclude $EX/c/verify/verify_keyboard.c lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -o verify_keyboard
cc -std=c17 -O2 -Iinclude $EX/c/verify/verify_touch.c    lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -lm -o verify_touch
cc -std=c17 -O2 -Iinclude $EX/c/verify/verify_mouse.c    lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -lm -o verify_mouse
cc -std=c17 -O2 -Iinclude $EX/c/verify/verify_toggle.c   lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -o verify_toggle
cc -std=c17 -O2 -Iinclude $EX/c/verify/verify_all.c      lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -lm -o verify_all
cc -std=c17 -O2 -Iinclude $EX/c/verify/verify_accessory.c lib/libaoahid_static.a -lstdc++ $LIBUSB -lpthread -o verify_accessory
```

## C++ example

`examples/cpp/multitouch_gesture.cpp` includes `aoahid.hpp` (C++20) and opens the first phone
found:

```sh
EX=share/doc/libaoahid/examples
LIBUSB='-Llib -lusb-1.0 -Wl,-rpath,$ORIGIN/lib'
c++ -std=c++20 -O2 -Iinclude $EX/cpp/multitouch_gesture.cpp lib/libaoahid_static.a $LIBUSB -lpthread -o multitouch_gesture
```

With CMake, `examples/CMakeLists.txt` builds every example against an installed package
(`find_package(aoahid REQUIRED)`), or from a source checkout with `-DAOAHID_BUILD_EXAMPLES=ON`.

`$LIBUSB` links the libusb the archive ships in `lib/` and records
`$ORIGIN/lib` as the run-time search path, so the programs run from the package
root find it; the single quotes keep the shell from expanding `$ORIGIN`. On
macOS use `-Wl,-rpath,@executable_path/lib` instead. To use a system libusb
1.0.30 or later, set `LIBUSB=-lusb-1.0`.

To link the **shared** library from the same archive instead (`lib/libaoahid.so`,
`lib/libaoahid.dylib`, or `aoahid.dll`), replace `lib/libaoahid_static.a` with
`-Llib -laoahid` in every command above; the same `RPATH` finds it at run time
(on Windows keep the `.dll` files next to the `.exe`).

`PKG_CONFIG_LIBDIR=lib/pkgconfig pkg-config --static --libs aoahid-static`
prints the flags from the archive's own metadata. See `docs/PORTING.md` for
platform-specific notes.

## Python, Rust, and C# examples

Each is a short enumerate-only program: `examples/python/enumerate.py`,
`examples/csharp/Enumerate.csproj`, and the Rust one at
`bindings/rust/aoahid-sys/examples/enumerate.rs`. The `.csproj` and
`Cargo.toml` name the exact dependency versions they were written against. `docs/EXAMPLES.md` in this
package explains what each example demonstrates.
