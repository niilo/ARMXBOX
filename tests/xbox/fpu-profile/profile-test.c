/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Actual helper/profile bodies; clock and Android log boundaries are stubs. */
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fpu-profile.h"
#define x87n_binop test_x87n_binop_impl
#define x87n_sqrt test_x87n_sqrt_impl
#include "x87-native.h"
#undef x87n_binop
#undef x87n_sqrt
#define x87n24_binop test_x87n24_binop_impl
#define x87n24_sqrt test_x87n24_sqrt_impl
#include "x87-native24.h"
#undef x87n24_binop
#undef x87n24_sqrt

static unsigned binary_attempts, sqrt_attempts, policy_calls;
static unsigned binary24_attempts, sqrt24_attempts;
static bool x87n_binop(int op, uint64_t al, uint16_t ah,
                       uint64_t bl, uint16_t bh, uint64_t *rl,
                       uint16_t *rh, bool *inexact)
{
    binary_attempts++;
    return test_x87n_binop_impl(op, al, ah, bl, bh, rl, rh, inexact);
}

static bool x87n_sqrt(uint64_t low, uint16_t high, uint64_t *rl,
                      uint16_t *rh, bool *inexact)
{
    sqrt_attempts++;
    return test_x87n_sqrt_impl(low, high, rl, rh, inexact);
}

static enum X87Native24Result
x87n24_binop(int op, uint64_t al, uint16_t ah, uint64_t bl, uint16_t bh,
            uint64_t *rl, uint16_t *rh, bool *inexact)
{
    binary24_attempts++;
    return test_x87n24_binop_impl(op, al, ah, bl, bh, rl, rh, inexact);
}

static enum X87Native24Result
x87n24_sqrt(uint64_t low, uint16_t high, uint64_t *rl, uint16_t *rh,
           bool *inexact)
{
    sqrt24_attempts++;
    return test_x87n24_sqrt_impl(low, high, rl, rh, inexact);
}

typedef struct TestClock {
    uint64_t wall, cpu, reads;
    unsigned fail_at;
} TestClock;
static __thread TestClock test_clock;
static char messages[8][2048];
static unsigned reports;
static bool log_changes_fenv;

static int test_clock_gettime(clockid_t which, struct timespec *ts)
{
    uint64_t ns;

    test_clock.reads++;
    errno = EAGAIN;
    if (test_clock.fail_at == test_clock.reads) {
        return -1;
    }
    assert(which == CLOCK_MONOTONIC || which == CLOCK_THREAD_CPUTIME_ID);
    ns = which == CLOCK_MONOTONIC ? test_clock.wall : test_clock.cpu;
    ts->tv_sec = ns / 1000000000;
    ts->tv_nsec = ns % 1000000000;
    return 0;
}

int __android_log_print(int priority, const char *tag, const char *fmt, ...)
{
    va_list args;

    (void)priority;
    assert(reports < 8);
    snprintf(messages[reports], sizeof(messages[reports]), "%s ", tag);
    va_start(args, fmt);
    vsnprintf(messages[reports] + strlen(messages[reports]),
              sizeof(messages[reports]) - strlen(messages[reports]), fmt, args);
    va_end(args);
    reports++;
    if (log_changes_fenv) {
        feraiseexcept(FE_DIVBYZERO);
        fesetround(FE_DOWNWARD);
        errno = ERANGE;
    }
    return 0;
}

#ifndef PROFILE_REAL_CLOCKS
#define clock_gettime test_clock_gettime
#endif
#include "profile-core.inc"
#undef clock_gettime

typedef struct TestFloat80 {
    uint64_t low;
    uint16_t high;
} floatx80;
typedef uint32_t float32;
typedef struct TestFloatStatus {
    int flags;
    int floatx80_rounding_precision;
    int float_rounding_mode;
} float_status;
enum {
    floatx80_precision_x,
    floatx80_precision_d,
    floatx80_precision_s,
};
enum { float_round_nearest_even };
static int get_floatx80_rounding_precision(const float_status *s)
{
    return s->floatx80_rounding_precision;
}
static int get_float_rounding_mode(const float_status *s)
{
    return s->float_rounding_mode;
}
typedef struct TestFpuRegister {
    floatx80 d;
} TestFpuRegister;
typedef struct CPUX86State {
    TestFpuRegister fpregs[8];
    floatx80 ft0;
    int fpstt;
    uint8_t fptags[8];
    uint16_t fpuc, fpus;
    float_status fp_status;
} CPUX86State;
typedef union Reg {
    uint32_t l[8];
    uint64_t q[4];
} Reg;

