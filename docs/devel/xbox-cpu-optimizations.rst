Xbox Android CPU optimization switches
======================================

The Android native CMake build exposes three independent switches:

=========================== =============================================
Switch                      Controlled path
=========================== =============================================
``XEMU_OPT_NATIVE_FLOAT``    Native ARM64 SSE floating-point helpers
``XEMU_OPT_TCG_PEEPHOLE``    Adjacent setcond/conditional-branch fusion
``XEMU_OPT_TB_CACHE_HINTS``  Persistent translation-block cache hints
=========================== =============================================

All three default to ``ON`` in fresh CMake build directories. This preserves
their historical effective behavior: previously the CMake options defaulted to
``OFF``, but an OFF option emitted no compiler definition, and the C/C++ sources
defaulted an undefined switch to 1. Both the core and Android frontend now
receive an explicit ``=0`` or ``=1`` definition. Turning a switch OFF therefore
actually disables its path, which permits independent reference comparisons.

Existing CMake caches containing OFF now select the disabled path. To preserve
the previous behavior when reusing such a directory, explicitly configure
``-DXEMU_OPT_NATIVE_FLOAT=ON -DXEMU_OPT_TCG_PEEPHOLE=ON
-DXEMU_OPT_TB_CACHE_HINTS=ON``, or configure a fresh build directory. Check the
``Native optimization switches`` configure message and generated compile
commands when comparing builds. Desktop Meson defaults are unchanged.

These switches are build-time experiments, not claims of game compatibility or
device performance. The native floating-point implementation is unchanged by
the switch correction. Keep QEMU assertions enabled, and compare one switch
at a time against the same source, compiler, workload and build type.

The standalone regression in ``tests/tcg/optimizer/`` checks preservation of
live boolean results and comparison operand lifetimes during peephole fusion.
The optimizer retains ``setcond`` until the subsequent liveness pass can prove
its result dead; temporary kind alone does not establish that fact.
