/*
 * Execute source-extracted TCG emitter operations against RC10 x87 helpers.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Select only source gates after including the host's system headers. */
#define XBOX 1
#ifndef FIXTURE_ARM64
#define FIXTURE_ARM64 1
#endif
#if FIXTURE_ARM64
#define __aarch64__ 1
#else
#undef __aarch64__
#endif
#ifndef TARGET_LONG_BITS
#define TARGET_LONG_BITS 32
#endif
#define QEMU_BUILD_BUG_ON(c) _Static_assert(!(c), #c)
typedef uint64_t MMXReg;
#include "types.inc"

typedef struct CPUX86State {
    uint64_t before[3];
    int fpstt;
    uint16_t fpuc, fpus;
    uint8_t fptags[8];
    FPReg fpregs[8];
    floatx80 ft0;
    double ft0_native;
    uint64_t fpip, fpdp;
    uint16_t fpcs, fpds;
    struct { uint32_t selector; } segs[6];
    uint32_t eflags;
    uint8_t status[64];
    uint64_t after[3];
} CPUX86State;
#define FT0 (env->ft0)
#define ST0 (env->fpregs[env->fpstt].d)
#define ST(n) (env->fpregs[(env->fpstt + (n)) & 7].d)
#include "reference.inc"

typedef int TCGv_i32;
typedef int TCGv_i64;
typedef int TCGv_ptr;
typedef int TCGv_fp;
#define R_CS 1
#define TCG_ENV 1
#define TCG_TOP 2
static const TCGv_ptr tcg_env = TCG_ENV;
static const TCGv_i32 fpstt = TCG_TOP;
static int g_use_fp_jit;

typedef struct DisasContext {
    int fpstt_delta;
    TCGv_fp fpregs[8], ft0;
    bool flcr_set, fip_pend;
    int fdp_pend_seg;
    TCGv_i32 fip_sel;
    TCGv_ptr fip_val, fdp_val;
} DisasContext;

/*
 * Small interpreter for actual emitted integer operations. No FP operation is
 * implemented here: reaching the old lossy inline branch aborts the fixture.
 */
enum Opcode { IMM, MOV32, ADD32, AND32, MUL32, EXT_PTR, ADD_PTR, ADDI_PTR,
              LD64, ST64, LD32, LD16, ST16, ST8, ST_TL, HELPER, BRANCH };
enum Helper { PUSH, POP, FT_FROM_ST, ST0_FROM_ST, ST_FROM_ST0, XCHG,
              ENTER_MMX, EMMS, CHS };
typedef struct Op {
    enum Opcode code;
    int dst, a, b;
    int64_t imm;
} Op;
static Op ops[16384];
static unsigned num_ops, next_temp;
static DisasContext *current_dc;
static uint64_t helper_calls;

static int temp(void)
{
    assert(next_temp < 8192);
    return next_temp++;
}
#define tcg_temp_new_i32 temp
#define tcg_temp_new_i64 temp
#define tcg_temp_new_ptr temp
static void emit(enum Opcode code, int dst, int a, int b, int64_t imm)
{
    assert(num_ops < sizeof(ops) / sizeof(ops[0]));
    ops[num_ops++] = (Op){ code, dst, a, b, imm };
}
static TCGv_i32 tcg_constant_i32(int value)
{
    int id = temp();
    emit(IMM, id, 0, 0, (uint32_t)value);
    return id;
}
static TCGv_ptr constant_ptr(uint64_t value)
{
    int id = temp();
    emit(IMM, id, 0, 0, value);
    return id;
}
#define UNARY_IMM(name, code)                                        \
    static void name(int d, int a, int64_t v)                         \
    {                                                               \
        emit(code, d, a, 0, v);                                      \
    }
UNARY_IMM(tcg_gen_addi_i32, ADD32)
UNARY_IMM(tcg_gen_andi_i32, AND32)
UNARY_IMM(tcg_gen_muli_i32, MUL32)
UNARY_IMM(tcg_gen_addi_ptr, ADDI_PTR)
UNARY_IMM(tcg_gen_ld_i64, LD64)
UNARY_IMM(tcg_gen_st_i64, ST64)
UNARY_IMM(tcg_gen_ld_i32, LD32)
UNARY_IMM(tcg_gen_st16_i32, ST16)
UNARY_IMM(tcg_gen_st8_i32, ST8)
UNARY_IMM(tcg_gen_st_tl, ST_TL)
static void tcg_gen_subi_i32(int d, int a, int v)
{
    emit(ADD32, d, a, 0, -(int64_t)v);
}
static void tcg_gen_mov_i32(int d, int a)
{
    emit(MOV32, d, a, 0, 0);
}
static void tcg_gen_ext_i32_ptr(int d, int a)
{
    emit(EXT_PTR, d, a, 0, 0);
}
static void tcg_gen_add_ptr(int d, int a, int b)
{
    emit(ADD_PTR, d, a, b, 0);
}

