/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Native execution fixture: actual TCG optimizer, liveness, register allocator
 * and x86 backend. Branch selection and CPU floatx80 type are fixture-gated.
 */
#include "qemu/osdep.h"
/* Select the ARM floatx80 declaration without changing host libc or TCG. */
#undef XBOX
#include "fpu/softfloat-types.h"
#define XBOX 1
#include "cpu.h"
#include "exec/translation-block.h"
#include "tcg/tcg-op.h"
#include "tcg/startup.h"
#include "tcg/helper-info.h"
#include "qemu/cacheinfo.h"

static TCGv_i32 fpstt;
static int g_use_fp_jit;
typedef struct TCGv_fp_d *TCGv_fp;
typedef struct DisasContext {
    uint32_t flags;
    bool flcr_set;
    int fpstt_delta;
    TCGv_fp fpregs[8];
    TCGv_fp ft0;
    bool fip_pend;
    int fdp_pend_seg;
    TCGv fip_val;
    TCGv fdp_val;
    TCGv_i32 fip_sel;
} DisasContext;
#define ST(n) (env->fpregs[(env->fpstt + (n)) & 7].d)
#define ST0 ST(0)
#define FT0 (env->ft0)
#define XEMU_FPU_PROFILE_SCOPE(...) do { } while (0)
#include "reference.inc"
#include "emitters.inc"

enum Kind { PUSH, POP, FT_FROM_ST, ST0_FROM_ST, ST_FROM_ST0, XCHG };
typedef struct Step { enum Kind kind; int index; } Step;
typedef struct Program {
    const void *code;
    unsigned bytes, calls, ops, steps;
    bool each, mixed;
    Step plan[32];
} Program;

static CPUX86State expected[64];
static unsigned observed, checks, helper_calls;
static uint64_t random_state = 0x987654321ULL;
static uint32_t rng(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
}

static void normalize(CPUX86State *env)
{
    for (unsigned i = 0; i < 9; i++) {
        floatx80 *f = i == 8 ? &env->ft0 : &env->fpregs[i].d;
        memset((uint8_t *)f + 10, 0, 6);
    }
}

static void equal(const CPUX86State *actual, const CPUX86State *wanted)
{
    CPUX86State a, b;
    memcpy(&a, actual, sizeof(a));
    memcpy(&b, wanted, sizeof(b));
    normalize(&a);
    normalize(&b);
    if (memcmp(&a, &b, sizeof(a))) {
        for (size_t i = 0; i < sizeof(a); i++) {
            if (((uint8_t *)&a)[i] != ((uint8_t *)&b)[i]) {
                fprintf(stderr, "state mismatch check=%u offset=%zu "
                        "actual=%02x expected=%02x top=%u/%u\n",
                        checks, i, ((uint8_t *)&a)[i], ((uint8_t *)&b)[i],
                        a.fpstt, b.fpstt);
                exit(1);
            }
        }
    }
    checks++;
}

static void observer(CPUX86State *env, uint32_t id)
{
    equal(env, &expected[id]);
    observed++;
}

/*
 * Mutate a TCG global from C to test invalidation and reloading after helpers,
 * as well as synchronization before their entry.
 */
static void mutator(CPUX86State *env)
{
    env->fpstt = (env->fpstt + 3) & 7;
    env->fpus ^= 0x4500;
    env->fpregs[(env->fpstt + 5) & 7].d.low ^= 0x123456789abcdef0ULL;
    helper_calls++;
}

#define INFO1(fn) { .func = fn, .name = #fn, .typemask = (6 << 3) }
#define INFO2(fn) { .func = fn, .name = #fn, .typemask = (6 << 3) | (2 << 6) }
static TCGHelperInfo observer_info = INFO2(observer);
static TCGHelperInfo mutator_info = INFO1(mutator);
static TCGHelperInfo ref_info[] = {
    INFO1(helper_fpush), INFO1(helper_fpop), INFO2(helper_fmov_FT0_STN),
    INFO2(helper_fmov_ST0_STN), INFO2(helper_fmov_STN_ST0),
    INFO2(helper_fxchg_ST0_STN),
};
static TCGHelperInfo mmx_info = INFO1(helper_enter_mmx);
static TCGHelperInfo emms_info = INFO1(helper_emms);

