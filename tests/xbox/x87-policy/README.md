<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Cached x87 native policy

```sh
python3 tests/xbox/x87-policy/test.py --sanitize --negative-control
python3 tests/xbox/x87-policy/test.py --cc clang-21 --negative-control \
  --output-dir build/x87-policy-clang
```

The fixture extracts the actual policy implementation from `fpu_helper.c` and
the immutable v1.2.11 master commit. It compiles Android and desktop branches
separately. Only the external property/environment lookup is substituted; no
arithmetic is modeled here. Ten settings per implementation/platform run in
fresh processes: missing/empty, leading `0`, other strings and Android's maximum
property length. Each process changes the setting after initialization and
checks 10,000 cached calls with exactly one external lookup.

A reentrant lookup also verifies the existing default-before-query behavior.
Two negative controls detect repeated queries and reversed policy semantics on
both platforms. This preserves the existing process cache; it does not introduce
or establish concurrent initialization safety. Normal startup queries the policy
before guest execution. Runtime changes to the external setting require a new
process, as before.

Run the separate `x87-native24/test.py` numerical/helper-state suite after policy
changes. These host fixtures do not measure ARM64 call overhead or device FPS.
Inspect the final ThinLTO APK for outlined property-buffer allocation, smaller
hot helper frames and retained FPCR guards before device comparison.
