<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->

# MCPX voice attenuation reuse regression

The standalone fixture compiles complete production `vp.c` against the real
configured APU/QEMU headers and real libsamplerate. Its immutable comparison
source is RC7 commit `d2da241bab37c6460fc8832e8ded75d42362e739`; pass
`--baseline-ref REV` to select another revision explicitly. RC7 is the
last published candidate before this audio optimization; its audio source is
identical to RC8/RC9. Keeping that revision immutable prevents later commits
from silently replacing the comparison with already-optimized code. Checked fixture
RAM accessors provide the guest writes and the UI mute hook is deterministic.
Production voice decoding, envelopes, resampling, LPF/HRTF filters and mixing
remain actual source. No games, firmware or audio device are required.

Run inside the configured native development container from the repository
root. Build native DSP/QEMU utility and object dependencies first if absent:

```sh
meson compile -C build test-xbox
ninja -C build libqom.a.p/qom_container.c.o \
    libqom.a.p/qom_object.c.o libqom.a.p/qom_object_interfaces.c.o \
    libqom.a.p/qom_qom-qobject.c.o \
    libevent-loop-base.a.p/event-loop-base.c.o
python3 tests/xbox/audio/audio-vp-test.py --negative-control
python3 tests/xbox/audio/audio-vp-test.py --negative-control --cc gcc --sanitize
python3 tests/xbox/audio/dsp-engines-test.py
```

Clang 21 and the configured container GCC 11 have been exercised. The script
reuses include and configuration flags from native `compile_commands.json`,
removing Clang-only flags for GCC. Sanitizers require the compiler's runtime
libraries. Link inputs retain real QEMU utility/trace/thread and QOM objects,
because GCC's global instrumentation keeps the otherwise unused MMIO table.
The new fixtures are standalone and are not included in APKs or the Meson
one-test DSP suite.

Both Android `SRC_LINEAR` and desktop `SRC_SINC_FASTEST` branches are tested.
The Android macro is set only after host headers are parsed, allowing actual
Android voice/resampler selection to run against the native library. Select
one branch with `--resampler android` or `--resampler desktop`.

Each branch runs 798 scenarios and 14,560 actual voice frames. Coverage includes
all 4,096 attenuation codes in nearest-rounding sweeps, selected boundary
cases under each of four host FENV rounding modes, mono/stereo
S16 PCM and ADPCM input, multipass, LPF, HRTF, pitch changes, monitor selection,
mute, pause, isolation and seeded randomized records. It compares complete
mixbins, stereo float and S16 output samples, guest voice RAM, debug/filter
state, errno and floating-point flags byte for byte against the old source.
The reduced-call check and explicit original-source negative control verify
the saved work as well as unchanged outputs.

The DSP companion runs the retained synthetic basic program first with its
default C interpreter and then with Rust JIT enabled in an external build copy
of the same test. It prints the actual configured native DSP objects and Rust
library used to link. It leaves production source and the original Meson test
unchanged. Passing this basic program is not exhaustive DSP validation.

`--benchmark` compiles unwrapped binaries with no power-call counters or
forced builtin setting for optional host microbenchmarks. The Android branch
uses the production no-math-errno behavior. These measurements depend
on host load, compiler, libm and samplerate build; they do not predict device
FPS or audio latency. The hard evidence is identical outputs/state and fewer
attenuation calls. Actual Android/Steam Deck callbacks, FIFO timing, concurrent
worker scheduling and audible behavior still require device testing.
