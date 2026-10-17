/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exact normal x87 transfers without conversion-call or flag setup overhead. */
#ifndef TARGET_I386_TCG_X87_TRANSFER_H
#define TARGET_I386_TCG_X87_TRANSFER_H

#include "fpu/softfloat.h"

/*
 * These are the same exact normal cases as fpu/softfloat.c. Keeping their
 * small packing operations inline lets the CPU helpers also bypass saving,
 * clearing and merging flags when no new flag can be raised. Other cases
 * still use the complete softfloat conversion and exception path.
 */
static inline bool x87t_load_f32(uint32_t bits, floatx80 *out)
{
    uint32_t exp = (bits >> 23) & 0xff;

    if (exp - 1 >= 0xfe) {
        return false;
    }
    out->low = (UINT64_C(1) << 63) | ((uint64_t)(bits & 0x7fffff) << 40);
    out->high = ((bits >> 16) & 0x8000) | (exp + (0x3fff - 0x7f));
    return true;
}

static inline bool x87t_load_f64(uint64_t bits, FloatX80RoundPrec precision,
                                 floatx80 *out)
{
    uint32_t exp = (bits >> 52) & 0x7ff;

    if (exp - 1 >= 0x7fe || precision == floatx80_precision_s) {
        return false;
    }
    out->low = (UINT64_C(1) << 63) | (bits << 11);
    out->high = ((bits >> 48) & 0x8000) | (exp + (0x3fff - 0x3ff));
    return true;
}

static inline bool x87t_store_f32(floatx80 value, uint32_t *out)
{
    uint32_t exp = (value.high & 0x7fff) - (0x3fff - 0x7f);

    if (!(value.low >> 63) || exp - 1 >= 0xfe ||
        (value.low & UINT64_C(0xffffffffff))) {
        return false;
    }
    *out = ((uint32_t)(value.high >> 15) << 31) | (exp << 23) |
           ((value.low >> 40) & 0x7fffff);
    return true;
}

static inline bool x87t_store_f64(floatx80 value, uint64_t *out)
{
    uint32_t exp = (value.high & 0x7fff) - (0x3fff - 0x3ff);

    if (!(value.low >> 63) || exp - 1 >= 0x7fe || (value.low & 0x7ff)) {
        return false;
    }
    *out = ((uint64_t)(value.high >> 15) << 63) | ((uint64_t)exp << 52) |
           ((value.low >> 11) & UINT64_C(0xfffffffffffff));
    return true;
}

#endif
