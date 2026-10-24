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
