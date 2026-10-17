/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include <errno.h>
#include <fenv.h>
#include "fpu/softfloat.h"
#include "fpu-profile.h"

/* Select the real generic floatx80 ABI before the production ARM gate. */
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
#define ST(n) (env->fpregs[(env->fpstt + (n)) & 7].d)
#define FPU_HELPER_COUNT() ((void)0)
#include "common.inc"
#include "reference.inc"
#include "current.inc"
/* Also check the numerical API when the production feature gate is OFF. */
#include "x87-native24.h"
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static uint64_t random_state = UINT64_C(0x9d7af3561042);
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
static uint64_t cases, api_cases, accepted[5];
static void numerical_case(int op, floatx80 a, floatx80 b)
{
    uint64_t low = UINT64_C(0x123456789abcdef0);
    uint16_t high = 0xa5a5;
    bool inexact = true;
    int old_exceptions = fetestexcept(FE_ALL_EXCEPT);
    int old_rounding = fegetround();
    errno = EDOM;
    enum X87Native24Result outcome = op == 4 ?
        x87n24_sqrt(a.low, a.high, &low, &high, &inexact) :
        x87n24_binop(op, a.low, a.high, b.low, b.high,
                      &low, &high, &inexact);
    assert(fegetround() == old_rounding && errno == EDOM);
    if (outcome != X87N24_HIT) {
        if (low != UINT64_C(0x123456789abcdef0) || high != 0xa5a5 ||
            !inexact || fetestexcept(FE_ALL_EXCEPT) != old_exceptions) {
            fprintf(stderr, "rejection changed output/host flags case=%"
                    PRIu64 " op=%d\n", api_cases, op);
            exit(1);
        }
    } else {
        float_status status = { 0 };
        set_float_2nan_prop_rule(float_2nan_prop_x87, &status);
        set_float_default_nan_pattern(0b11000000, &status);
        set_floatx80_rounding_precision(floatx80_precision_s, &status);
        set_float_rounding_mode(float_round_nearest_even, &status);
        floatx80 reference;
        switch (op) {
        case X87N_ADD:
            reference = floatx80_add(a, b, &status);
            break;
        case X87N_SUB:
            reference = floatx80_sub(a, b, &status);
            break;
        case X87N_MUL:
            reference = floatx80_mul(a, b, &status);
            break;
        case X87N_DIV:
            reference = floatx80_div(a, b, &status);
            break;
        case 4:
            reference = floatx80_sqrt(a, &status);
            break;
        default:
            abort();
        }
        int expected_flags = inexact ? float_flag_inexact : 0;
        if (low != reference.low || high != reference.high ||
            get_float_exception_flags(&status) != expected_flags ||
            old_rounding != FE_TONEAREST) {
            fprintf(stderr, "numerical mismatch case=%" PRIu64 " op=%d "
                    "a=%04x:%016" PRIx64 " b=%04x:%016" PRIx64 " "
                    "actual=%04x:%016" PRIx64 "/%u "
                    "reference=%04x:%016" PRIx64 "/%u\n",
                    api_cases, op, a.high, a.low, b.high, b.low,
                    high, low, expected_flags, reference.high,
                    reference.low, get_float_exception_flags(&status));
            exit(1);
        }
        accepted[op]++;
    }
    api_cases++;
}
static void init(CPUX86State *env, unsigned pc, unsigned rc,
                 unsigned sfpc, unsigned sfrc, uint16_t old_flags,
                 uint16_t fpus, uint16_t masks)
{
    uint8_t *p = (void *)env;
    for (unsigned i = 0; i < sizeof(*env); i++) {
        p[i] = random32();
    }
    memset(&env->fp_status, 0, sizeof(env->fp_status));
    memset(&env->mmx_status, 0, sizeof(env->mmx_status));
    memset(&env->sse_status, 0, sizeof(env->sse_status));
    cpu_init_fp_statuses(env);
    set_float_rounding_mode(sfrc, &env->fp_status);
    set_floatx80_rounding_precision(sfpc, &env->fp_status);
    set_float_exception_flags(old_flags, &env->fp_status);
    env->fpstt = random32() & 7;
    env->fpuc = (random32() & ~0xf3f) | pc << 8 | rc << 10 | masks;
    env->fpus = fpus;
}
static void normalize_padding(CPUX86State *env)
{
    for (unsigned i = 0; i < 9; i++) {
        floatx80 *p = i == 8 ? &env->ft0 : &env->fpregs[i].d;
        memset((char *)p + 10, 0, sizeof(*p) - 10);
    }
}
static void invoke(CPUX86State *env, unsigned helper, bool reference,
                   unsigned index)
{
#define CALL(n) do { \
    if (reference) { \
        orig_helper_##n(env); \
    } else { \
        helper_##n(env); \
    } \
} while (0)
#define CALL_N(n) do { \
    if (reference) { \
        orig_helper_##n(env, index); \
    } else { \
        helper_##n(env, index); \
    } \
} while (0)
    switch (helper) {
    case 0:
        CALL(fadd_ST0_FT0);
        break;
    case 1:
        CALL(fmul_ST0_FT0);
        break;
    case 2:
        CALL(fsub_ST0_FT0);
        break;
    case 3:
        CALL(fsubr_ST0_FT0);
        break;
    case 4:
        CALL(fdiv_ST0_FT0);
        break;
    case 5:
        CALL(fdivr_ST0_FT0);
        break;
    case 6:
        CALL_N(fadd_STN_ST0);
        break;
    case 7:
        CALL_N(fmul_STN_ST0);
        break;
    case 8:
        CALL_N(fsub_STN_ST0);
        break;
    case 9:
        CALL_N(fsubr_STN_ST0);
        break;
    case 10:
        CALL_N(fdiv_STN_ST0);
        break;
    case 11:
        CALL_N(fdivr_STN_ST0);
        break;
    case 12:
        CALL(fsqrt);
        break;
    default:
        abort();
    }
