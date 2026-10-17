/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Diagnostic classifications after rejection; no arithmetic is reexecuted. */
#ifndef TARGET_I386_TCG_FPU_PROFILE_REASONS_H
#define TARGET_I386_TCG_FPU_PROFILE_REASONS_H

#include "fpu-profile.h"

#if XEMU_FPU_PROFILE_ACTIVE
#include "x87-native.h"
#include "x87-native24.h"

static inline enum XemuFpuProfileRejectReason
xemu_fpu_profile_control_reason(uint16_t fpuc, bool policy, bool pc24_supported)
{
    unsigned pc = (fpuc >> 8) & 3;
    unsigned rc = (fpuc >> 10) & 3;

    if (pc != 2 && !(pc == 0 && pc24_supported)) {
        return pc == 0 ? XEMU_FPU_REJECT_PC24 :
               pc == 3 ? XEMU_FPU_REJECT_PC64 : XEMU_FPU_REJECT_PC_RESERVED;
    }
    if (rc) {
        return (enum XemuFpuProfileRejectReason)
               (XEMU_FPU_REJECT_ROUND_DOWN + rc - 1);
    }
    return policy ? XEMU_FPU_REJECT_REASON_COUNT : XEMU_FPU_REJECT_POLICY_OFF;
}

static inline enum XemuFpuProfileRejectReason
xemu_fpu_profile_operand_reason_limits(uint64_t low, uint16_t high, bool right,
                                        uint64_t precision_mask, int exp_limit)
{
    unsigned exp = high & 0x7fff;
    int e = (int)exp - 16383;
    int offset;

    if (exp == 0) {
        if (!low) {
            return XEMU_FPU_REJECT_REASON_COUNT;
        }
        offset = low >> 63 ? 1 : 0; /* pseudo-denormal or denormal */
    } else if (!(low >> 63)) {
        offset = 2; /* unnormal, including unsupported special encodings */
    } else if (exp == 0x7fff) {
        offset = low == UINT64_C(0x8000000000000000) ? 3 : 4;
    } else if (low & precision_mask) {
        offset = 5; /* not exactly representable at the selected precision */
    } else if (e < -exp_limit) {
        offset = 6;
    } else if (e > exp_limit) {
        offset = 7;
    } else {
        return XEMU_FPU_REJECT_REASON_COUNT;
    }
    return (enum XemuFpuProfileRejectReason)
           ((right ? XEMU_FPU_REJECT_B_DENORMAL :
                     XEMU_FPU_REJECT_A_DENORMAL) + offset);
}

static inline enum XemuFpuProfileRejectReason
xemu_fpu_profile_operand_reason(uint64_t low, uint16_t high, bool right)
{
    return xemu_fpu_profile_operand_reason_limits(low, high, right, 0x7ff,
                                                  X87N_EXP_LIMIT);
}

static inline enum XemuFpuProfileRejectReason
xemu_fpu_profile_pc24_operand_reason(uint64_t low, uint16_t high, bool right)
{
    return xemu_fpu_profile_operand_reason_limits(
        low, high, right, X87N24_DISCARDED_MASK, X87N24_EXP_LIMIT);
}

static inline enum XemuFpuProfileRejectReason
xemu_fpu_profile_binop_reason(uint16_t fpuc, bool policy,
                              bool pc24_supported, int op,
                              uint64_t a_low, uint16_t a_high,
                              uint64_t b_low, uint16_t b_high)
{
    enum XemuFpuProfileRejectReason reason =
        xemu_fpu_profile_control_reason(fpuc, policy, pc24_supported);

    if (reason != XEMU_FPU_REJECT_REASON_COUNT) {
        return reason;
    }
    reason = xemu_fpu_profile_operand_reason(a_low, a_high, false);
    if (reason != XEMU_FPU_REJECT_REASON_COUNT) {
        return reason;
    }
    reason = xemu_fpu_profile_operand_reason(b_low, b_high, true);
    if (reason != XEMU_FPU_REJECT_REASON_COUNT) {
        return reason;
    }
    if (op < X87N_ADD || op > X87N_DIV) {
        return XEMU_FPU_REJECT_INVALID_OP;
    }
    if (op == X87N_DIV && !b_low && !(b_high & 0x7fff)) {
        return XEMU_FPU_REJECT_DIV_ZERO;
    }
    /* Original outcome rejected after these inputs passed; do not recompute. */
    return XEMU_FPU_REJECT_RESULT;
}

static inline enum XemuFpuProfileRejectReason
xemu_fpu_profile_sqrt_reason(uint16_t fpuc, bool policy, bool pc24_supported,
                             uint64_t low, uint16_t high)
{
    enum XemuFpuProfileRejectReason reason =
        xemu_fpu_profile_control_reason(fpuc, policy, pc24_supported);

    if (reason != XEMU_FPU_REJECT_REASON_COUNT) {
        return reason;
    }
    reason = xemu_fpu_profile_operand_reason(low, high, false);
    if (reason != XEMU_FPU_REJECT_REASON_COUNT) {
        return reason;
    }
    if ((high & 0x8000) && (low || (high & 0x7fff))) {
        return XEMU_FPU_REJECT_SQRT_NEGATIVE;
    }
    return XEMU_FPU_REJECT_RESULT;
}

/* PC24 mode, softfloat state and native policy gates have already passed. */
static inline enum XemuFpuProfileRejectReason
xemu_fpu_profile_pc24_binop_reason(int op, uint64_t a_low, uint16_t a_high,
                                   uint64_t b_low, uint16_t b_high)
{
    enum XemuFpuProfileRejectReason reason =
        xemu_fpu_profile_pc24_operand_reason(a_low, a_high, false);

    if (reason != XEMU_FPU_REJECT_REASON_COUNT) {
        return reason;
    }
    reason = xemu_fpu_profile_pc24_operand_reason(b_low, b_high, true);
    if (reason != XEMU_FPU_REJECT_REASON_COUNT) {
        return reason;
    }
    if (op < X87N_ADD || op > X87N_DIV) {
        return XEMU_FPU_REJECT_INVALID_OP;
    }
    if (op == X87N_DIV && !b_low && !(b_high & 0x7fff)) {
        return XEMU_FPU_REJECT_DIV_ZERO;
    }
    return XEMU_FPU_REJECT_RESULT;
}

static inline enum XemuFpuProfileRejectReason
xemu_fpu_profile_pc24_sqrt_reason(uint64_t low, uint16_t high)
{
    enum XemuFpuProfileRejectReason reason =
        xemu_fpu_profile_pc24_operand_reason(low, high, false);

    if (reason != XEMU_FPU_REJECT_REASON_COUNT) {
        return reason;
    }
    if ((high & 0x8000) && (low || (high & 0x7fff))) {
        return XEMU_FPU_REJECT_SQRT_NEGATIVE;
    }
    return XEMU_FPU_REJECT_RESULT;
}
#endif
#endif