#define FT0 (env->ft0)
#define ST0 (env->fpregs[env->fpstt].d)
#define ST(n) (env->fpregs[(env->fpstt + (n)) & 7].d)
#define ST1 ST(1)
#define FPREG_VAL(r) ((r).d)
#define ZMM_S(i) l[i]
#define ZMM_L(i) l[i]
#define ZMM_D(i) q[i]
#define ZMM_Q(i) q[i]
#define FPU_ADD(size, a, b) float32_add_native(a, b)
#define X87N_FPUC_MASK 0x0f00
#define X87N_FPUC_MODE 0x0200
#define FPUS_PE 0x20
#define float_flag_inexact 0x10
static bool native_policy = true;
static bool x87_native_enabled(void)
{
    policy_calls++;
    return native_policy;
}
static void float_raise(int flags, float_status *s)
{
    s->flags |= flags;
}
static void fpu_set_exception(CPUX86State *env, int flags)
{
    env->fpus |= flags;
}

#include "helpers.inc"

static __attribute__((noinline)) void empty_scope(void)
{
    XEMU_FPU_PROFILE_SCOPE(XEMU_FPU_X87_OTHER);
}

static void seed(CPUX86State *env)
{
    memset(env, 0, sizeof(*env));
    env->fpstt = 5;
    env->fpuc = X87N_FPUC_MODE;
    env->fp_status.floatx80_rounding_precision = floatx80_precision_d;
    for (int i = 0; i < 8; i++) {
        env->fpregs[i].d.low = UINT64_C(0x8000000000000000) + i;
        env->fpregs[i].d.high = 0x3fff + i;
        env->fptags[i] = i & 1;
    }
    env->ft0.low = 1234;
    env->ft0.high = 0x4567;
}

static void check_helper_state(void)
{
    CPUX86State old, now;

    seed(&old);
    now = old;
    for (int i = 0; i < 512; i++) {
        int n = i & 7;

        orig_helper_fpush(&old);
        helper_fpush(&now);
        orig_helper_fmov_FT0_STN(&old, n);
        helper_fmov_FT0_STN(&now, n);
        orig_helper_fmov_ST0_STN(&old, (n + 3) & 7);
        helper_fmov_ST0_STN(&now, (n + 3) & 7);
        orig_helper_fmov_STN_ST0(&old, n);
        helper_fmov_STN_ST0(&now, n);
        orig_helper_fxchg_ST0_STN(&old, n);
        helper_fxchg_ST0_STN(&now, n);
        orig_helper_fpop(&old);
        helper_fpop(&now);
        assert(memcmp(&old, &now, sizeof(old)) == 0);
    }
    for (int alias = 0; alias < 4; alias++) {
        Reg a[3] = { 0 }, b[3];
        int di = alias == 1 || alias == 3 ? 1 : 0;
        int si = alias == 2 || alias == 3 ? 1 : 2;

        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 8; j++) {
                a[i].l[j] = 0x3f800000 + i * 0x800000 + j;
            }
        }
        memcpy(b, a, sizeof(a));
        orig_helper_addss(&old, &a[di], &a[1], &a[si]);
        helper_addss(&now, &b[di], &b[1], &b[si]);
        assert(memcmp(a, b, sizeof(a)) == 0);
    }
}

static void check_lazy_detail_hooks(void)
{
    unsigned helper_evals = 0, path_evals = 0, argument_evals = 0;
    unsigned kind_evals = 0, reason_evals = 0;
    XEMU_FPU_PROFILE_POLICY_STATE;

    xemu_fpu_profile_init(false);
    XEMU_FPU_PROFILE_TRANSFER((helper_evals++, XEMU_FPU_LS_FLDS_FT0),
                              (path_evals++, XEMU_FPU_LS_GENERIC));
    XEMU_FPU_PROFILE_REJECT_BINOP((argument_evals++, 0x200), 0,
                                  0, 0, 0, 0);
    XEMU_FPU_PROFILE_REJECT_SQRT((argument_evals++, 0x200), 0, 0);
    XEMU_FPU_PROFILE_REJECT_DIRECT((kind_evals++, XEMU_FPU_REJECT_BINOP),
                                   (reason_evals++, XEMU_FPU_REJECT_HOST_ENV));
    XEMU_FPU_PROFILE_REJECT_PC24_BINOP((argument_evals++, 0), 0, 0, 0, 0);
    XEMU_FPU_PROFILE_REJECT_PC24_SQRT((argument_evals++, 0), 0);
    assert(!helper_evals && !path_evals && !argument_evals);
    assert(!kind_evals && !reason_evals);
    assert(XEMU_FPU_PROFILE_POLICY((argument_evals++, true)));
    assert(argument_evals == 1); /* original policy expression still runs */
}

