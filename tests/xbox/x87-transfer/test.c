/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "fpu/softfloat.h"
#include "fpu-profile.h"

/*
 * Real softfloat.c and this TU have already selected the same generic
 * floatx80 declaration. Only now select the production ARM helper gates.
 */
#define XBOX 1
#define __aarch64__ 1
#define USE_NATIVE_DOUBLE_STORAGE 0

typedef union FPReg {
    floatx80 d __attribute__((aligned(16)));
    uint64_t mmx;
} FPReg;
typedef struct CPUX86State {
    uint64_t before[3];
    int fpstt;
    uint16_t fpuc, fpus;
    uint8_t fptags[8];
    FPReg fpregs[8];
    floatx80 ft0;
    double ft0_native;
    float_status fp_status, mmx_status, sse_status;
    uint64_t fpip, fpdp;
    uint16_t fpcs, fpds;
    uint32_t eflags;
    uint64_t after[3];
} CPUX86State;
#define FT0 (env->ft0)
#define ST0 (env->fpregs[env->fpstt].d)
#define FPREG_VAL(r) ((r).d)
#include "common.inc"
#include "reference.inc"
static unsigned ls_calls;
static enum XemuFpuProfileTransfer ls_helper;
static enum XemuFpuProfileTransferPath ls_path;
static void record_transfer(enum XemuFpuProfileTransfer helper,
                            enum XemuFpuProfileTransferPath path)
{
    ls_calls++;
    ls_helper = helper;
    ls_path = path;
}
#undef XEMU_FPU_PROFILE_TRANSFER
#define XEMU_FPU_PROFILE_TRANSFER(helper, path) record_transfer(helper, path)
#include "current.inc"

