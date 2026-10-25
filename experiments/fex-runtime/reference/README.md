# Xbox reference CPUID capture

`xbox-cpuid.json` records **executed guest CPUID**, not cloud-host CPUID or just a
copy of the declared Pentium III feature mask. A generated 512 KiB ROM runs 54
EAX/ECX input pairs twice, writes outputs into RAM, publishes a completion word
and halts with interrupts disabled. QMP reads the table and verifies equal
passes, the halt and normal emulator process exit. No proprietary firmware,
Xbox game or SDK is included or required.

Capture binary: ARMXBOX Linux x86-64, version `1.2.12-5-ga1aeac59eb`, immutable
source revision `a1aeac59eb417b07f9b72ac882511a675cd9e080`. The JSON records its
executable checksum and generated-ROM checksum. `target/i386/cpu.c`,
`target/i386/cpu.h`, `target/i386/tcg/tcg-cpu.c` and `hw/xbox/xbox.c` at that
revision are byte-identical to the experiment branch's files at
`b9f63657ddab9fec7ec6da1e4d0f9710fcf0a789`. Capture uses the default realized
Xbox machine, one Pentium III CPU, 64 MiB RAM, TCG and real mode. It is a
reference-emulator compatibility golden, not a capture from physical Xbox
hardware or a performance benchmark.

On a Linux desktop, or under a software-rendered virtual display in the supported
Docker environment:

```sh
xvfb-run -a python3 experiments/fex-runtime/scripts/capture-xbox-cpuid.py \
  --binary /path/to/armxbox \
  --reference-commit a1aeac59eb417b07f9b72ac882511a675cd9e080 \
  --output /tmp/armxbox-cpuid-new
```

Use a fresh output directory. The tool gives this process a dedicated emulator
configuration, EEPROM and generated ROM. It verifies the binary's startup
revision and saves logs, registers, the memory dump and JSON. Keep proxy/CA
configuration inherited when installing development tools. Xvfb/Mesa/SDL dummy
audio are development facilities, not Android runtime dependencies.

After reviewing a new capture, replace `xbox-cpuid.json` and regenerate the C++
table with `python3 experiments/fex-runtime/scripts/generate-xbox-cpuid.py`.
CI uses `--check` to prevent JSON/header drift. Do not regenerate expected data
from the FEX implementation being tested.

The reference returns EDX `0383fbfd` for leaf 1: VME is filtered despite appearing
in the declared `PENTIUM3_FEATURES`. Leaf 1 ECX/EBX are zero. Hypervisor leaves
`40000000` and `40000001` are zero. Other tested unsupported basic, extended and
high-range inputs return the Xbox leaf-2 cache descriptors. Repeated ECX inputs
including `ffffffff` do not change any captured leaf's outputs.

## Instruction admission capture

`xbox-instructions.json` records 12 selected instruction encodings, each executed
twice under TCG in protected IA32 CPL0 mode, with flat segments and CR4 `0x600`
(OSFXSR/OSXMMEXCPT). SSE/MMX-enabling CR0 setup and accepted SSE1/MMX/x87 controls
avoid confusing missing OS state with unsupported instructions. A generated GDT
and #UD/#GP/#PF handlers capture exception PC/error, sampled EAX/EDX/EFLAGS and
whether the following sentinel ran. The tool requires the completion marker,
CS32/CPL0/halt state, known controls, repeat consistency and normal process exit.
It does not capture all registers or certify the whole ISA.

| Tested form | Observed retained-reference result |
| --- | --- |
| SSE1 XORPS; MMX PXOR; x87 FLD1 | Executes |
| SSE2 PXOR XMM (`66 0f ef c0`) | Executes despite absent CPUID SSE2 |
| SSE3 HADDPS; SSSE3 PSHUFB; SSE4.1 PTEST; SSE4.2 PCMPGTQ | #UD |
| AVX VZEROUPPER; RDTSCP; XGETBV; explicit UD2 | #UD |

The SSE2 observation is a reference-emulator permissiveness finding. It is not
physical Xbox/Pentium III evidence or a reason to declare SSE2 supported on Xbox.
Likewise one tested form does not settle every prefix, operand or instruction
in its family. Future policy must explicitly distinguish hardware architecture,
reference compatibility and the runtime's admitted subset.

The binary revision is again `a1aeac59eb417b07f9b72ac882511a675cd9e080`; the JSON
records binary, ROM and assembly checksums. Its entire `target/i386` source tree
and `hw/xbox/xbox.c` are byte-identical to this branch at the capture. Two fresh
captures produce identical JSON and ROM checksums. A real-ROM negative control
replacing UD2 with NOP fails the required trap-control check; corrupted result
PC/sentinel/flags are separately rejected. Initial harness assembly/reset errors
produced no accepted table and were corrected before this capture.

Reproduce with GNU binutils (`as`, `ld`), Python and the retained desktop binary:

```sh
xvfb-run -a python3 experiments/fex-runtime/scripts/capture-xbox-instructions.py \
  --binary /path/to/armxbox \
  --reference-commit a1aeac59eb417b07f9b72ac882511a675cd9e080 \
  --output /tmp/armxbox-instructions-new
```

Use a fresh output directory. No FEX-generated output or cloud-host CPUID supplies
the expected outcomes. The script does not alter the production backend or APK.
The capture establishes compatibility observations, not Android native execution,
physical Xbox accuracy, game admission or performance. See the
[next policy plan](../../../docs/devel/fex-hle/phase10-plan.md).