#if XEMU_FPU_PROFILE_ACTIVE
static void reset_test(void)
{
    memset(&xemu_fpu_profile_thread, 0, sizeof(xemu_fpu_profile_thread));
    test_clock = (TestClock) { .wall = 1000000000, .cpu = 100000000 };
    memset(messages, 0, sizeof(messages));
    reports = 0;
    log_changes_fenv = false;
}

static void check_sampling_and_disable(void)
{
    reset_test();
    xemu_fpu_profile_init(false);
    for (int i = 0; i < 3000; i++) {
        empty_scope();
    }
    assert(!test_clock.reads && !reports);
    assert(!xemu_fpu_profile_thread.totals.entries[XEMU_FPU_X87_OTHER]);
    xemu_fpu_profile_init(true);
    for (int i = 0; i < 2050; i++) {
        errno = EDOM;
        empty_scope();
        assert(errno == EDOM);
    }
    assert(xemu_fpu_profile_thread.totals.entries[XEMU_FPU_X87_OTHER] == 2050);
    assert(xemu_fpu_profile_thread.totals.samples[XEMU_FPU_X87_OTHER] == 3);
    assert(test_clock.reads == 12 && !reports);
}

static void check_native_outcomes(void)
{
    CPUX86State env_storage, *env = &env_storage;
    floatx80 a = { UINT64_C(0x8000000000000000), 0x3fff };
    floatx80 b = a, out = { 0 }, unchanged;
    int argument_calls = 0;

    reset_test();
    seed(env);
    native_policy = true;
    assert(X87_NATIVE_BINOP((argument_calls++, X87N_ADD), a, b, &out));
    assert(argument_calls == 1 && out.high == 0x4000);
    a.low |= 1;
    unchanged = out;
    assert(!X87_NATIVE_BINOP(X87N_ADD, a, b, &out));
    assert(memcmp(&unchanged, &out, sizeof(out)) == 0);
    a.low &= ~UINT64_C(1);
    native_policy = false;
    assert(!X87_NATIVE_BINOP(X87N_ADD, a, b, &out));
    assert(memcmp(&unchanged, &out, sizeof(out)) == 0);
    assert(xemu_fpu_profile_thread.totals.native_hit[XEMU_FPU_NATIVE_BINOP]
           == 1);
    assert(xemu_fpu_profile_thread.totals.native_fallback[XEMU_FPU_NATIVE_BINOP]
           == 2);
    native_policy = true;
}

static void check_transfer_cells(void)
{
    unsigned helper_evals = 0, path_evals = 0;
    unsigned kind_evals = 0, reason_evals = 0;

    reset_test();
    for (int i = 0; i < XEMU_FPU_LS_COUNT; i++) {
        for (int j = 0; j < XEMU_FPU_LS_PATH_COUNT; j++) {
            for (int n = 0; n <= j; n++) {
                XEMU_FPU_PROFILE_TRANSFER((helper_evals++, i),
                                          (path_evals++, j));
            }
            assert(xemu_fpu_profile_thread.totals.transfer[i][j]
                   == (uint64_t)j + 1);
        }
    }
    assert(helper_evals == XEMU_FPU_LS_COUNT * 10);
    assert(path_evals == helper_evals);
    XEMU_FPU_PROFILE_REJECT_DIRECT((kind_evals++, XEMU_FPU_REJECT_SQRT),
                                   (reason_evals++, XEMU_FPU_REJECT_PC24_ENV));
    assert(kind_evals == 1 && reason_evals == 1);
    assert(xemu_fpu_profile_thread.totals.reject[XEMU_FPU_REJECT_SQRT]
           [XEMU_FPU_REJECT_PC24_ENV] == 1);
    assert(!test_clock.reads && !reports); /* no extra timing from details */
}

static void check_rejected_status_case(enum XemuFpuProfileRejectKind kind,
                                       uint16_t fpuc, bool policy, int op,
                                       floatx80 a, floatx80 b, int bad_status,
                                       enum XemuFpuProfileRejectReason expected)
{
    CPUX86State env_storage, *env = &env_storage;
    floatx80 reference = { 0 };
    uint16_t reference_fpus = 0;
    int reference_flags = 0, reference_fenv = 0, reference_errno = 0;
    int reference_round = 0;
    bool reference_hit = false;

