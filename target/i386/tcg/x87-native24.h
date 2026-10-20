/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guarded x87 single-precision arithmetic, retaining 80-bit registers. */
#ifndef TARGET_I386_TCG_X87_NATIVE24_H
#define TARGET_I386_TCG_X87_NATIVE24_H

#include <fenv.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* X87N_ADD/SUB/MUL/DIV are declared by x87-native.h. */

/*
 * Only exactly representable float inputs with exponents in [-60, 60] are
 * admitted. Their smallest nonzero add/sub result is at least 2^-83, their
 * nonzero product is in [2^-120, 2^122), and their nonzero quotient is in
 * (2^-121, 2^121).
 * Thus every nonzero result is normal in binary32, before and after rounding.
 * Native float arithmetic rounds once to 24 bits, exactly as x87 PC24 does
 * within this range. This is not a double operation followed by a float cast:
 * that could double-round a division or square root at a midpoint.
 *
 * Values outside this conservative subset retain the reference path, including
 * normal extended values outside the float exponent range or with more than
 * 24 significant bits. In particular, operands are never rounded on entry.
 */
#define X87N24_EXP_LIMIT 60
#define X87N24_FRAC_MASK UINT32_C(0x007fffff)
#define X87N24_DISCARDED_MASK UINT64_C(0x000000ffffffffff)

enum X87Native24Result {
    X87N24_REJECT_INPUT,
    X87N24_HIT,
    X87N24_REJECT_HOST,
    X87N24_REJECT_RESULT,
};

static inline bool x87n24_host_nearest(void)
{
#if defined(__aarch64__) && defined(__ARM_ARCH)
    uint64_t fpcr;

    /*
     * Require nearest rounding and masked hardware exceptions. Reject AH/FIZ
     * conservatively. FZ, DN and FP16 controls cannot affect the admitted
     * ordinary binary32 values or the normal binary64 exactness checks.
     * __ARM_ARCH also keeps host fixtures which select the ARM helper gate
     * from accidentally compiling ARM assembly on a different host CPU.
     */
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    return !(fpcr & ((UINT64_C(3) << 22) | (UINT64_C(0x1f) << 8) |
                     (UINT64_C(1) << 15) | UINT64_C(3)));
#else
    return fegetround() == FE_TONEAREST;
#endif
}

static inline bool x87n24_to_float(uint64_t low, uint16_t high, float *out)
{
    uint32_t exp = high & 0x7fff;
    uint32_t bits = (uint32_t)(high >> 15) << 31;

    if (!exp) {
        if (low) {
            return false;
        }
    } else {
        int e = (int)exp - 16383;

        if (!(low >> 63) || (low & X87N24_DISCARDED_MASK) ||
            e < -X87N24_EXP_LIMIT || e > X87N24_EXP_LIMIT) {
            return false;
        }
        bits |= (uint32_t)(e + 127) << 23;
        bits |= (uint32_t)(low >> 40) & X87N24_FRAC_MASK;
    }
    memcpy(out, &bits, sizeof(bits));
    return true;
}

static inline bool x87n24_from_float(float value, uint64_t *low, uint16_t *high)
{
    uint32_t bits;
    uint64_t result_low;
    uint16_t result_high;

    memcpy(&bits, &value, sizeof(bits));
    uint32_t exp = (bits >> 23) & 0xff;

    if (!exp) {
        if (bits & X87N24_FRAC_MASK) {
            return false;
        }
        result_low = 0;
        result_high = (bits >> 31) << 15;
    } else {
        if (exp == 0xff) {
            return false;
        }
        result_low = (UINT64_C(1) << 63) |
                     ((uint64_t)(bits & X87N24_FRAC_MASK) << 40);
        result_high = ((bits >> 31) << 15) | (exp + (16383 - 127));
    }
    *low = result_low;
    *high = result_high;
    return true;
}

/*
 * Admitted PC24 operations produce only normal binary32 values or signed zero
 * (see the operand-bound proof above). Pack those results without rechecking
 * impossible subnormal/special cases. The checked converter remains available
 * for callers without that proof. Operand and host admission stay unchanged.
 */
static inline void x87n24_from_bounded_float(float value, uint64_t *low,
                                           uint16_t *high)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    uint32_t exp = (bits >> 23) & 0xff;
    uint64_t significand = (UINT64_C(1) << 63) |
                          ((uint64_t)(bits & X87N24_FRAC_MASK) << 40);

    *low = exp ? significand : 0;
    *high = ((bits >> 31) << 15) | (exp ? exp + (16383 - 127) : 0);
}

/*
 * Every non-HIT return leaves result outputs untouched. A false need_inexact
 * skips exactness work when the caller already has both guest sticky flags.
 * Operand admission, host mode checks and result computation are unchanged.
 */
static inline enum X87Native24Result
x87n24_binop(int op, uint64_t a_low, uint16_t a_high,
            uint64_t b_low, uint16_t b_high,
            uint64_t *r_low, uint16_t *r_high, bool *inexact, bool need_inexact)
{
    float a, b, r;
    bool lost;
    uint64_t low;
    uint16_t high;

    if (!x87n24_to_float(a_low, a_high, &a) ||
        !x87n24_to_float(b_low, b_high, &b) ||
        (unsigned)op > X87N_DIV || (op == X87N_DIV && !b_low)) {
        return X87N24_REJECT_INPUT;
    }
    if (!x87n24_host_nearest()) {
        return X87N24_REJECT_HOST;
    }

    switch (op) {
    case X87N_SUB:
        b = -b;
        /* fall through */
    case X87N_ADD:
        r = a + b;
        /*
         * If r is the exact sum, both differences equal the other operand.
         * Otherwise, subtracting the larger-magnitude input from r is exact
         * by Sterbenz: r is within a factor of two of that input. Opposite-
         * sign cancellation outside that interval would itself have been
         * exact. The exact difference therefore exposes any lost bits.
         * Checking both differences avoids a magnitude-selection branch and
         * detects tiny addends which a rounded double sum could also lose.
         */
        lost = need_inexact && ((double)r - (double)a != (double)b ||
                           (double)r - (double)b != (double)a);
        break;
    case X87N_MUL:
        r = a * b;
        /* The product of two 24-bit significands is exact in binary64. */
        lost = need_inexact && (double)a * (double)b != (double)r;
        break;
    case X87N_DIV:
        r = a / b;
        lost = need_inexact && (double)r * (double)b != (double)a;
        break;
    default:
        return X87N24_REJECT_INPUT;
    }

    x87n24_from_bounded_float(r, &low, &high);
    *r_low = low;
    *r_high = high;
    if (need_inexact) {
        *inexact = lost;
    }
    return X87N24_HIT;
}

static inline enum X87Native24Result
x87n24_sqrt(uint64_t a_low, uint16_t a_high,
           uint64_t *r_low, uint16_t *r_high, bool *inexact)
{
    float a, r;
    uint64_t low;
    uint16_t high;

    if (!x87n24_to_float(a_low, a_high, &a) ||
        (a_low && (a_high >> 15))) {
        return X87N24_REJECT_INPUT;
    }
    if (!x87n24_host_nearest()) {
        return X87N24_REJECT_HOST;
    }
    r = sqrtf(a);
    x87n24_from_bounded_float(r, &low, &high);
    *r_low = low;
    *r_high = high;
    *inexact = (double)r * (double)r != (double)a;
    return X87N24_HIT;
}

#endif
