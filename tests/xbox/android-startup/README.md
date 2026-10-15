# Android startup configuration regression

Run from the repository root as an unprivileged host user after configuring a
native QEMU build and building `build/libqemuutil.a`:

```sh
python3 tests/xbox/android-startup/test-startup.py \
  --cc clang-21 --cxx clang++-21
```

Use `--build-dir` for a different native build and `--output-dir` for temporary
generated sources/binaries. The default outputs live under the native build
directory. GLib development headers/libraries and C/C++ compilers are required.
No Android device, firmware, game files or GPU is required.

The test compiles fragments extracted from the current production sources:

- Android's argument construction and launch context, including TCG tuning and
  both DVD file-descriptor branches.
- QEMU's argument forwarding, complete option definition table, `lookup_opt`,
  first-pass parsing/config gate and default-config reader in `system/vl.c`.
- The actual `-readconfig` case, plus `util/qemu-config.c` and the native utility
  library for real file opening, config parsing and fatal error propagation.

Only the default path locator, config application callback, Android environment
helpers, logging and display initialization are substituted. The denied config
is a real chmod-000 file; the test first verifies that opening it returns
EACCES. Generated arguments must prevent any default-config read attempt, while
explicit `-readconfig` still reads and validates its supplied path. Native
arguments without the switch must retain fatal denied/default-file behavior,
ignore a missing default, and parse a readable default.

This covers 45 option/config cases across eight tuning/DVD combinations. Source
extraction fails when expected boundaries change, so a refactor requires review
instead of silently testing an outdated copy. It is narrower than a complete
QEMU/SDL/Android startup: machine creation, TOML loading, scoped storage itself,
Java lifecycle and runtime graphics/audio are not exercised.

For a negative control, supply a pre-fix `xemu_android.cpp` through
`--android-source`. The ordinary test must fail because its generated arguments
cause the denied default to be opened. Do not run this regression as root;
root's ability to bypass file permissions invalidates the EACCES fixture.