    for (int enabled = 0; enabled <= 1; enabled++) {
        floatx80 out = kind == XEMU_FPU_REJECT_SQRT ? a : b;
        bool hit;
        bool pc53 = (fpuc & 0xf00) == 0x200;
        bool pc24 = X87_NATIVE24_ACTIVE && !(fpuc & 0xf00);
        unsigned expected_policy = pc53 || (pc24 && !bad_status);

        reset_test();
        xemu_fpu_profile_init(enabled);
        seed(env);
        env->fpuc = fpuc;
        env->fp_status.floatx80_rounding_precision =
            pc24 ? floatx80_precision_s : floatx80_precision_d;
        if (bad_status == 1) {
            env->fp_status.floatx80_rounding_precision = floatx80_precision_d;
        } else if (bad_status == 2) {
            env->fp_status.float_rounding_mode = 1;
        }
        native_policy = policy;
        binary_attempts = sqrt_attempts = policy_calls = 0;
        binary24_attempts = sqrt24_attempts = 0;
        feclearexcept(FE_ALL_EXCEPT);
        errno = EDOM;
        hit = kind == XEMU_FPU_REJECT_BINOP ?
              X87_NATIVE_BINOP(op, a, b, &out) : X87_NATIVE_SQRT(&out);
        assert(policy_calls == expected_policy);
        assert(binary_attempts == (unsigned)(kind == XEMU_FPU_REJECT_BINOP &&
                                            pc53 && policy));
        assert(sqrt_attempts == (unsigned)(kind == XEMU_FPU_REJECT_SQRT &&
                                          pc53 && policy));
        assert(binary24_attempts ==
               (unsigned)(kind == XEMU_FPU_REJECT_BINOP && pc24 &&
                          expected_policy && policy));
        assert(sqrt24_attempts ==
               (unsigned)(kind == XEMU_FPU_REJECT_SQRT && pc24 &&
                          expected_policy && policy));
        assert(hit == (expected == XEMU_FPU_REJECT_REASON_COUNT));
        if (!enabled) {
            reference = out;
            reference_fpus = env->fpus;
            reference_flags = env->fp_status.flags;
            reference_fenv = fetestexcept(FE_ALL_EXCEPT);
            reference_round = fegetround();
            reference_errno = errno;
            reference_hit = hit;
        } else {
            uint64_t reasons = 0;

            assert(out.low == reference.low && out.high == reference.high);
            assert(env->fpus == reference_fpus);
            assert(env->fp_status.flags == reference_flags);
            assert(fetestexcept(FE_ALL_EXCEPT) == reference_fenv);
            assert(fegetround() == reference_round);
            assert(errno == reference_errno && hit == reference_hit);
            for (int i = 0; i < XEMU_FPU_REJECT_REASON_COUNT; i++) {
                reasons += xemu_fpu_profile_thread.totals.reject[kind][i];
            }
            assert(reasons == !hit);
            if (!hit) {
                assert(xemu_fpu_profile_thread.totals.reject[kind][expected]
                       == 1);
            }
            assert(xemu_fpu_profile_thread.totals.native_fallback[kind]
                   == !hit);
            assert(xemu_fpu_profile_thread.totals.native_hit[kind] == hit);
        }
        assert(!test_clock.reads && !reports);
    }
    native_policy = true;
}

static void check_rejected_case(enum XemuFpuProfileRejectKind kind,
                                uint16_t fpuc, bool policy, int op,
                                floatx80 a, floatx80 b,
                                enum XemuFpuProfileRejectReason expected)
{
    check_rejected_status_case(kind, fpuc, policy, op, a, b, 0, expected);
}