static void gen_flush_fp(DisasContext *s);
static void record_helper(enum Helper helper, int arg)
{
    /* tcg_gen_callN invokes gen_bb_epilogue before emitting a real helper. */
    gen_flush_fp(current_dc);
    emit(HELPER, 0, arg, 0, helper);
}
static void gen_helper_fpush(int env)
{
    assert(env == tcg_env);
    record_helper(PUSH, 0);
}
static void gen_helper_fpop(int env)
{
    assert(env == tcg_env);
    record_helper(POP, 0);
}
#define HELPER_ARG(name, code)                                      \
    static void gen_helper_##name(int env, int index)                \
    {                                                              \
        assert(env == tcg_env);                                     \
        record_helper(code, index);                                 \
    }
HELPER_ARG(fmov_FT0_STN, FT_FROM_ST)
HELPER_ARG(fmov_ST0_STN, ST0_FROM_ST)
HELPER_ARG(fmov_STN_ST0, ST_FROM_ST0)
HELPER_ARG(fxchg_ST0_STN, XCHG)
#define HARD_FPU_HAS_TCG_FP_OPS 1
#define BISECT_GRP_STACK 1
#define fp_pc_wrapper(name) name##_fixture
static void flush_fp_regs_fixture(DisasContext *s)
{
    assert(!s->ft0);
    for (unsigned i = 0; i < 8; i++) {
        assert(!s->fpregs[i]);
    }
}
static void gen_fpop_fixture(DisasContext *s)
{
    (void)s;
    abort();
}
#define UNUSED_FP(name)                                             \
    static void name##_fixture(DisasContext *s, int index)           \
    {                                                              \
        (void)s; (void)index; abort();                              \
    }
UNUSED_FP(gen_fmov_FT0_STN)
UNUSED_FP(gen_fmov_ST0_STN)
UNUSED_FP(gen_fmov_STN_ST0)
static bool fpu_using_double_precision(DisasContext *s)
{
    (void)s;
    return true;
}
static void get_stn_f64(DisasContext *s, int n)
{
    (void)s; (void)n; abort();
}
#define get_stn_f32 get_stn_f64
#include "emitters.inc"