#undef CALL
#undef CALL_N
}
static void run_case(CPUX86State *initial, unsigned helper, unsigned index)
{
    CPUX86State a = *initial, b = *initial;
    int rounding = fegetround();
    errno = EDOM;
    invoke(&b, helper, true, index);
    int original_errno = errno;
    errno = EDOM;
    invoke(&a, helper, false, index);
    if (errno != original_errno || fegetround() != rounding) {
        fprintf(stderr, "host state changed case=%" PRIu64 " helper=%u\n",
                cases, helper);
        exit(1);
    }
    normalize_padding(&a);
    normalize_padding(&b);
    if (memcmp(&a, &b, sizeof(a))) {
        fprintf(stderr, "state mismatch case=%" PRIu64 " helper=%u "
                "CW=%x SW=%x PC=%u RC=%u hostRC=%d flags=%x index=%u "
                "ST0=%04x:%016" PRIx64 " FT0=%04x:%016" PRIx64 "\n",
                cases, helper, initial->fpuc, initial->fpus,
                initial->fp_status.floatx80_rounding_precision,
                initial->fp_status.float_rounding_mode, rounding,
                initial->fp_status.float_exception_flags, index,
                initial->fpregs[initial->fpstt].d.high,
                initial->fpregs[initial->fpstt].d.low,
                initial->ft0.high, initial->ft0.low);
        exit(1);
    }
    cases++;
    static const int operations[] = {
        X87N_ADD, X87N_MUL, X87N_SUB, X87N_SUB,
        X87N_DIV, X87N_DIV, X87N_ADD, X87N_MUL,
        X87N_SUB, X87N_SUB, X87N_DIV, X87N_DIV, 4,
    };
    floatx80 left = initial->fpregs[initial->fpstt].d;
    floatx80 right = helper >= 6 && helper < 12 ?
        initial->fpregs[(initial->fpstt + index) & 7].d : initial->ft0;
    if (helper == 3 || helper == 5 || helper == 6 || helper == 7 ||
        helper == 8 || helper == 10) {
        floatx80 swap = left;
        left = right;
        right = swap;
    }
    numerical_case(operations[helper], left, right);
}
static floatx80 number(uint64_t mantissa, int exponent, bool negative)
{
    return (floatx80) { mantissa,
                       (exponent + 16383) | (negative ? 0x8000 : 0) };
}
static const floatx80 edges[] = {
    { 0, 0 }, { 0, 0x8000 }, { 1, 0 },
    { UINT64_C(0x8000000000000000), 0 },
    { 1, 0x3fff }, { UINT64_MAX, 0x7ffe },
    { UINT64_C(0x8000000000000000), 0x7fff },
    { UINT64_C(0xc000000000000001), 0x7fff },
    { UINT64_C(0x8000000000000001), 0x7fff },
    { UINT64_C(0x8000000000000000), 0x3fff },
    { UINT64_C(0x8000010000000000), 0x3fff },
    { UINT64_C(0xffffff0000000000), 0x3fff },
    { UINT64_C(0x8000000000000000), 0x4000 },
    { UINT64_C(0xc000000000000000), 0x4000 },
    { UINT64_C(0x8000000000000001), 0x3fff },
    { UINT64_C(0x8000000000000800), 0x3fff },
    { UINT64_C(0x8000007fffffffff), 0x3fff },
    { UINT64_C(0x8000008000000000), 0x3fff },
    { UINT64_C(0x8000008000000001), 0x3fff },
    { UINT64_C(0x8000000000000000), 0x3fe7 },
    { UINT64_C(0x8000000000000000), 0x3fe6 },
    { UINT64_C(0x8000000000000001), 0x3fe7 },
    { UINT64_C(0x8000000000000000), 0x3f83 },
    { UINT64_C(0x8000000000000000), 0x407b },
    { UINT64_C(0x8000000000000000), 0x3fc2 },
    { UINT64_C(0x8000000000000000), 0x3fc3 },
    { UINT64_C(0x8000000000000000), 0x403b },
    { UINT64_C(0x8000000000000000), 0x403c },
    { UINT64_C(0x8000000000000000), 0x3fc9 },
    { UINT64_C(0x8000000000000000), 1 },
    { UINT64_C(0x8000000000000000), 0x7ffe },
};
static const unsigned soft_rc[] = {
    float_round_nearest_even, float_round_down,
    float_round_up, float_round_to_zero,
};
static const int host_rc[] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD,
                               FE_TOWARDZERO };