static void check_rejection_reasons(void)
{
    const floatx80 one = { UINT64_C(0x8000000000000000), 0x3fff };
    const floatx80 zero = { 0, 0 };
    const floatx80 negative_zero = { 0, 0x8000 };
    const floatx80 negative = { UINT64_C(0x8000000000000000), 0xbfff };
    const floatx80 large = { UINT64_C(0x8000000000000000), 16383 + 900 };
    const floatx80 small = { UINT64_C(0x8000000000000000), 16383 - 900 };
    const floatx80 rejected[] = {
        { 1, 0 },
        { UINT64_C(0x8000000000000000), 0 },
        { 1, 0x3fff },
        { UINT64_C(0x8000000000000000), 0x7fff },
        { UINT64_C(0xc000000000000000), 0x7fff },
        { UINT64_C(0x8000000000000001), 0x3fff },
        { UINT64_C(0x8000000000000000), 16383 - 901 },
        { UINT64_C(0x8000000000000000), 16383 + 901 },
    };
    const int host_rounding[] = {
        FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO,
    };

    for (int h = 0; h < 4; h++) {
        assert(fesetround(host_rounding[h]) == 0);
        for (unsigned pc = 0; pc < 4; pc++) {
            for (unsigned rc = 0; rc < 4; rc++) {
                enum XemuFpuProfileRejectReason expected =
                    pc == 0 && !X87_NATIVE24_ACTIVE ? XEMU_FPU_REJECT_PC24 :
                    pc == 1 ? XEMU_FPU_REJECT_PC_RESERVED :
                    pc == 3 ? XEMU_FPU_REJECT_PC64 :
                    rc ? XEMU_FPU_REJECT_ROUND_DOWN + rc - 1 :
                    pc == 0 && h ? XEMU_FPU_REJECT_HOST_ENV :
                         XEMU_FPU_REJECT_REASON_COUNT;

                check_rejected_case(XEMU_FPU_REJECT_BINOP,
                                    (pc << 8) | (rc << 10), true, X87N_ADD,
                                    one, one, expected);
                check_rejected_case(XEMU_FPU_REJECT_SQRT,
                                    (pc << 8) | (rc << 10), true, 0,
                                    one, zero, expected);
            }
        }
        for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++) {
            check_rejected_case(XEMU_FPU_REJECT_BINOP, 0x200, true,
                                X87N_ADD, rejected[i], one,
                                XEMU_FPU_REJECT_A_DENORMAL + i);
            check_rejected_case(XEMU_FPU_REJECT_BINOP, 0x200, true,
                                X87N_ADD, one, rejected[i],
                                XEMU_FPU_REJECT_B_DENORMAL + i);
            check_rejected_case(XEMU_FPU_REJECT_SQRT, 0x200, true,
                                0, rejected[i], zero,
                                XEMU_FPU_REJECT_A_DENORMAL + i);
        }
        check_rejected_case(XEMU_FPU_REJECT_BINOP, 0x200, false,
                            X87N_ADD, one, one, XEMU_FPU_REJECT_POLICY_OFF);
        check_rejected_case(XEMU_FPU_REJECT_SQRT, 0x200, false,
                            0, one, zero, XEMU_FPU_REJECT_POLICY_OFF);
        check_rejected_case(XEMU_FPU_REJECT_BINOP, 0x200, true,
                            X87N_DIV, one, negative_zero,
                            XEMU_FPU_REJECT_DIV_ZERO);
        check_rejected_case(XEMU_FPU_REJECT_BINOP, 0x200, true,
                            99, one, one, XEMU_FPU_REJECT_INVALID_OP);
        check_rejected_case(XEMU_FPU_REJECT_BINOP, 0x200, true,
                            X87N_MUL, large, large, XEMU_FPU_REJECT_RESULT);
        check_rejected_case(XEMU_FPU_REJECT_BINOP, 0x200, true,
                            X87N_DIV, small, large, XEMU_FPU_REJECT_RESULT);
        check_rejected_case(XEMU_FPU_REJECT_SQRT, 0x200, true,
                            0, negative, zero, XEMU_FPU_REJECT_SQRT_NEGATIVE);
        check_rejected_case(XEMU_FPU_REJECT_SQRT, 0x200, true,
                            0, negative_zero, zero,
                            XEMU_FPU_REJECT_REASON_COUNT);
    }
    fesetround(FE_TONEAREST);
    feclearexcept(FE_ALL_EXCEPT);
}