static uint64_t reg_get(uint64_t *regs, int id, CPUX86State *env)
{
    if (id == TCG_TOP) {
        return (uint32_t)env->fpstt;
    }
    return regs[id];
}
static void execute(CPUX86State *env)
{
    uint64_t regs[8192] = { 0 };
    for (unsigned n = 0; n < num_ops; n++) {
        Op *op = &ops[n];
        uint64_t a = reg_get(regs, op->a, env);
        uint64_t b = reg_get(regs, op->b, env);
        uint64_t result = 0;
        size_t size = 0;
        bool store = false;
        switch (op->code) {
        case IMM:
            result = op->imm;
            break;
        case MOV32:
            result = (uint32_t)a;
            break;
        case ADD32:
            result = (uint32_t)(a + op->imm);
            break;
        case AND32:
            result = (uint32_t)a & op->imm;
            break;
        case MUL32:
            result = (uint32_t)(a * op->imm);
            break;
        case EXT_PTR:
            result = (uint32_t)a;
            break;
        case ADD_PTR:
            result = a + b;
            break;
        case ADDI_PTR:
            result = a + op->imm;
            break;
        case LD64:
            size = 8;
            break;
        case LD32:
            size = 4;
            break;
        case LD16:
            size = 2;
            break;
        case ST64:
            size = 8;
            store = true;
            break;
        case ST16:
            size = 2;
            store = true;
            break;
        case ST8:
            size = 1;
            store = true;
            break;
        case ST_TL:
            size = TARGET_LONG_BITS / 8;
            store = true;
            break;
        case BRANCH:
            if (a) {
                n = op->imm - 1;
            }
            continue;
        case HELPER:
            helper_calls++;
            switch (op->imm) {
            case PUSH:
                helper_fpush(env);
                break;
            case POP:
                helper_fpop(env);
                break;
            case FT_FROM_ST:
                helper_fmov_FT0_STN(env, a);
                break;
            case ST0_FROM_ST:
                helper_fmov_ST0_STN(env, a);
                break;
            case ST_FROM_ST0:
                helper_fmov_STN_ST0(env, a);
                break;
            case XCHG:
                helper_fxchg_ST0_STN(env, a);
                break;
            case ENTER_MMX:
                helper_enter_mmx(env);
                break;
            case EMMS:
                helper_emms(env);
                break;
            case CHS:
                helper_fchs_ST0(env);
                break;
            default:
                abort();
            }
            continue;
        }
        if (size) {
            uint64_t offset = (store ? reg_get(regs, op->a, env) : a) +
                              op->imm;
            assert(offset <= sizeof(*env) && size <= sizeof(*env) - offset);
            if (store) {
                uint64_t value = reg_get(regs, op->dst, env);
                memcpy((char *)env + offset, &value, size);
                continue;
            }
            memcpy(&result, (char *)env + offset, size);
        }
        if (op->dst == TCG_TOP) {
            env->fpstt = result;
        } else {
            regs[op->dst] = result;
        }
    }
}
static uint64_t seed = 0x987fedcba;
static uint32_t rng(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
}
static void init(CPUX86State *env)
{
    uint8_t *bytes = (void *)env;
    for (unsigned n = 0; n < sizeof(*env); n++) {
        bytes[n] = rng();
    }
    env->fpstt = rng() & 7;
}
static void start(DisasContext *s)
{
    memset(s, 0, sizeof(*s));
    s->fdp_pend_seg = -1;
    current_dc = s;
    num_ops = 0;
    next_temp = 3;
}
static void pending(DisasContext *s, CPUX86State *expected)
{
    s->fip_pend = true;
    s->fip_val = constant_ptr(rng());
    uint64_t ip = ops[num_ops - 1].imm;
    memcpy(&expected->fpip, &ip, TARGET_LONG_BITS / 8);
    expected->fpcs = expected->segs[R_CS].selector;
    s->fdp_pend_seg = rng() % 6;
    s->fdp_val = constant_ptr(rng());
    uint64_t dp = ops[num_ops - 1].imm;
    memcpy(&expected->fpdp, &dp, TARGET_LONG_BITS / 8);
    expected->fpds = expected->segs[s->fdp_pend_seg].selector;
}
static void emit_selected(DisasContext *s, enum Helper kind, int index)
{
    switch (kind) {
    case PUSH:
        gen_fpush(s);
        break;
    case POP:
        gen_fpop(s);
        break;
    case FT_FROM_ST:
        gen_fmov_FT0_STN(s, index);
        break;
    case ST0_FROM_ST:
        gen_fmov_ST0_STN(s, index);
        break;
    case ST_FROM_ST0:
        gen_fmov_STN_ST0(s, index);
        break;
    case XCHG:
        gen_fxchg_ST0_STN(s, index);
        break;
    case ENTER_MMX: case EMMS: case CHS:
        record_helper(kind, 0);
        break;
    }
}
static void reference(CPUX86State *env, enum Helper kind, int index)
{
    switch (kind) {
    case PUSH:
        helper_fpush(env);
        break;
    case POP:
        helper_fpop(env);
        break;
    case FT_FROM_ST:
        helper_fmov_FT0_STN(env, index);
        break;
    case ST0_FROM_ST:
        helper_fmov_ST0_STN(env, index);
        break;
    case ST_FROM_ST0:
        helper_fmov_STN_ST0(env, index);
        break;
    case XCHG:
        helper_fxchg_ST0_STN(env, index);
        break;
    case ENTER_MMX:
        helper_enter_mmx(env);
        break;
    case EMMS:
        helper_emms(env);
        break;
    case CHS:
        helper_fchs_ST0(env);
        break;
    }
}
static unsigned cases;
static void equal(const CPUX86State *actual, const CPUX86State *expected)
{
    /*
     * Padding bytes are not architectural. Compare their enclosing state
     * separately after zeroing only floatx80's tail padding in both copies.
     */
    CPUX86State a = *actual, b = *expected;
    for (unsigned n = 0; n < 9; n++) {
        floatx80 *x = n == 8 ? &a.ft0 : &a.fpregs[n].d;
        floatx80 *y = n == 8 ? &b.ft0 : &b.fpregs[n].d;
        memset((char *)x + 10, 0, 6);
        memset((char *)y + 10, 0, 6);
    }
    if (memcmp(&a, &b, sizeof(a))) {
        fprintf(stderr, "state mismatch at case %u\n", cases);
        exit(1);
    }
    cases++;
}
static void one(CPUX86State *actual, CPUX86State *expected,
                enum Helper kind, int index, bool fip)
{
    DisasContext s;
    start(&s);
    if (fip) {
        pending(&s, expected);
    }
    emit_selected(&s, kind, index);
    reference(expected, kind, index);
    execute(actual);
    equal(actual, expected);
    assert(s.fpstt_delta == 0);
}
static void edges(void)
{
    static const floatx80 values[] = {
        { 0, 0 }, { 0, 0x8000 }, { 1, 0 },
        { UINT64_MAX, 0x7ffe }, { UINT64_MAX, 0xfffe },
        { UINT64_C(0x8000000000000000), 0x7fff },
        { UINT64_C(0xc000000000000001), 0x7fff },
        { UINT64_C(0x8000000000000001), 0x3fff },
        { UINT64_C(0x7fffffffffffffff), 0x4000 },
    };
    for (unsigned top = 0; top < 8; top++) {
        for (unsigned index = 0; index < 8; index++) {
            for (unsigned value = 0; value < sizeof(values) / sizeof(*values);
                 value++) {
                for (unsigned kind = 0; kind <= XCHG; kind++) {
                    CPUX86State a, b;
                    init(&a);
                    a.fpstt = top;
                    a.fpregs[top].d = values[value];
                    b = a;
                    one(&a, &b, kind, index, true);
                }
            }
        }
    }
    printf("%u edge/index/top/tag/payload/FIP cases pass\n", cases);
}
static void sequences(void)
{
    CPUX86State a, b;
    init(&a);
    b = a;
    for (unsigned i = 0; i < 100000; i++) {
        one(&a, &b, rng() % 6, rng() & 7, i % 7 == 0);
    }
    puts("100000 randomized mixed operations pass");
}
static void blocks(void)
{
    for (unsigned trial = 0; trial < 1000; trial++) {
        CPUX86State a, b;
        DisasContext s;
        init(&a);
        b = a;
        start(&s);
        pending(&s, &b);
        for (unsigned n = 0; n < 32; n++) {
            enum Helper kind = rng() % 9;
            int index = rng() & 7;
            emit_selected(&s, kind, index);
            reference(&b, kind, index);
        }
        gen_flush_fp(&s);
        execute(&a);
        equal(&a, &b);
    }
    puts("1000 blocks of 32 emitters with real MMX/unary "
         "helper boundaries pass");
}