static void call1(TCGHelperInfo *info)
{
    tcg_gen_call1(info->func, info, NULL, tcgv_ptr_temp(tcg_env));
}
static void call2(TCGHelperInfo *info, unsigned arg)
{
    tcg_gen_call2(info->func, info, NULL, tcgv_ptr_temp(tcg_env),
                   tcgv_i32_temp(tcg_constant_i32(arg)));
}
static void emit_raw(DisasContext *s, Step step)
{
    switch (step.kind) {
    case PUSH:
        gen_fpush(s);
        break;
    case POP:
        gen_fpop(s);
        break;
    case FT_FROM_ST:
        gen_fmov_FT0_STN(s, step.index);
        break;
    case ST0_FROM_ST:
        gen_fmov_ST0_STN(s, step.index);
        break;
    case ST_FROM_ST0:
        gen_fmov_STN_ST0(s, step.index);
        break;
    case XCHG:
        gen_fxchg_ST0_STN(s, step.index);
        break;
    }
}
static void apply_reference(CPUX86State *env, Step step)
{
    switch (step.kind) {
    case PUSH:
        helper_fpush(env);
        break;
    case POP:
        helper_fpop(env);
        break;
    case FT_FROM_ST:
        helper_fmov_FT0_STN(env, step.index);
        break;
    case ST0_FROM_ST:
        helper_fmov_ST0_STN(env, step.index);
        break;
    case ST_FROM_ST0:
        helper_fmov_STN_ST0(env, step.index);
        break;
    case XCHG:
        helper_fxchg_ST0_STN(env, step.index);
        break;
    }
}
static void pending(DisasContext *s, unsigned step)
{
    s->fip_pend = true;
    s->fip_val = tcg_constant_i32(0x60000000 + step);
    s->fdp_pend_seg = step % 6;
    s->fdp_val = tcg_constant_i32(0x70000000 + step);
}
static void pending_reference(CPUX86State *env, unsigned step)
{
    uint32_t ip = 0x60000000 + step, dp = 0x70000000 + step;
    memcpy(&env->fpip, &ip, sizeof(ip));
    memcpy(&env->fpdp, &dp, sizeof(dp));
    env->fpcs = env->segs[R_CS].selector;
    env->fpds = env->segs[step % 6].selector;
}

static Program compile_program(const Step *steps, unsigned count, bool raw,
                                bool each, bool mixed)
{
    TCGContext *s = tcg_ctx;
    DisasContext dc = { .fdp_pend_seg = -1 };
    Program p = { .steps = count, .each = each, .mixed = mixed };
    memcpy(p.plan, steps, count * sizeof(*steps));
    TranslationBlock *tb = tcg_tb_alloc(s);
    assert(tb);
    memset(tb, 0, sizeof(*tb));
    tb->tc.ptr = s->code_gen_ptr;
    tb->icount = 1;
    s->gen_tb = tb;
    s->addr_type = TCG_TYPE_I32;
    s->disas_ctx = &dc;
    tcg_func_start(s);
    tcg_gen_insn_start(0, 0);
    for (unsigned i = 0; i < count; i++) {
        pending(&dc, i);
        if (raw) {
            emit_raw(&dc, steps[i]);
        } else if (steps[i].kind <= POP) {
            call1(&ref_info[steps[i].kind]);
        } else {
            call2(&ref_info[steps[i].kind], steps[i].index);
        }
        if (each) {
            call2(&observer_info, i);
        }
        if (mixed && i % 4 == 1) {
            call1(&mutator_info);
        }
        if (mixed && i % 4 == 2) {
            call1(&mmx_info);
        }
        if (mixed && i % 4 == 3) {
            call1(&emms_info);
        }
    }
    tcg_gen_exit_tb(NULL, 0);
    TCGOp *op;
    QTAILQ_FOREACH(op, &s->ops, link) {
        p.ops++;
        if (op->opc == INDEX_op_call) {
            p.calls++;
        }
    }
    unsigned expected_calls = (raw ? 0 : count) + (each ? count : 0);
    if (mixed) {
        for (unsigned i = 0; i < count; i++) {
            expected_calls += i % 4 != 0;
        }
    }
    assert(p.calls == expected_calls);
    int size = tcg_gen_code(s, tb, 0);
    assert(size > 0);
    p.bytes = size;
    p.code = tb->tc.ptr;
    s->code_gen_ptr = s->code_ptr;
    return p;
}

