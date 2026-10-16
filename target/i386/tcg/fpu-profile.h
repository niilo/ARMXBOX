/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Optional runtime helper profiling; not translation statistics. */
#ifndef TARGET_I386_TCG_FPU_PROFILE_H
#define TARGET_I386_TCG_FPU_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call before creating any vCPU thread. Do not change during execution. */
void xemu_fpu_profile_init(bool enabled);
bool xemu_fpu_profile_is_enabled(void);
bool xemu_x87_native_helpers_enabled(void);

enum XemuFpuProfileCategory {
    XEMU_FPU_X87_ARITH,
    XEMU_FPU_X87_COMPARE,
    XEMU_FPU_X87_CONVERT,
    XEMU_FPU_X87_LOAD_STORE,
    XEMU_FPU_X87_STACK,
    XEMU_FPU_X87_TRANSCENDENTAL,
    XEMU_FPU_X87_OTHER,
    XEMU_FPU_SSE_PACKED,
    XEMU_FPU_SSE_SCALAR,
    XEMU_FPU_SSE_COMPARE,
    XEMU_FPU_SSE_CONVERT,
    XEMU_FPU_SSE_OTHER,
    XEMU_FPU_CATEGORY_COUNT,
};

enum XemuFpuProfileNative {
    XEMU_FPU_NATIVE_BINOP,
    XEMU_FPU_NATIVE_SQRT,
    XEMU_FPU_NATIVE_COMPARE,
    XEMU_FPU_NATIVE_FIST,
    XEMU_FPU_NATIVE_COUNT,
};

#ifndef XEMU_ENABLE_CPU_PROFILE
#define XEMU_ENABLE_CPU_PROFILE 0
#endif

#if XEMU_ENABLE_CPU_PROFILE && defined(XBOX) && \
    defined(__ANDROID__) && defined(__aarch64__)
#define XEMU_FPU_PROFILE_ACTIVE 1
#else
#define XEMU_FPU_PROFILE_ACTIVE 0
#endif

#if XEMU_FPU_PROFILE_ACTIVE
#define XEMU_FPU_PROFILE_STRIDE 1024

typedef struct XemuFpuProfileTotals {
    uint64_t entries[XEMU_FPU_CATEGORY_COUNT];
    uint64_t samples[XEMU_FPU_CATEGORY_COUNT];
    uint64_t wall_ns[XEMU_FPU_CATEGORY_COUNT];
    uint64_t cpu_ns[XEMU_FPU_CATEGORY_COUNT];
    uint64_t native_hit[XEMU_FPU_NATIVE_COUNT];
    uint64_t native_fallback[XEMU_FPU_NATIVE_COUNT];
} XemuFpuProfileTotals;

typedef struct XemuFpuProfileThread {
    XemuFpuProfileTotals totals;
    uint64_t sequence[XEMU_FPU_CATEGORY_COUNT];
    uint64_t interval_wall_ns;
    uint64_t interval_cpu_ns;
    uint64_t report_sequence;
    bool anchored;
} XemuFpuProfileThread;

typedef struct XemuFpuProfileScope {
    uint64_t wall_ns;
    uint64_t cpu_ns;
    enum XemuFpuProfileCategory category;
    bool timed;
} XemuFpuProfileScope;

/* Immutable after startup; all counters belong to the executing thread. */
extern bool xemu_fpu_profile_enabled;
extern __thread XemuFpuProfileThread xemu_fpu_profile_thread;
void xemu_fpu_profile_sample_start(XemuFpuProfileScope *scope);
void xemu_fpu_profile_sample_end(XemuFpuProfileScope *scope);

static inline XemuFpuProfileScope
xemu_fpu_profile_begin(enum XemuFpuProfileCategory category)
{
    XemuFpuProfileScope scope = { .category = category };

    if (__builtin_expect(xemu_fpu_profile_enabled, 0)) {
        XemuFpuProfileThread *t = &xemu_fpu_profile_thread;
        uint64_t sequence = t->sequence[category]++;

        t->totals.entries[category]++;
        if ((sequence & (XEMU_FPU_PROFILE_STRIDE - 1)) == 0) {
            xemu_fpu_profile_sample_start(&scope);
        }
    }
    return scope;
}

static inline void xemu_fpu_profile_scope_end(XemuFpuProfileScope *scope)
{
    if (__builtin_expect(scope->timed, 0)) {
        xemu_fpu_profile_sample_end(scope);
    }
}

static inline bool
xemu_fpu_profile_native_result(enum XemuFpuProfileNative kind, bool hit)
{
    if (__builtin_expect(xemu_fpu_profile_enabled, 0)) {
        XemuFpuProfileTotals *t = &xemu_fpu_profile_thread.totals;

        if (hit) {
            t->native_hit[kind]++;
        } else {
            t->native_fallback[kind]++;
        }
    }
    return hit;
}

/* Cleanup covers ordinary early returns; a nonlocal exit has no sample. */
#define XEMU_FPU_PROFILE_SCOPE(category) \
    XemuFpuProfileScope xemu_fpu_scope \
        __attribute__((cleanup(xemu_fpu_profile_scope_end))) = \
        xemu_fpu_profile_begin(category)
#else
#define XEMU_FPU_PROFILE_SCOPE(category)
#define xemu_fpu_profile_native_result(kind, hit) (hit)
#endif

#ifdef __cplusplus
}
#endif
#endif