static void check_pc24_rejection_reasons(void)
{
    const floatx80 one = { UINT64_C(0x8000000000000000), 0x3fff };
    const floatx80 zero = { 0, 0 };
    const floatx80 negative_zero = { 0, 0x8000 };
    const floatx80 negative = { UINT64_C(0x8000000000000000), 0xbfff };
    const floatx80 rejected[] = {
        { 1, 0 },
        { UINT64_C(0x8000000000000000), 0 },
        { 1, 0x3fff },
        { UINT64_C(0x8000000000000000), 0x7fff },
        { UINT64_C(0xc000000000000000), 0x7fff },
        { UINT64_C(0x8000008000000000), 0x3fff },
        { UINT64_C(0x8000000000000000), 16383 - 61 },
        { UINT64_C(0x8000000000000000), 16383 + 61 },
    };
    const floatx80 accepted[] = {
        { 0, 0 },
        { 0, 0x8000 },
        { UINT64_C(0x8000000000000000), 16383 - 60 },
        { UINT64_C(0xffffff0000000000), 16383 + 60 },
        { UINT64_C(0x8000010000000000), 0x3fff },
    };
    const int host_rounding[] = {
        FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO,
    };

    if (!X87_NATIVE24_ACTIVE) {
        return;
    }
    for (int h = 0; h < 4; h++) {
        enum XemuFpuProfileRejectReason ordinary =
            h ? XEMU_FPU_REJECT_HOST_ENV : XEMU_FPU_REJECT_REASON_COUNT;

        assert(fesetround(host_rounding[h]) == 0);
        for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++) {
            check_rejected_case(XEMU_FPU_REJECT_BINOP, 0, true,
                                X87N_ADD, rejected[i], one,
                                XEMU_FPU_REJECT_A_DENORMAL + i);
            check_rejected_case(XEMU_FPU_REJECT_BINOP, 0, true,
                                X87N_ADD, one, rejected[i],
                                XEMU_FPU_REJECT_B_DENORMAL + i);
            check_rejected_case(XEMU_FPU_REJECT_SQRT, 0, true,
                                0, rejected[i], zero,
                                XEMU_FPU_REJECT_A_DENORMAL + i);
        }
        for (unsigned i = 0; i < sizeof(accepted) / sizeof(accepted[0]); i++) {
            check_rejected_case(XEMU_FPU_REJECT_BINOP, 0, true,
                                X87N_ADD, accepted[i], one, ordinary);
            check_rejected_case(XEMU_FPU_REJECT_SQRT, 0, true,
                                0, accepted[i], zero, ordinary);
        }
        for (int kind = 0; kind < XEMU_FPU_REJECT_KIND_COUNT; kind++) {
            for (int bad_status = 1; bad_status <= 2; bad_status++) {
                check_rejected_status_case(kind, 0, false, X87N_ADD,
                                           one, one, bad_status,
                                           XEMU_FPU_REJECT_PC24_ENV);
                check_rejected_status_case(kind, 0, true, X87N_ADD,
                                           one, one, bad_status,
                                           XEMU_FPU_REJECT_PC24_ENV);
            }
            check_rejected_case(kind, 0, false, X87N_ADD, one, one,
                                XEMU_FPU_REJECT_POLICY_OFF);
        }
        check_rejected_case(XEMU_FPU_REJECT_BINOP, 0, true,
                            X87N_DIV, one, negative_zero,
                            XEMU_FPU_REJECT_DIV_ZERO);
        check_rejected_case(XEMU_FPU_REJECT_BINOP, 0, true,
                            99, one, one, XEMU_FPU_REJECT_INVALID_OP);
        check_rejected_case(XEMU_FPU_REJECT_SQRT, 0, true,
                            0, negative, zero, XEMU_FPU_REJECT_SQRT_NEGATIVE);
        check_rejected_case(XEMU_FPU_REJECT_SQRT, 0, true,
                            0, negative_zero, zero, ordinary);
    }
    fesetround(FE_TONEAREST);
    feclearexcept(FE_ALL_EXCEPT);
}

static void check_detail_report_bounds(void)
{
    reset_test();
    memset(xemu_fpu_profile_thread.totals.transfer, 0xff,
           sizeof(xemu_fpu_profile_thread.totals.transfer));
    memset(xemu_fpu_profile_thread.totals.reject, 0xff,
           sizeof(xemu_fpu_profile_thread.totals.reject));
    xemu_fpu_profile_thread.report_sequence = UINT64_MAX - 1;
    empty_scope();
    test_clock.wall += UINT64_C(5000000000);
    for (int i = 0; i < 1024; i++) {
        empty_scope();
    }
    assert(reports == 6);
    for (int i = 0; i < 6; i++) {
        assert(strstr(messages[i], "seq=18446744073709551615"));
    }
    assert(strstr(messages[3], "xemu-cpu-ls"));
    assert(strstr(messages[3], "fbstST=18446744073709551615/"
                  "18446744073709551615/18446744073709551615/"
                  "18446744073709551615"));
    assert(strstr(messages[4], "kind=bin"));
    assert(strstr(messages[5], "kind=sqrt"));
    assert(strstr(messages[4], "v=3"));
    assert(strstr(messages[4], "badOp=18446744073709551615"));
    assert(strstr(messages[5], "badOp=18446744073709551615"));
    assert(strstr(messages[4], "pc24Env=18446744073709551615"));
    assert(strstr(messages[5], "hostEnv=18446744073709551615"));
    for (int i = 0; i < XEMU_FPU_LS_COUNT; i++) {
        for (int j = 0; j < XEMU_FPU_LS_PATH_COUNT; j++) {
            assert(!xemu_fpu_profile_thread.totals.transfer[i][j]);
        }
    }
    for (int i = 0; i < XEMU_FPU_REJECT_KIND_COUNT; i++) {
        for (int j = 0; j < XEMU_FPU_REJECT_REASON_COUNT; j++) {
            assert(!xemu_fpu_profile_thread.totals.reject[i][j]);
        }
    }
}

