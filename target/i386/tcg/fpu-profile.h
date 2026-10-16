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

enum XemuFpuProfileTransfer {
    XEMU_FPU_LS_FLDS_FT0,
    XEMU_FPU_LS_FLDL_FT0,
    XEMU_FPU_LS_FILDL_FT0,
    XEMU_FPU_LS_FLDS_ST0,
    XEMU_FPU_LS_FLDL_ST0,
    XEMU_FPU_LS_FILDL_ST0,
    XEMU_FPU_LS_FILDLL_ST0,
    XEMU_FPU_LS_FSTS_ST0,
    XEMU_FPU_LS_FSTL_ST0,
    XEMU_FPU_LS_FLDT_ST0,
    XEMU_FPU_LS_FSTT_ST0,
    XEMU_FPU_LS_FBLD_ST0,
    XEMU_FPU_LS_FBST_ST0,
    XEMU_FPU_LS_COUNT,
};

/* Eligibility/selected branches, not observations of new exception flags. */
enum XemuFpuProfileTransferPath {
    XEMU_FPU_LS_NORMAL_EXACT_FAST,
    XEMU_FPU_LS_PRECISION_FALLBACK,
    XEMU_FPU_LS_NON_NORMAL_FALLBACK,
    XEMU_FPU_LS_GENERIC,
    XEMU_FPU_LS_PATH_COUNT,
};

enum XemuFpuProfileRejectKind {
    XEMU_FPU_REJECT_BINOP,
    XEMU_FPU_REJECT_SQRT,
    XEMU_FPU_REJECT_KIND_COUNT,
};