static uint64_t random_state = UINT64_C(0xaeb4c57682fa);
static uint32_t random32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
}
static uint64_t random64(void)
{
    return (uint64_t)random32() << 32 | random32();
}
static uint64_t cases;
static void init(CPUX86State *env, unsigned precision, unsigned rounding,
                 uint16_t old_flags, uint16_t fpus, uint16_t masks)
{
    uint8_t *p = (void *)env;
    for (unsigned i = 0; i < sizeof(*env); i++) {
        p[i] = random32();
    }
    memset(&env->fp_status, 0, sizeof(env->fp_status));
    memset(&env->mmx_status, 0, sizeof(env->mmx_status));
    memset(&env->sse_status, 0, sizeof(env->sse_status));
    cpu_init_fp_statuses(env);
    set_float_rounding_mode(rounding, &env->fp_status);
    set_floatx80_rounding_precision(precision, &env->fp_status);
    set_float_exception_flags(old_flags, &env->fp_status);
    env->fpstt = random32() & 7;
    /* Deliberately independent of softfloat precision and rounding fields. */
    env->fpuc = ((random32() & ~FPUC_EM) | masks);
    env->fpus = fpus;
}
static void normalize_padding(CPUX86State *env)
{
    for (unsigned i = 0; i < 9; i++) {
        floatx80 *p = i == 8 ? &env->ft0 : &env->fpregs[i].d;
        memset((char *)p + 10, 0, sizeof(*p) - 10);
    }
}
static const enum XemuFpuProfileTransfer helper_ids[] = {
    XEMU_FPU_LS_FLDS_FT0, XEMU_FPU_LS_FLDL_FT0,
    XEMU_FPU_LS_FLDS_ST0, XEMU_FPU_LS_FLDL_ST0,
    XEMU_FPU_LS_FSTS_ST0, XEMU_FPU_LS_FSTL_ST0,
};
static void run_case(CPUX86State *initial, unsigned helper, uint64_t value,
                     int expected_path)
{
    CPUX86State a = *initial, b = *initial;
    uint64_t actual = 0, reference = 0;
    ls_calls = 0;
    switch (helper) {
    case 0:
        orig_helper_flds_FT0(&b, value);
        helper_flds_FT0(&a, value);
        break;
    case 1:
        orig_helper_fldl_FT0(&b, value);
        helper_fldl_FT0(&a, value);
        break;
    case 2:
        orig_helper_flds_ST0(&b, value);
        helper_flds_ST0(&a, value);
        break;
    case 3:
        orig_helper_fldl_ST0(&b, value);
        helper_fldl_ST0(&a, value);
        break;
    case 4:
        reference = orig_helper_fsts_ST0(&b);
        actual = helper_fsts_ST0(&a);
        break;
    case 5:
        reference = orig_helper_fstl_ST0(&b);
        actual = helper_fstl_ST0(&a);
        break;
    default:
        abort();
    }
    normalize_padding(&a);
    normalize_padding(&b);
    if (actual != reference || memcmp(&a, &b, sizeof(a))) {
        fprintf(stderr, "state mismatch case=%" PRIu64 " helper=%u "
                "PC=%u RC=%u flags=%x CW=%x SW=%x value=%016" PRIx64 "\n",
                cases, helper, initial->fp_status.floatx80_rounding_precision,
                initial->fp_status.float_rounding_mode,
                initial->fp_status.float_exception_flags, initial->fpuc,
                initial->fpus, value);
        exit(1);
    }
    assert(ls_calls == 1 && ls_helper == helper_ids[helper]);
    if (!X87_TRANSFER_ACTIVE) {
        assert(ls_path == XEMU_FPU_LS_GENERIC);
    } else if (expected_path >= 0) {
        assert(ls_path == (enum XemuFpuProfileTransferPath)expected_path);
    }
    cases++;
}
static void edges(void)
{
    static const uint32_t f32[] = {
        0, 0x80000000, 1, 0x007fffff, 0x00800000, 0x3f800000,
        0x3f800001, 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000,
        0x7fc00000, 0x7f800001, 0xff800001,
    };
    static const uint64_t f64[] = {
        0, UINT64_C(0x8000000000000000), 1, UINT64_C(0x000fffffffffffff),
        UINT64_C(0x0010000000000000), UINT64_C(0x3ff0000000000000),
        UINT64_C(0x3ff000000fffffff), UINT64_C(0x3ff0000010000000),
        UINT64_C(0x3ff0000010000001), UINT64_C(0x3ff0000030000000),
        UINT64_C(0x7fefffffffffffff), UINT64_C(0xffefffffffffffff),
        UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000),
        UINT64_C(0x7ff8000000000000), UINT64_C(0x7ff0000000000001),
    };
    static const floatx80 f80[] = {
        { 0, 0 }, { 0, 0x8000 }, { 1, 0 },
        { UINT64_C(0x8000000000000000), 0 },
        { 1, 0x3fff }, { UINT64_MAX, 0x7ffe },
        { UINT64_C(0x8000000000000000), 0x7fff },
        { UINT64_C(0xc000000000000001), 0x7fff },
        { UINT64_C(0x8000000000000001), 0x7fff },
        { UINT64_C(0x8000000000000000), 0x3fff },
        { UINT64_C(0x8000000000000001), 0x3fff },
        { UINT64_C(0x8000007fffffffff), 0x3fff },
        { UINT64_C(0x8000008000000000), 0x3fff },
        { UINT64_C(0x8000008000000001), 0x3fff },
        { UINT64_C(0x80000000000003ff), 0x3fff },
        { UINT64_C(0x8000000000000400), 0x3fff },
        { UINT64_C(0x8000000000000401), 0x3fff },
        { UINT64_C(0xffffff0000000000), 0x3f80 },
        { UINT64_C(0xffffff8000000000), 0x407e },
        { UINT64_C(0xfffffffffffffc00), 0x43fe },
        { UINT64_C(0x8000000000000000), 0x3f81 },
        { UINT64_C(0x8000000000000000), 0x407f },
        { UINT64_C(0x8000000000000000), 0x3c00 },
        { UINT64_C(0x8000000000000000), 0x43ff },
    };
    static const uint16_t flags[] = {
        0, float_flag_invalid, float_flag_divbyzero, float_flag_overflow,
        float_flag_underflow, float_flag_inexact,
        float_flag_input_denormal_used, UINT16_MAX,
    };
    for (unsigned pc = 0; pc < 3; pc++) {
        for (unsigned rc = 0; rc < 4; rc++) {
            for (unsigned prior = 0; prior < sizeof(flags) / sizeof(*flags);
                 prior++) {
                for (unsigned mask = 0; mask < 64; mask++) {
                    CPUX86State env;
                    init(&env, pc, rc, flags[prior], (mask ^ 0x3f), mask);
                    env.fpstt = mask & 7;
                    for (unsigned i = 0; i < sizeof(f32) / sizeof(*f32); i++) {
                        run_case(&env, 0, f32[i], -1);
                        run_case(&env, 2, f32[i], -1);
                    }
                    for (unsigned i = 0; i < sizeof(f64) / sizeof(*f64); i++) {
                        run_case(&env, 1, f64[i], -1);
                        run_case(&env, 3, f64[i], -1);
                    }
                    for (unsigned i = 0; i < sizeof(f80) / sizeof(*f80); i++) {
                        env.fpregs[env.fpstt].d = f80[i];
                        run_case(&env, 4, 0, -1);
                        run_case(&env, 5, 0, -1);
                        env.fpregs[env.fpstt].d.high ^= 0x8000;
                        run_case(&env, 4, 0, -1);
                        run_case(&env, 5, 0, -1);
                    }
                }
            }
        }
    }
    printf("edge/mode/oldstatus cases=%" PRIu64 " pass\n", cases);
}
static void sticky_flags(void)
{
    CPUX86State env;
    init(&env, floatx80_precision_d, float_round_nearest_even, 0, FPUS_IE, 0);
    env.fpregs[env.fpstt].d = (floatx80) { UINT64_C(0x8000000000000000),
                                       0x3fff };
    for (unsigned flags = 0; flags <= UINT16_MAX; flags++) {
        env.fp_status.float_exception_flags = flags;
        run_case(&env, 0, 0x3f800000, XEMU_FPU_LS_NORMAL_EXACT_FAST);
        run_case(&env, 1, UINT64_C(0x3ff0000000000000),
                 XEMU_FPU_LS_NORMAL_EXACT_FAST);
        run_case(&env, 2, 0x3f800000, XEMU_FPU_LS_NORMAL_EXACT_FAST);
        run_case(&env, 3, UINT64_C(0x3ff0000000000000),
                 XEMU_FPU_LS_NORMAL_EXACT_FAST);
        run_case(&env, 4, 0, XEMU_FPU_LS_NORMAL_EXACT_FAST);
        run_case(&env, 5, 0, XEMU_FPU_LS_NORMAL_EXACT_FAST);
    }
    puts("all 65536 prior flag patterns x6 exact helpers pass "
         "incl pending SE/B");
}
static void pending_matrix(void)
{
    for (unsigned pc = 0; pc < 3; pc++) {
        for (unsigned rc = 0; rc < 4; rc++) {
            for (unsigned status = 0; status < 64; status++) {
                for (unsigned masks = 0; masks < 64; masks++) {
                    for (unsigned summary = 0; summary < 4; summary++) {
                        CPUX86State env;
                        uint16_t high = (summary & 1 ? FPUS_SE : 0) |
                                        (summary & 2 ? FPUS_B : 0);
                        init(&env, pc, rc, random32(), status | high, masks);
                        env.fpregs[env.fpstt].d = (floatx80) {
                            UINT64_C(0x8000000000000000), 0x3fff,
                        };
                        for (unsigned helper = 0; helper < 6; helper++) {
                            uint64_t value = helper & 1 ?
                                UINT64_C(0x3ff0000000000000) : 0x3f800000;
                            run_case(&env, helper, value, -1);
                        }
                    }
                }
            }
        }
    }
    puts("all 64x64 status/mask pairs x4 SE/B states xPC/RC/helpers pass");
}

