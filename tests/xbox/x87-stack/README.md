# Raw x87 stack and move lowering

This experiment enables only on AArch64 Xbox builds with
`XEMU_OPT_X87_STACK=1` and `g_use_fp_jit==0`. The default when the macro is absent
is OFF. Build with explicit 0/1 values for an otherwise identical device A/B.
It does not enable the old native-double-storage FPJIT path or change x87
arithmetic, SSE, guest memory operations, rendering or synchronization.

The six selected emitters implement push/pop, FT0-from-ST, ST0-from-ST,
ST-from-ST0 and FXCH using integer TCG operations against authoritative CPU
state. Each first invokes the existing `gen_flush_fp`, retaining the FP/FIP/FDP
flush that occurred before the old helper call. The raw path keeps no cached
floating values or stack delta. Helpers and block boundaries continue to use
the existing flush machinery. All sources are loaded before overlapping stores.
Copies include the complete 16-byte floatx80 object; compile-time layout checks
are restricted to the active AArch64 option so unrelated host ABIs still build.
Padding is nonarchitectural, and no bytes outside the object are touched.

`fmov_ST0_FT0` is left unchanged: there is no active emitter/call site beyond
its existing helper-mapping macro. The three real move forms and FXCH preserve
all 80 payload bits, including NaNs, unsupported encodings, signed zero and
values that cannot be represented by a double. Tags, FP status/control and
integer flags follow the existing helpers exactly, including empty operands.

## Source IR differential fixture

Run from the repository root:

```sh
python3 tests/xbox/x87-stack/test.py --negative-control
python3 tests/xbox/x87-stack/test.py --sanitize --negative-control
python3 tests/xbox/x87-stack/test.py --cc clang-21 --negative-control
```

`--output-dir PATH` retains generated inputs, binaries and logs. The default
`--baseline-ref` is immutable RC10
`fa8b312c49292e60c53f5f8221a33d4223e282bb`.

The runner extracts the actual current emitter bodies, pointer/tag generators,
flush functions, fallback macros and actual ARM floatx80/FPReg types. Integer
operations emitted by that code execute in a small IR interpreter. Its oracle
is the real immutable RC10 push/pop/move/swap helper source, not a reimplementation
of their arithmetic. The real baseline MMX/EMMS/FCHS helpers run between selected
operations. The fixture separately runs the feature OFF, ON, and feature ON
with the AArch64 host gate OFF. It explicitly rejects the raw path when
`g_use_fp_jit` is nonzero.

Each normal variant checks:

- 3,456 combinations of TOP, ST index, operation and raw payload, with pending
  FIP/FDP metadata and randomized surrounding bytes/tags/status/control/flags.
- 100,000 sequential randomized operations.
- 1,000 blocks of 32 operations mixed with real MMX/EMMS/unary helpers.
- FLD ST(7) after push, FSTP ST(0)/ST(7), FXCH ST(0) alias, runtime taken/untaken
  FCMOV branches, helper/block flushes and a modeled observation before a fault.
- Actual i386 32-bit target-long stores into the 64-bit FIP/FDP fields, preserving
  the upper 32 bits. This differs from a 64-bit guest fixture.

The full modeled CPU state is compared, including canaries, MMX low64 aliases,
`ft0_native`, selectors and status. Only floatx80 tail padding is normalized:
C structure assignment is permitted to copy or omit it, and it is not x87
architectural state. The fixture does not use host floating arithmetic.

Three negative controls must fail: removing the raw push's FIP/FDP flush,
omitting the copied upper object word, and substituting the actual RC10 emitter
bodies while retaining the expected zero-helper-call requirement.

A representative sequence containing one of each selected operation changes
from six runtime helper calls to zero. The unoptimized source IR count grows
from 10 to 76, since a call hides its callee's work. This is not an instruction
count or a performance regression claim; real generated code and device
measurements determine the total cost.

This fixture validates emitted source semantics and explicit boundary models.
It does not execute a Vulkan game, the full decoder, real guest memory faults,
TCG register allocation or backend machine code. The real-TCG-JIT fixture
provided alongside this test supplies the backend proof; Android integration
builds and device A/B remain separate checks. No device FPS gain is predicted.
