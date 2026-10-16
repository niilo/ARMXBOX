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
#include "x87-native.h"

typedef struct TestClock {
    uint64_t wall, cpu, reads;
    unsigned fail_at;
} TestClock;
static __thread TestClock test_clock;
static char messages[8][1600];
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
} float_status;
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
    assert(reports == 3 && errno == EDOM);
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
    assert(reports == 3);
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
    }
    assert(xemu_fpu_profile_thread.totals.entries[XEMU_FPU_X87_OTHER] == n);
    assert(xemu_fpu_profile_thread.totals.samples[XEMU_FPU_X87_OTHER] == 1);
    assert(test_clock.reads == 4);
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
#if XEMU_FPU_PROFILE_ACTIVE
    assert(!test_clock.reads && !reports);
    check_sampling_and_disable();
    check_native_outcomes();
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
    puts("PASS actual helper state, sampling/disable, native outcomes, report "
         "FENV/errno, early/nonlocal exit, clock failures and TLS isolation");
    return 0;
}