static void random_cases(void)
{
    for (unsigned n = 0; n < 250000; n++) {
        CPUX86State env;
        init(&env, random32() % 3, random32() & 3, random32(),
             random32(), random32() & 0x3f);
        unsigned kind = random32() % 6;
        uint64_t bits = random64();
        env.fpregs[env.fpstt].d = (floatx80) { random64(), random32() };
        if (n % 4 == 0) {
            env.fpregs[env.fpstt].d = (floatx80) {
                (UINT64_C(1) << 63) | (random64() & ~UINT64_C(0x7ff)),
                0x3fff,
            };
        }
        run_case(&env, kind, bits, -1);
    }
    puts("250000 random full-state/encoding cases pass");
}
static void paths(void)
{
    CPUX86State env;
    init(&env, floatx80_precision_s, float_round_up, 0, 0, 0x3f);
    run_case(&env, 1, UINT64_C(0x3ff0000010000000),
             XEMU_FPU_LS_PRECISION_FALLBACK);
    run_case(&env, 3, UINT64_C(0x3ff0000010000000),
             XEMU_FPU_LS_PRECISION_FALLBACK);
    run_case(&env, 0, 0, XEMU_FPU_LS_NON_NORMAL_FALLBACK);
    run_case(&env, 2, 1, XEMU_FPU_LS_NON_NORMAL_FALLBACK);
    run_case(&env, 1, UINT64_C(0x7ff8000000000000),
             XEMU_FPU_LS_NON_NORMAL_FALLBACK);
    env.fpregs[env.fpstt].d = (floatx80) { UINT64_C(0x8000000000000400),
                                       0x3fff };
    run_case(&env, 4, 0, XEMU_FPU_LS_PRECISION_FALLBACK);
    run_case(&env, 5, 0, XEMU_FPU_LS_PRECISION_FALLBACK);
    env.fpregs[env.fpstt].d.high = 0x7fff;
    run_case(&env, 4, 0, XEMU_FPU_LS_NON_NORMAL_FALLBACK);
    run_case(&env, 5, 0, XEMU_FPU_LS_NON_NORMAL_FALLBACK);
    puts("actual transfer hook helper IDs/selected paths pass once per entry");
}
int main(void)
{
    sticky_flags();
    edges();
    pending_matrix();
    random_cases();
    paths();
    printf("total exact helper state checks=%" PRIu64 "\n", cases);
    return 0;
}