static void execute(const Program *p, unsigned top, unsigned tags)
{
    CPUX86State actual, wanted;
    for (size_t i = 0; i < sizeof(actual); i++) {
        ((uint8_t *)&actual)[i] = rng();
    }
    actual.fpstt = top;
    for (unsigned i = 0; i < 8; i++) {
        actual.fptags[i] = (tags >> i) & 1;
    }
    memcpy(&wanted, &actual, sizeof(wanted));
    for (unsigned i = 0; i < p->steps; i++) {
        pending_reference(&wanted, i);
        apply_reference(&wanted, p->plan[i]);
        memcpy(&expected[i], &wanted, sizeof(wanted));
        if (p->mixed && i % 4 == 1) {
            mutator(&wanted);
        }
        if (p->mixed && i % 4 == 2) {
            helper_enter_mmx(&wanted);
        }
        if (p->mixed && i % 4 == 3) {
            helper_emms(&wanted);
        }
    }
    observed = 0;
    tcg_qemu_tb_exec(&actual, p->code);
    equal(&actual, &wanted);
    assert(observed == (p->each ? p->steps : 0));
}

int main(void)
{
#ifdef NEGATIVE_NO_SYNC
    /* False helper metadata must expose canonical TOP staleness. */
    observer_info.flags = TCG_CALL_NO_RWG;
#endif
    QEMU_BUILD_BUG_ON(sizeof(floatx80) != 16);
    QEMU_BUILD_BUG_ON(sizeof(FPReg) != 16);
    tcg_init(32 * 1024 * 1024, 0, 1);
    fpstt = tcg_global_mem_new_i32(tcg_env, offsetof(CPUX86State, fpstt),
                                  "fpstt");
    tcg_prologue_init();
    tcg_register_thread();
    unsigned programs = 0;
    for (unsigned raw = 0; raw < 2; raw++) {
        for (unsigned kind = 0; kind < 6; kind++) {
            for (unsigned index = 0; index < 8; index++) {
                Step step = { kind, index };
                Program p = compile_program(&step, 1, raw, true, false);
                for (unsigned top = 0; top < 8; top++) {
                    for (unsigned tags = 0; tags < 256; tags += 17) {
                        execute(&p, top, tags);
                    }
                }
                programs++;
            }
        }
        Step sequence[32];
        for (unsigned i = 0; i < 32; i++) {
            sequence[i] = (Step){ i % 6, (i * 7) & 7 };
        }
        for (unsigned mode = 0; mode < 3; mode++) {
            Program p = compile_program(sequence, 32, raw,
                                        mode != 0, mode == 2);
            for (unsigned i = 0; i < 1000; i++) {
                execute(&p, rng() & 7, rng() & 255);
            }
            printf("%s mode=%u selected=32 IRops=%u "
                   "helper_calls=%u host_code=%u bytes\n",
                   raw ? "raw" : "RC10 helpers", mode, p.ops, p.calls, p.bytes);
            programs++;
        }
    }
    printf("PASS: %u actual-TCG machine-code programs, %u state comparisons, "
           "canonical TOP/tags/FIP/FDP observed at helper entry and TB exit\n",
           programs, checks);
    return 0;
}