static void check_report_and_fenv(void)
{
    reset_test();
    empty_scope();
    test_clock.wall += UINT64_C(5000000000);
    test_clock.cpu += UINT64_C(2000000000);
    log_changes_fenv = true;
    fesetround(FE_UPWARD);
    feclearexcept(FE_ALL_EXCEPT);
    feraiseexcept(FE_INEXACT);
    errno = EDOM;
    for (int i = 0; i < 1024; i++) {
        empty_scope();
    }
    assert(reports == 6 && errno == EDOM);
    assert(fegetround() == FE_UPWARD);
    assert(fetestexcept(FE_ALL_EXCEPT) == FE_INEXACT);
    assert(strstr(messages[0], "wall_ns=5000000000"));
    assert(strstr(messages[0], "thread_cpu_ns=2000000000"));
    assert(strstr(messages[0], "x87O=1025"));
    assert(strstr(messages[2], "x87O=2/0/0"));
    assert(!xemu_fpu_profile_thread.totals.entries[XEMU_FPU_X87_OTHER]);
    assert(xemu_fpu_profile_thread.sequence[XEMU_FPU_X87_OTHER] == 1025);
    for (int i = 0; i < 2048; i++) {
        empty_scope();
    }
    assert(reports == 6);
    fesetround(FE_TONEAREST);
    feclearexcept(FE_ALL_EXCEPT);
}

static jmp_buf escape;
static void nonlocal_scope(void)
{
    XEMU_FPU_PROFILE_SCOPE(XEMU_FPU_X87_OTHER);
    longjmp(escape, 1);
}

static void check_nonlocal_and_clock_failure(void)
{
    reset_test();
    if (setjmp(escape) == 0) {
        nonlocal_scope();
    }
    assert(xemu_fpu_profile_thread.totals.entries[XEMU_FPU_X87_OTHER] == 1);
    assert(!xemu_fpu_profile_thread.totals.samples[XEMU_FPU_X87_OTHER]);
    assert(test_clock.reads == 2);
    for (int i = 0; i < 1024; i++) {
        empty_scope();
    }
    assert(xemu_fpu_profile_thread.totals.samples[XEMU_FPU_X87_OTHER] == 1);
    assert(test_clock.reads == 6);
    for (unsigned fail = 1; fail <= 4; fail++) {
        reset_test();
        test_clock.fail_at = fail;
        errno = EDOM;
        empty_scope();
        assert(errno == EDOM);
        assert(!xemu_fpu_profile_thread.totals.samples[XEMU_FPU_X87_OTHER]);
        assert(!reports);
        test_clock.fail_at = 0;
        for (int i = 0; i < 1024; i++) {
            empty_scope();
        }
        assert(xemu_fpu_profile_thread.totals.samples[XEMU_FPU_X87_OTHER] == 1);
    }
}

static void *thread_test(void *arg)
{
    unsigned n = (uintptr_t)arg;

    test_clock = (TestClock) { .wall = 1, .cpu = 1 };
    for (unsigned i = 0; i < n; i++) {
        empty_scope();
        XEMU_FPU_PROFILE_TRANSFER(XEMU_FPU_LS_FLDS_FT0, XEMU_FPU_LS_GENERIC);
        XEMU_FPU_PROFILE_REJECT_DIRECT(XEMU_FPU_REJECT_BINOP,
                                       XEMU_FPU_REJECT_PC24_ENV);
    }
    assert(xemu_fpu_profile_thread.totals.entries[XEMU_FPU_X87_OTHER] == n);
    assert(xemu_fpu_profile_thread.totals.samples[XEMU_FPU_X87_OTHER] == 1);
    assert(test_clock.reads == 4);
    assert(xemu_fpu_profile_thread.totals.transfer[XEMU_FPU_LS_FLDS_FT0]
           [XEMU_FPU_LS_GENERIC] == n);
    assert(xemu_fpu_profile_thread.totals.reject[XEMU_FPU_REJECT_BINOP]
           [XEMU_FPU_REJECT_PC24_ENV] == n);
    return NULL;
}

