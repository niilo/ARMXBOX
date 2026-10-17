/*
 * Differential coverage for the setcond/brcond optimizer peephole.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Use the real optimizer with a small IR interpreter. The stubs below provide
 * only its allocation/list plumbing; no TCG backend or guest firmware is used.
 */
#include "qemu/osdep.h"
#include "tcg/optimize.c"
#include "tcg/tcg-common.c"

__thread TCGContext *tcg_ctx;
TCGv_env tcg_env;

static uint8_t pool[128 * 1024] QEMU_ALIGNED(16);

#ifdef CONFIG_DEBUG_TCG
size_t temp_idx(TCGTemp *ts)
{
    size_t idx = ts - tcg_ctx->temps;

    g_assert_cmpuint(idx, <, tcg_ctx->nb_temps);
    return idx;
}

TCGTemp *tcgv_i32_temp(TCGv_i32 v)
{
    TCGTemp *t = (void *)tcg_ctx + (uintptr_t)v;

    temp_idx(t);
    return t;
}
#endif

uint64_t (dup_const)(unsigned vece, uint64_t val)
{
    g_error("unexpected vector operation in scalar test");
}

void *tcg_malloc_internal(TCGContext *s, int size)
{
    g_error("test optimizer pool exhausted");
}

TCGTemp *tcg_temp_new_internal(TCGType type, TCGTempKind kind)
{
    TCGTemp *t = &tcg_ctx->temps[tcg_ctx->nb_temps++];

    g_assert_cmpint(tcg_ctx->nb_temps, <, TCG_MAX_TEMPS);
    t->type = t->base_type = type;
    t->kind = kind;
    return t;
}

TCGTemp *tcg_constant_internal(TCGType type, int64_t val)
{
    TCGTemp *t = tcg_temp_new_internal(type, TEMP_CONST);

    t->val = val;
    return t;
}

static TCGOp *new_op(TCGOpcode opc, TCGType type, unsigned nargs)
{
    TCGOp *op = g_malloc0(sizeof(*op) + nargs * sizeof(TCGArg));

    op->opc = opc;
    op->nargs = nargs;
    TCGOP_TYPE(op) = type;
    tcg_ctx->nb_ops++;
    return op;
}

TCGOp *tcg_op_insert_before(TCGContext *s, TCGOp *old, TCGOpcode opc,
                            TCGType type, unsigned nargs)
{
    TCGOp *op = new_op(opc, type, nargs);

    QTAILQ_INSERT_BEFORE(old, op, link);
    return op;
}

TCGOp *tcg_op_insert_after(TCGContext *s, TCGOp *old, TCGOpcode opc,
                           TCGType type, unsigned nargs)
{
    TCGOp *op = new_op(opc, type, nargs);

    QTAILQ_INSERT_AFTER(&s->ops, old, op, link);
    return op;
}

void tcg_op_remove(TCGContext *s, TCGOp *op)
{
    QTAILQ_REMOVE(&s->ops, op, link);
    QTAILQ_INSERT_TAIL(&s->free_ops, op, link);
    s->nb_ops--;
}

bool tcg_op_supported(TCGOpcode opc, TCGType type, unsigned flags)
{
    return true;
}

int tcg_can_emit_vec_op(TCGOpcode opc, TCGType type, unsigned vece)
{
    return 0;
}

static bool compare(uint64_t a, uint64_t b, TCGType type, TCGCond cond)
{
    int64_t sa, sb;

    if (type == TCG_TYPE_I32) {
        a = (uint32_t)a;
        b = (uint32_t)b;
        sa = (int32_t)a;
        sb = (int32_t)b;
    } else {
        sa = a;
        sb = b;
    }
    switch (cond) {
    case TCG_COND_EQ: return a == b;
    case TCG_COND_NE: return a != b;
    case TCG_COND_LT: return sa < sb;
    case TCG_COND_GE: return sa >= sb;
    case TCG_COND_LE: return sa <= sb;
    case TCG_COND_GT: return sa > sb;
    case TCG_COND_LTU: return a < b;
    case TCG_COND_GEU: return a >= b;
    case TCG_COND_LEU: return a <= b;
    case TCG_COND_GTU: return a > b;
    default:
        g_assert_not_reached();
    }
}

static void interpret(TCGContext *s, uint64_t *values)
{
    TCGOp *op;

    for (int i = 0; i < s->nb_temps; i++) {
        if (s->temps[i].kind == TEMP_CONST) {
            values[i] = s->temps[i].val;
        }
    }
    for (op = QTAILQ_FIRST(&s->ops); op; op = QTAILQ_NEXT(op, link)) {
        TCGArg label = 0;

        switch (op->opc) {
        case INDEX_op_setcond:
            values[temp_idx(arg_temp(op->args[0]))] =
                compare(values[temp_idx(arg_temp(op->args[1]))],
                        values[temp_idx(arg_temp(op->args[2]))],
                        TCGOP_TYPE(op), op->args[3]);
            break;
        case INDEX_op_mov:
            values[temp_idx(arg_temp(op->args[0]))] =
                values[temp_idx(arg_temp(op->args[1]))];
            break;
        case INDEX_op_brcond:
            if (compare(values[temp_idx(arg_temp(op->args[0]))],
                        values[temp_idx(arg_temp(op->args[1]))],
                        TCGOP_TYPE(op), op->args[2])) {
                label = op->args[3];
            }
            break;
        case INDEX_op_br:
            label = op->args[0];
            break;
        case INDEX_op_set_label:
            break;
        default:
            g_error("unexpected test IR opcode: %s", tcg_op_defs[op->opc].name);
        }
        if (label) {
            do {
                op = QTAILQ_NEXT(op, link);
                g_assert_nonnull(op);
            } while (op->opc != INDEX_op_set_label || op->args[0] != label);
        }
    }
}

