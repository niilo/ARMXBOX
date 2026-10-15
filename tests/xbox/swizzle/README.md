# Swizzle correctness and benchmark

Run `make check` for deterministic comparisons against an independent Morton
coordinate interleaver. The checks cover both directions, unequal power-of-two
dimensions, volumes, bytes per pixel of 1/2/3/4/8, unaligned buffers, row and
slice padding, and allocation canaries. Padding must remain unchanged.

Run `make bench` for separate swizzle and unswizzle measurements. Each case has
two warmup samples and eleven timed samples. The output includes minimum,
median and maximum time per conversion, and useful-pixel throughput (excluding
padding). The benchmark deliberately includes 2D RGBA8, another pixel size,
and a volume so changes to a fast path can be checked against its fallbacks.

For a before/after comparison, save the original implementation and its header
outside the source directory, then run:

```sh
make clean
make BASELINE_SOURCE=/absolute/path/to/original/swizzle.c
./swizzle-test --check --bench
```

The two implementations are linked into the same executable and their timing
order alternates between samples. Use identical compiler options for both.
Run `make clean` when changing `BASELINE_SOURCE`, the compiler or compiler
options; make does not track these values as object dependencies.

For memory and undefined-behavior checks:

```sh
make clean
make CFLAGS='-O2 -Wall -g -fsanitize=address,undefined -fno-omit-frame-pointer'
./swizzle-test --check
```

Host results measure this conversion kernel on that host. They do not measure
game speed or the performance of another CPU architecture. ARM64 uses NEON;
execute the checks on ARM64 as well when a suitable device is available.