static void edge_modes(void)
{
    for (unsigned hrc = 0; hrc < 4; hrc++) {
        assert(fesetround(host_rc[hrc]) == 0);
        for (unsigned pc = 0; pc < 4; pc++) {
            for (unsigned rc = 0; rc < 4; rc++) {
                for (unsigned sfpc = 0; sfpc < 3; sfpc++) {
                    for (unsigned sfrc = 0; sfrc < 4; sfrc++) {
                        CPUX86State env;
                        init(&env, pc, rc, sfpc, soft_rc[sfrc], random32(),
                             random32(), random32() & 63);
                        for (unsigned i = 0; i < ARRAY_SIZE(edges); i++) {
                            env.fpregs[env.fpstt].d = edges[i];
                            env.ft0 = edges[(i * 7 + 5) % ARRAY_SIZE(edges)];
                            env.fpregs[(env.fpstt + 3) & 7].d = env.ft0;
                            for (unsigned op = 0; op < 13; op++) {
                                run_case(&env, op, 3);
                            }
                        }
                    }
                }
            }
        }
    }
    assert(fesetround(FE_TONEAREST) == 0);
    printf("all independent guest/softfloat/host modes cases=%" PRIu64
           " pass\n", cases);
}
static void pair_edges(void)
{
    for (unsigned i = 0; i < ARRAY_SIZE(edges); i++) {
        for (unsigned j = 0; j < ARRAY_SIZE(edges); j++) {
            for (unsigned sign = 0; sign < 4; sign++) {
                CPUX86State env;
                init(&env, 0, 0, floatx80_precision_s,
                     float_round_nearest_even, random32(), random32(),
                     random32() & 63);
                env.fpregs[env.fpstt].d = edges[i];
                env.ft0 = edges[j];
                env.fpregs[env.fpstt].d.high ^= (sign & 1) << 15;
                env.ft0.high ^= (sign >> 1) << 15;
                env.fpregs[(env.fpstt + 1) & 7].d = env.ft0;
                for (unsigned op = 0; op < 13; op++) {
                    run_case(&env, op, 1);
                }
            }
        }
    }
    puts("adversarial edge cross-product / both signs pass");
}
static void sticky_and_pending(void)
{
    CPUX86State env;
    init(&env, 0, 0, floatx80_precision_s, float_round_nearest_even,
         0, FPUS_IE, 0);
    env.fpregs[env.fpstt].d = number(UINT64_C(1) << 63, 0, false);
    env.ft0 = env.fpregs[env.fpstt].d;
    env.fpregs[(env.fpstt + 1) & 7].d = env.ft0;
    for (unsigned flags = 0; flags <= UINT16_MAX; flags++) {
        env.fp_status.float_exception_flags = flags;
        for (unsigned op = 0; op < 13; op++) {
            run_case(&env, op, 1);
        }
    }
    for (unsigned status = 0; status < 64; status++) {
        for (unsigned mask = 0; mask < 64; mask++) {
            for (unsigned summary = 0; summary < 4; summary++) {
                env.fpus = status | (summary & 1 ? FPUS_SE : 0) |
                           (summary & 2 ? FPUS_B : 0);
                env.fpuc = (env.fpuc & ~63) | mask;
                env.fp_status.float_exception_flags = random32();
                for (unsigned op = 0; op < 13; op++) {
                    run_case(&env, op, 1);
                }
            }
        }
    }
    puts("all 65536 prior flags x13 helpers and 64x64x4 pending/mask pass");
}
static void random_cases(void)
{
    for (unsigned i = 0; i < 350000; i++) {
        CPUX86State env;
        unsigned pc = floatx80_precision_s;
        if (!(i % 4)) {
            pc = random32() % 3;
        }
        init(&env, i % 4 ? 0 : random32() & 3, random32() % 8 ? 0 :
             random32() & 3, pc,
             i % 4 ? float_round_nearest_even : soft_rc[random32() & 3],
             random32(), random32(), random32() & 63);
        floatx80 a = { random64(), random32() };
        floatx80 b = { random64(), random32() };
        if (i % 5) {
            uint64_t mask = i % 5 == 1 ? UINT64_MAX : i % 5 == 2 ?
                            ~UINT64_C(0x7ff) : ~UINT64_C(0xffffffffff);
            a = number((random64() & mask) | (UINT64_C(1) << 63),
                       (int)(random32() % 127) - 63, random32() & 1);
            b = number((random64() & mask) | (UINT64_C(1) << 63),
                       (int)(random32() % 127) - 63, random32() & 1);
        }
        env.fpregs[env.fpstt].d = a;
        env.ft0 = b;
        unsigned index = random32() & 7;
        env.fpregs[(env.fpstt + index) & 7].d = b;
        run_case(&env, random32() % 13, index);
    }
    puts("350000 deterministic mixed-precision full-state cases pass");
}
static void branch_admission(void)
{
    CPUX86State env;
    floatx80 one = number(UINT64_C(1) << 63, 0, false);
    bool expected = X87_NATIVE24_ACTIVE &&
                    getenv("XEMU_TCG_NATIVE_X87")[0] != '0';

    for (unsigned op = 0; op < 5; op++) {
        init(&env, 0, 0, floatx80_precision_s, float_round_nearest_even,
             0, FPUS_IE, 0);
        floatx80 output = one;
        bool actual = op == 4 ? x87_native_sqrt(&env, &output) :
                      x87_native_binop(&env, op, one, one, &output);
        assert(actual == expected);
        if (actual) {
            assert((env.fpus & (FPUS_SE | FPUS_B)) == (FPUS_SE | FPUS_B));
        }
    }
    puts("five actual wrapper admission/gate/policy checks pass");
}
static void directed_rejections(void)
{
    for (unsigned mode = 0; mode < 4; mode++) {
        assert(fesetround(host_rc[mode]) == 0);
        for (unsigned kind = 0; kind < 8; kind++) {
            floatx80 a = number(UINT64_C(1) << 63, 0, false);
            floatx80 b = a;
            int op = X87N_DIV;
            if (!mode) {
                switch (kind) {
                case 0:
                    a.low++;
                    break;
                case 1:
                    b.low++;
                    break;
                case 2:
                    a.high += 61;
                    break;
                case 3:
                    b.high -= 61;
                    break;
                case 4:
                    b.low = 0;
                    b.high = 0;
                    break;
                case 5:
                    a.high |= 0x8000;
                    op = 4;
                    break;
                case 6:
                    a.low = 0;
                    break;
                case 7:
                    op = 5;
                    break;
                }
            }
            assert(feclearexcept(FE_ALL_EXCEPT) == 0);
            assert(feraiseexcept(FE_INVALID) == 0);
            uint64_t before = 0, after = 0;
            for (unsigned i = 0; i < 5; i++) {
                before += accepted[i];
            }
            numerical_case(op, a, b);
            for (unsigned i = 0; i < 5; i++) {
                after += accepted[i];
            }
            assert(before == after);
            assert(fetestexcept(FE_ALL_EXCEPT) == FE_INVALID);
        }
    }
    assert(fesetround(FE_TONEAREST) == 0);
    assert(feclearexcept(FE_ALL_EXCEPT) == 0);
    puts("32 clean/seeded host-FE rejection cases pass");
}
static void double_rounding_witness(void)
{
    numerical_case(X87N_ADD,
                   number(UINT64_C(0x8000008000000000), 0, false),
                   number(UINT64_C(0x8000000000000000), -54, false));
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "double-rounding-witness")) {
        double_rounding_witness();
        return 0;
    }
    double_rounding_witness();
    edge_modes();
    pair_edges();
    sticky_and_pending();
    random_cases();
    branch_admission();
    directed_rejections();
    printf("complete helper state comparisons=%" PRIu64 " pass\n", cases);
    printf("direct numerical/reject comparisons=%" PRIu64 " pass; "
           "accepted add/sub/mul/div/sqrt=%" PRIu64 "/%" PRIu64 "/%"
           PRIu64 "/%" PRIu64 "/%" PRIu64 "\n", api_cases, accepted[0],
           accepted[1], accepted[2], accepted[3], accepted[4]);
    return 0;
}