enum XemuFpuProfileRejectReason {
    XEMU_FPU_REJECT_PC24,
    XEMU_FPU_REJECT_PC64,
    XEMU_FPU_REJECT_PC_RESERVED,
    XEMU_FPU_REJECT_ROUND_DOWN,
    XEMU_FPU_REJECT_ROUND_UP,
    XEMU_FPU_REJECT_ROUND_ZERO,
    XEMU_FPU_REJECT_POLICY_OFF,
    XEMU_FPU_REJECT_A_DENORMAL,
    XEMU_FPU_REJECT_A_PSEUDO_DENORMAL,
    XEMU_FPU_REJECT_A_UNNORMAL,
    XEMU_FPU_REJECT_A_INFINITY,
    XEMU_FPU_REJECT_A_NAN,
    XEMU_FPU_REJECT_A_PRECISION,
    XEMU_FPU_REJECT_A_EXP_LOW,
    XEMU_FPU_REJECT_A_EXP_HIGH,
    XEMU_FPU_REJECT_B_DENORMAL,
    XEMU_FPU_REJECT_B_PSEUDO_DENORMAL,
    XEMU_FPU_REJECT_B_UNNORMAL,
    XEMU_FPU_REJECT_B_INFINITY,
    XEMU_FPU_REJECT_B_NAN,
    XEMU_FPU_REJECT_B_PRECISION,
    XEMU_FPU_REJECT_B_EXP_LOW,
    XEMU_FPU_REJECT_B_EXP_HIGH,
    XEMU_FPU_REJECT_DIV_ZERO,
    XEMU_FPU_REJECT_SQRT_NEGATIVE,
    XEMU_FPU_REJECT_RESULT,
    XEMU_FPU_REJECT_INVALID_OP,
    XEMU_FPU_REJECT_PC24_ENV,
    XEMU_FPU_REJECT_HOST_ENV,
    XEMU_FPU_REJECT_REASON_COUNT,
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
    uint64_t transfer[XEMU_FPU_LS_COUNT][XEMU_FPU_LS_PATH_COUNT];
    uint64_t reject[XEMU_FPU_REJECT_KIND_COUNT][XEMU_FPU_REJECT_REASON_COUNT];
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
void xemu_fpu_profile_reject_binop(uint16_t fpuc, bool policy,
                                  bool pc24_supported, int op,
                                  uint64_t a_low, uint16_t a_high,
                                  uint64_t b_low, uint16_t b_high);
void xemu_fpu_profile_reject_sqrt(uint16_t fpuc, bool policy,
                                 bool pc24_supported,
                                 uint64_t low, uint16_t high);
void xemu_fpu_profile_reject_pc24_binop(int op,
                                       uint64_t a_low, uint16_t a_high,
                                       uint64_t b_low, uint16_t b_high);
void xemu_fpu_profile_reject_pc24_sqrt(uint64_t low, uint16_t high);

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

/* Capture only the original policy call in its original short-circuit slot. */
#define XEMU_FPU_PROFILE_POLICY_STATE bool xemu_fpu_policy = true
#define XEMU_FPU_PROFILE_POLICY(expression) \
    (xemu_fpu_policy = (expression))

/* Lazy arguments: runtime-off and compiled-out hooks do no reason work. */
#define XEMU_FPU_PROFILE_TRANSFER(helper, path) do { \
    if (__builtin_expect(xemu_fpu_profile_enabled, 0)) { \
        xemu_fpu_profile_thread.totals.transfer[(helper)][(path)]++; \
    } \
} while (0)
#define XEMU_FPU_PROFILE_REJECT_BINOP(fpuc, op, al, ah, bl, bh) do { \
    if (__builtin_expect(xemu_fpu_profile_enabled, 0)) { \
        xemu_fpu_profile_reject_binop((fpuc), xemu_fpu_policy, \
                                     X87_NATIVE24_ACTIVE, (op), \
                                     (al), (ah), (bl), (bh)); \
    } \
} while (0)
#define XEMU_FPU_PROFILE_REJECT_SQRT(fpuc, low, high) do { \
    if (__builtin_expect(xemu_fpu_profile_enabled, 0)) { \
        xemu_fpu_profile_reject_sqrt((fpuc), xemu_fpu_policy, \
                                    X87_NATIVE24_ACTIVE, (low), (high)); \
    } \
} while (0)
#define XEMU_FPU_PROFILE_REJECT_DIRECT(kind, reason) do { \
    if (__builtin_expect(xemu_fpu_profile_enabled, 0)) { \
        xemu_fpu_profile_thread.totals.reject[(kind)][(reason)]++; \
    } \
} while (0)
#define XEMU_FPU_PROFILE_REJECT_PC24_BINOP(op, al, ah, bl, bh) do { \
    if (__builtin_expect(xemu_fpu_profile_enabled, 0)) { \
        xemu_fpu_profile_reject_pc24_binop((op), (al), (ah), (bl), (bh)); \
    } \
} while (0)
#define XEMU_FPU_PROFILE_REJECT_PC24_SQRT(low, high) do { \
    if (__builtin_expect(xemu_fpu_profile_enabled, 0)) { \
        xemu_fpu_profile_reject_pc24_sqrt((low), (high)); \
    } \
} while (0)
#else
#define XEMU_FPU_PROFILE_SCOPE(category)
#define xemu_fpu_profile_native_result(kind, hit) (hit)
#define XEMU_FPU_PROFILE_POLICY_STATE
#define XEMU_FPU_PROFILE_POLICY(expression) (expression)
#define XEMU_FPU_PROFILE_TRANSFER(helper, path) ((void)0)
#define XEMU_FPU_PROFILE_REJECT_BINOP(fpuc, op, al, ah, bl, bh) ((void)0)
#define XEMU_FPU_PROFILE_REJECT_SQRT(fpuc, low, high) ((void)0)
#define XEMU_FPU_PROFILE_REJECT_DIRECT(kind, reason) ((void)0)
#define XEMU_FPU_PROFILE_REJECT_PC24_BINOP(op, al, ah, bl, bh) ((void)0)
#define XEMU_FPU_PROFILE_REJECT_PC24_SQRT(low, high) ((void)0)
#endif

#ifdef __cplusplus
}
#endif
#endif