static void check_threads(void)
{
    pthread_t a, b;

    reset_test();
    assert(pthread_create(&a, NULL, thread_test, (void *)(uintptr_t)137) == 0);
    assert(pthread_create(&b, NULL, thread_test, (void *)(uintptr_t)999) == 0);
    assert(pthread_join(a, NULL) == 0 && pthread_join(b, NULL) == 0);
    assert(!xemu_fpu_profile_thread.totals.entries[XEMU_FPU_X87_OTHER]);
    assert(!xemu_fpu_profile_thread.totals.transfer[XEMU_FPU_LS_FLDS_FT0]
           [XEMU_FPU_LS_GENERIC]);
    assert(!xemu_fpu_profile_thread.totals.reject[XEMU_FPU_REJECT_BINOP]
           [XEMU_FPU_REJECT_PC24_ENV]);
    assert(!test_clock.reads && !reports);
}
#endif

#ifdef PROFILE_REAL_CLOCKS
static uint64_t now_ns(void)
{
    struct timespec ts;

    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static __attribute__((noinline)) void empty_original(void)
{
    __asm__ volatile("" : : : "memory");
}

static void benchmark(unsigned iterations)
{
    CPUX86State env;

    seed(&env);
    for (int trial = 0; trial < 9; trial++) {
        for (int step = 0; step < 6; step++) {
            int mode = (step + trial) % 6;
            uint64_t start, ns;

            xemu_fpu_profile_init(mode == 2 || mode == 5);
            memset(&xemu_fpu_profile_thread, 0,
                   sizeof(xemu_fpu_profile_thread));
            start = now_ns();
            for (unsigned i = 0; i < iterations; i++) {
                if (mode < 3) {
                    if (mode == 0) {
                        orig_helper_fpush(&env);
                        orig_helper_fpop(&env);
                    } else {
                        helper_fpush(&env);
                        helper_fpop(&env);
                    }
                } else if (mode == 3) {
                    empty_original();
                } else {
                    empty_scope();
                }
            }
            ns = now_ns() - start;
            printf("bench trial=%d mode=%d ns=%" PRIu64
                   " calls=%u ns_per_call=%.4f checksum=%d\n",
                   trial, mode, ns, iterations * (mode < 3 ? 2 : 1),
                   (double)ns / iterations / (mode < 3 ? 2 : 1), env.fpstt);
        }
    }
}
#endif

int main(int argc, char **argv)
{
#ifdef PROFILE_REAL_CLOCKS
    if (argc > 1) {
        benchmark(5000000);
        return 0;
    }
#else
    (void)argc;
    (void)argv;
#endif
    assert(!xemu_fpu_profile_is_enabled());
    assert(XEMU_FPU_PROFILE_ACTIVE == EXPECT_PROFILE_ACTIVE);
    check_helper_state();
    check_lazy_detail_hooks();
#if XEMU_FPU_PROFILE_ACTIVE
    assert(!test_clock.reads && !reports);
    check_sampling_and_disable();
    check_native_outcomes();
    check_transfer_cells();
    check_rejection_reasons();
    check_pc24_rejection_reasons();
    check_detail_report_bounds();
    check_report_and_fenv();
    check_nonlocal_and_clock_failure();
    check_threads();
    reset_test();
    check_helper_state();
    assert(xemu_fpu_profile_thread.totals.entries[XEMU_FPU_X87_STACK] == 3072);
    assert(xemu_fpu_profile_thread.totals.samples[XEMU_FPU_X87_STACK] == 3);
    assert(xemu_fpu_profile_thread.totals.entries[XEMU_FPU_SSE_SCALAR] == 4);
    assert(xemu_fpu_profile_thread.totals.samples[XEMU_FPU_SSE_SCALAR] == 1);
#else
    xemu_fpu_profile_init(true);
    assert(!xemu_fpu_profile_is_enabled());
    check_helper_state();
    assert(!test_clock.reads && !reports);
#endif
    puts("PASS actual helper state, sampling/disable, native outcomes/reasons, "
         "transfer cells, report bounds/FENV/errno, nonlocal exit, clock "
         "failures and TLS isolation");
    return 0;
}
