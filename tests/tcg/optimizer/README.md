# TCG setcond/branch regression

This standalone host fixture runs the real `tcg_optimize()` over small synthetic
IR blocks and interprets their results before and after optimization. It needs
the generated headers and utility library from a configured native QEMU build,
plus GLib development files; it needs no Xbox firmware, games or GPU.

From this directory, after building `libqemuutil.a` in the native build:

```sh
make CC=clang-21 LDFLAGS='-fuse-ld=lld -Wl,--gc-sections' check
```

Override `BUILD_DIR` for a different native build directory. The default test
binaries go under `build/tests/tcg/optimizer`, outside the source directory.

The fixture checks 36,000 cases each with the peephole enabled and disabled:
32/64-bit comparisons, ten signed/unsigned/equality conditions, EQ/NE branches
and a nonfusing LT branch, ten operand edge values, EBB/TB temporary lifetimes,
and a destination that is distinct from or aliases either source. The EBB
result remains observable on conditional fallthrough; the TB result also
remains observable after the branch label. An independent scalar interpreter
compares the observable values rather than merely asserting an opcode shape.

The allocator, opcode list operations and backend capability hooks are small
test stubs. The fixture does not exercise JIT machine code, register allocation,
hardware timing, floating-point emulation or a complete Xbox boot. The normal
TCG pipeline runs liveness analysis after optimization; it can remove a fused
comparison's `setcond` only when the boolean result is actually dead.

For a sanitizer run with a compiler that has the required runtime installed:

```sh
make CC='gcc -fsanitize=address,undefined -fno-omit-frame-pointer' \
  TEST_DIR="$PWD/../../../build/tests/tcg/optimizer/sanitized" \
  check
```