static TCGOp *append_op(TCGOpcode opc, TCGType type, unsigned nargs)
{
    TCGOp *op = new_op(opc, type, nargs);

    QTAILQ_INSERT_TAIL(&tcg_ctx->ops, op, link);
    return op;
}

static void run_case(TCGType type, TCGTempKind kind, unsigned alias,
                     TCGCond cond, TCGCond branch_cond, uint64_t a, uint64_t b)
{
    TCGContext *s = g_new0(TCGContext, 1);
    uint64_t expected[TCG_MAX_TEMPS] = { 0 };
    uint64_t actual[TCG_MAX_TEMPS] = { 0 };
    TCGTemp *out_fall, *out_label, *ta, *tb, *dst, *zero;
    TCGOp *op;

    tcg_ctx = s;
    s->pool_cur = (uintptr_t)pool;
    s->pool_end = (uintptr_t)pool + sizeof(pool);
    QTAILQ_INIT(&s->ops);
    QTAILQ_INIT(&s->free_ops);
    out_fall = tcg_temp_new_internal(type, TEMP_GLOBAL);
    out_label = tcg_temp_new_internal(type, TEMP_GLOBAL);
    ta = tcg_temp_new_internal(type, kind);
    tb = tcg_temp_new_internal(type, kind);
    dst = alias == 1 ? ta : alias == 2 ? tb :
          tcg_temp_new_internal(type, kind);
    zero = tcg_constant_internal(type, 0);
    op = append_op(INDEX_op_setcond, type, 4);
    op->args[0] = temp_arg(dst);
    op->args[1] = temp_arg(ta);
    op->args[2] = temp_arg(tb);
    op->args[3] = cond;
    op = append_op(INDEX_op_brcond, type, 4);
    op->args[0] = temp_arg(dst);
    op->args[1] = temp_arg(zero);
    op->args[2] = branch_cond;
    op->args[3] = 1;
    op = append_op(INDEX_op_mov, type, 2);
    op->args[0] = temp_arg(out_fall);
    op->args[1] = temp_arg(dst);
    op = append_op(INDEX_op_set_label, type, 1);
    op->args[0] = 1;
    if (kind == TEMP_TB) {
        op = append_op(INDEX_op_mov, type, 2);
        op->args[0] = temp_arg(out_label);
        op->args[1] = temp_arg(dst);
    }
    expected[temp_idx(ta)] = a;
    expected[temp_idx(tb)] = b;
    expected[temp_idx(out_fall)] = 0xdead;
    expected[temp_idx(out_label)] = 0xbeef;
    memcpy(actual, expected, sizeof(actual));
    interpret(s, expected);
    tcg_optimize(s);
    interpret(s, actual);
    g_assert_cmpuint(actual[temp_idx(out_fall)], ==,
                     expected[temp_idx(out_fall)]);
    g_assert_cmpuint(actual[temp_idx(out_label)], ==,
                     expected[temp_idx(out_label)]);
    while ((op = QTAILQ_FIRST(&s->ops))) {
        QTAILQ_REMOVE(&s->ops, op, link);
        g_free(op);
    }
    while ((op = QTAILQ_FIRST(&s->free_ops))) {
        QTAILQ_REMOVE(&s->free_ops, op, link);
        g_free(op);
    }
    g_free(s);
}

int main(void)
{
    static const TCGCond conds[] = {
        TCG_COND_EQ, TCG_COND_NE, TCG_COND_LT, TCG_COND_GE,
        TCG_COND_LE, TCG_COND_GT, TCG_COND_LTU, TCG_COND_GEU,
        TCG_COND_LEU, TCG_COND_GTU,
    };
    static const TCGCond branches[] = {
        TCG_COND_EQ, TCG_COND_NE, TCG_COND_LT,
    };
    static const uint64_t values[] = {
        0, 1, 2, INT32_MAX, 0x80000000, UINT32_MAX,
        INT64_MAX, 0x8000000000000000ULL, UINT64_MAX,
        0x123456789abcdef0ULL,
    };
    unsigned cases = 0;

    for (TCGType type = TCG_TYPE_I32; type <= TCG_TYPE_I64; type++) {
        for (TCGTempKind kind = TEMP_EBB; kind <= TEMP_TB; kind++) {
            for (unsigned alias = 0; alias < 3; alias++) {
                for (unsigned c = 0; c < ARRAY_SIZE(conds); c++) {
                    for (unsigned br = 0; br < ARRAY_SIZE(branches); br++) {
                        for (unsigned a = 0; a < ARRAY_SIZE(values); a++) {
                            for (unsigned b = 0; b < ARRAY_SIZE(values); b++) {
                                run_case(type, kind, alias, conds[c],
                                         branches[br], values[a], values[b]);
                                cases++;
                            }
                        }
                    }
                }
            }
        }
    }
    printf("setcond/brcond differential cases passed: %u (peephole=%d)\n",
           cases, XEMU_OPT_TCG_PEEPHOLE);
    return 0;
}