static void boundaries(void)
{
    for (unsigned top = 0; top < 8; top++) {
        CPUX86State a, b;
        init(&a);
        a.fpstt = top;
        b = a;
        /* FLD ST(7): push first, then old ST(7) is new ST(0). */
        one(&a, &b, PUSH, 0, true);
        one(&a, &b, ST0_FROM_ST, (7 + 1) & 7, false);
        /* FSTP ST(0) and FSTP ST(7): copy before popping. */
        one(&a, &b, ST_FROM_ST0, 0, true);
        one(&a, &b, POP, 0, false);
        one(&a, &b, ST_FROM_ST0, 7, true);
        one(&a, &b, POP, 0, false);
        /* FCMOV: only the taken branch calls the move emitter. */
        for (unsigned taken = 0; taken < 2; taken++) {
            DisasContext s;
            start(&s);
            pending(&s, &b);
            /*
             * Actual tcg_emit_op's BB_END hook flushes before the FCMOV
             * branch; execute the same branch around both emitted paths.
             */
            gen_flush_fp(&s);
            int skip = tcg_constant_i32(!taken);
            unsigned branch = num_ops;
            emit(BRANCH, 0, skip, 0, 0);
            emit_selected(&s, ST0_FROM_ST, 5);
            ops[branch].imm = num_ops;
            if (taken) {
                reference(&b, ST0_FROM_ST, 5);
            }
            execute(&a);
            equal(&a, &b);
        }
        /* Execute the real RC10 transition helpers between raw emitters. */
        one(&a, &b, ENTER_MMX, 0, true);
        one(&a, &b, XCHG, 7, true);
        one(&a, &b, CHS, 0, true);
        one(&a, &b, EMMS, 0, true);
        one(&a, &b, PUSH, 0, true);
        /*
         * A modeled fault boundary observes state before the next guest
         * memory operation; raw ops themselves touch CPU state only.
         */
        equal(&a, &b);
    }
    puts("FLD/FSTP/FCMOV/MMX/helper and modeled fault boundaries pass");
}
static void savings(void)
{
    CPUX86State a;
    DisasContext s;
    init(&a);
    start(&s);
    helper_calls = 0;
    for (unsigned kind = 0; kind <= XCHG; kind++) {
        emit_selected(&s, kind, 7);
    }
    execute(&a);
    if (XEMU_OPT_X87_STACK && FIXTURE_ARM64 && helper_calls) {
        fputs("old helper call remains\n", stderr);
        exit(1);
    }
    assert(helper_calls == ((XEMU_OPT_X87_STACK && FIXTURE_ARM64) ? 0 : 6));
    printf("six selected operations: helper calls=%llu, source IR ops=%u\n",
           (unsigned long long)helper_calls, num_ops);
}
int main(void)
{
    g_use_fp_jit = 1;
    assert(!use_raw_x87_stack());
    g_use_fp_jit = 0;
    edges();
    sequences();
    blocks();
    boundaries();
    savings();
    return 0;
}
