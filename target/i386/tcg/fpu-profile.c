/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Optional ARM64 Android runtime helper profiling, owned by each thread. */
#include "qemu/osdep.h"
#include "fpu-profile.h"

#if XEMU_FPU_PROFILE_ACTIVE
#include <android/log.h>
#include <fenv.h>
#include <time.h>
#include "fpu-profile-reasons.h"

bool xemu_fpu_profile_enabled;
__thread XemuFpuProfileThread xemu_fpu_profile_thread;

void xemu_fpu_profile_reject_binop(uint16_t fpuc, bool policy, int op,
                                  uint64_t a_low, uint16_t a_high,
                                  uint64_t b_low, uint16_t b_high)
{
    enum XemuFpuProfileRejectReason reason =
        xemu_fpu_profile_binop_reason(fpuc, policy, op, a_low, a_high,
                                      b_low, b_high);

    xemu_fpu_profile_thread.totals.reject[XEMU_FPU_REJECT_BINOP][reason]++;
}

void xemu_fpu_profile_reject_sqrt(uint16_t fpuc, bool policy,
                                 uint64_t low, uint16_t high)
{
    enum XemuFpuProfileRejectReason reason =
        xemu_fpu_profile_sqrt_reason(fpuc, policy, low, high);

    xemu_fpu_profile_thread.totals.reject[XEMU_FPU_REJECT_SQRT][reason]++;
}

static bool read_clocks(uint64_t *wall_ns, uint64_t *cpu_ns)
{
    struct timespec wall, cpu;
    int saved_errno = errno;
    bool ok = clock_gettime(CLOCK_MONOTONIC, &wall) == 0 &&
              clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu) == 0;

    errno = saved_errno;
    if (ok) {
        *wall_ns = (uint64_t)wall.tv_sec * 1000000000 + wall.tv_nsec;
        *cpu_ns = (uint64_t)cpu.tv_sec * 1000000000 + cpu.tv_nsec;
    }
    return ok;
}

void xemu_fpu_profile_sample_start(XemuFpuProfileScope *scope)
{
    XemuFpuProfileThread *t = &xemu_fpu_profile_thread;

    scope->timed = read_clocks(&scope->wall_ns, &scope->cpu_ns);
    if (scope->timed && !t->anchored) {
        t->interval_wall_ns = scope->wall_ns;
        t->interval_cpu_ns = scope->cpu_ns;
        t->anchored = true;
    }
}

static void report_details(const XemuFpuProfileThread *t)
{
    static const char *const transfer_names[XEMU_FPU_LS_COUNT] = {
        "fldsFT", "fldlFT", "fildlFT", "fldsST", "fldlST", "fildlST",
        "fildllST", "fstsST", "fstlST", "fldtST", "fsttST", "fbldST",
        "fbstST",
    };
    static const char *const reason_names[XEMU_FPU_REJECT_REASON_COUNT] = {
        "pc24", "pc64", "pcRes", "rcDown", "rcUp", "rcZero", "policy",
        "aDen", "aPseudo", "aUnnorm", "aInf", "aNaN", "aPrec", "aExpLo",
        "aExpHi", "bDen", "bPseudo", "bUnnorm", "bInf", "bNaN", "bPrec",
        "bExpLo", "bExpHi", "divZero", "sqrtNeg", "result", "badOp",
    };
    static const char *const kinds[XEMU_FPU_REJECT_KIND_COUNT] = {
        "bin", "sqrt",
    };
    char buf[1536];
    size_t pos = 0;

    for (int i = 0; i < XEMU_FPU_LS_COUNT; i++) {
        const uint64_t *p = t->totals.transfer[i];

        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        " %s=%" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64,
                        transfer_names[i], p[XEMU_FPU_LS_NORMAL_EXACT_FAST],
                        p[XEMU_FPU_LS_PRECISION_FALLBACK],
                        p[XEMU_FPU_LS_NON_NORMAL_FALLBACK],
                        p[XEMU_FPU_LS_GENERIC]);
    }
    __android_log_print(ANDROID_LOG_INFO, "xemu-cpu-ls",
                        "seq=%" PRIu64 " v=2 paths="
                        "normal_exact_fast/precision_fallback/"
                        "non_normal_fallback/generic:%s",
                        t->report_sequence, buf);
    for (int kind = 0; kind < XEMU_FPU_REJECT_KIND_COUNT; kind++) {
        pos = 0;
        for (int i = 0; i < XEMU_FPU_REJECT_REASON_COUNT; i++) {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            " %s=%" PRIu64, reason_names[i],
                            t->totals.reject[kind][i]);
        }
        __android_log_print(ANDROID_LOG_INFO, "xemu-cpu-reject",
                            "seq=%" PRIu64 " v=2 kind=%s first_reason:%s",
                            t->report_sequence, kinds[kind], buf);
    }
}

static void report_interval(XemuFpuProfileThread *t, uint64_t wall,
                            uint64_t cpu)
{
    static const char *const names[XEMU_FPU_CATEGORY_COUNT] = {
        "x87A", "x87Cmp", "x87Cvt", "x87LS", "x87St", "x87T", "x87O",
        "sseP", "sseS", "sseCmp", "sseCvt", "sseO",
    };
    XemuFpuProfileTotals *s = &t->totals;
    uint64_t elapsed_wall = wall - t->interval_wall_ns;
    uint64_t elapsed_cpu = cpu - t->interval_cpu_ns;
    char counts[512] = { 0 }, times[1024] = { 0 };
    size_t count_pos = 0, time_pos = 0;
    int saved_errno = errno;
    fenv_t saved_fenv;
    bool have_fenv = fegetenv(&saved_fenv) == 0;

    for (int i = 0; i < XEMU_FPU_CATEGORY_COUNT; i++) {
        count_pos += snprintf(counts + count_pos, sizeof(counts) - count_pos,
                              " %s=%" PRIu64, names[i], s->entries[i]);
        time_pos += snprintf(times + time_pos, sizeof(times) - time_pos,
                             " %s=%" PRIu64 "/%" PRIu64 "/%" PRIu64,
                             names[i], s->samples[i], s->wall_ns[i],
                             s->cpu_ns[i]);
    }
    t->report_sequence++;
    __android_log_print(ANDROID_LOG_INFO, "xemu-cpu-run",
                        "seq=%" PRIu64 " wall_ns=%" PRIu64
                        " thread_cpu_ns=%" PRIu64
                        " stride=%d entries:%s",
                        t->report_sequence, elapsed_wall, elapsed_cpu,
                        XEMU_FPU_PROFILE_STRIDE, counts);
    __android_log_print(ANDROID_LOG_INFO, "xemu-cpu-native",
                        "seq=%" PRIu64 " hit/fallback:"
                        " bin=%" PRIu64 "/%" PRIu64
                        " sqrt=%" PRIu64 "/%" PRIu64
                        " cmp=%" PRIu64 "/%" PRIu64
                        " fist=%" PRIu64 "/%" PRIu64,
                        t->report_sequence,
                        s->native_hit[XEMU_FPU_NATIVE_BINOP],
                        s->native_fallback[XEMU_FPU_NATIVE_BINOP],
                        s->native_hit[XEMU_FPU_NATIVE_SQRT],
                        s->native_fallback[XEMU_FPU_NATIVE_SQRT],
                        s->native_hit[XEMU_FPU_NATIVE_COMPARE],
                        s->native_fallback[XEMU_FPU_NATIVE_COMPARE],
                        s->native_hit[XEMU_FPU_NATIVE_FIST],
                        s->native_fallback[XEMU_FPU_NATIVE_FIST]);
    __android_log_print(ANDROID_LOG_INFO, "xemu-cpu-sample",
                        "seq=%" PRIu64 " completed/wall_ns/thread_cpu_ns:%s",
                        t->report_sequence, times);
    report_details(t);
    memset(s, 0, sizeof(*s));
    t->interval_wall_ns = wall;
    t->interval_cpu_ns = cpu;
    if (have_fenv) {
        fesetenv(&saved_fenv);
    }
    errno = saved_errno;
}

void xemu_fpu_profile_sample_end(XemuFpuProfileScope *scope)
{
    XemuFpuProfileThread *t = &xemu_fpu_profile_thread;
    uint64_t wall, cpu;

    if (!read_clocks(&wall, &cpu) || wall < scope->wall_ns ||
        cpu < scope->cpu_ns) {
        return;
    }
    t->totals.samples[scope->category]++;
    t->totals.wall_ns[scope->category] += wall - scope->wall_ns;
    t->totals.cpu_ns[scope->category] += cpu - scope->cpu_ns;
    if (wall - t->interval_wall_ns >= UINT64_C(5000000000) &&
        cpu >= t->interval_cpu_ns) {
        report_interval(t, wall, cpu);
    }
}

void xemu_fpu_profile_init(bool enabled)
{
    /* Startup only: no reader exists until the caller starts vCPU threads. */
    xemu_fpu_profile_enabled = enabled;
}

bool xemu_fpu_profile_is_enabled(void)
{
    return xemu_fpu_profile_enabled;
}
#else
void xemu_fpu_profile_init(bool enabled)
{
    (void)enabled;
}

bool xemu_fpu_profile_is_enabled(void)
{
    return false;
}
#endif
