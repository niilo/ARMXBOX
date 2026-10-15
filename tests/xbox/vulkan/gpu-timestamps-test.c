/*
 * Exercise real Vulkan timestamp accounting with deterministic query results.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef GPU_TIMESTAMPS_DRAW_SOURCE
#define GPU_TIMESTAMPS_DRAW_SOURCE \
    "../../../hw/xbox/nv2a/pgraph/vk/draw.c"
#endif
#include GPU_TIMESTAMPS_DRAW_SOURCE

/* Generated from the actual end-recording timestamp block in draw.c. */
#include "gpu-timestamps-recording.h"

NV2AStats g_nv2a_stats;

static uint64_t query_data[NUM_SUBMIT_FRAMES][GPU_TS_QUERIES_PER_CB];
static VkResult query_result[NUM_SUBMIT_FRAMES];
static unsigned query_calls;
static unsigned begin_calls;
static unsigned reset_calls;
static unsigned timestamp_calls;

static VKAPI_ATTR VkResult VKAPI_CALL fake_get_query_results(
    VkDevice device, VkQueryPool pool, uint32_t first, uint32_t count,
    size_t size, void *data, VkDeviceSize stride, VkQueryResultFlags flags)
{
    unsigned frame = first / GPU_TS_QUERIES_PER_CB;

    g_assert_cmpuint(frame, <, NUM_SUBMIT_FRAMES);
    g_assert_cmpuint(first % GPU_TS_QUERIES_PER_CB, ==, 0);
    g_assert_cmpuint(count, <=, GPU_TS_QUERIES_PER_CB);
    g_assert_cmpuint(size, ==, count * sizeof(uint64_t));
    g_assert_cmpuint(stride, ==, sizeof(uint64_t));
    /* Accounting must not introduce WAIT_BIT or any new GPU wait. */
    g_assert_cmpuint(flags, ==, VK_QUERY_RESULT_64_BIT);
    query_calls++;
    if (query_result[frame] == VK_SUCCESS) {
        memcpy(data, query_data[frame], size);
    }
    return query_result[frame];
}

static VKAPI_ATTR VkResult VKAPI_CALL fake_begin_command_buffer(
    VkCommandBuffer buffer, const VkCommandBufferBeginInfo *info)
{
    g_assert_cmpuint(info->sType, ==,
                     VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin_calls++;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL fake_reset_query_pool(
    VkCommandBuffer buffer, VkQueryPool pool, uint32_t first, uint32_t count)
{
    g_assert_cmpuint(first % GPU_TS_QUERIES_PER_CB, ==, 0);
    g_assert_cmpuint(count, ==, GPU_TS_QUERIES_PER_CB);
    /* Recording a reset does not execute it or erase old GPU values yet. */
    reset_calls++;
}

static VKAPI_ATTR void VKAPI_CALL fake_write_timestamp(
    VkCommandBuffer buffer, VkPipelineStageFlagBits stage,
    VkQueryPool pool, uint32_t query)
{
    g_assert_cmpuint(query % GPU_TS_QUERIES_PER_CB, ==, 0);
    g_assert_cmpuint(stage, ==, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    timestamp_calls++;
}

PFN_vkGetQueryPoolResults vkGetQueryPoolResults = fake_get_query_results;
PFN_vkBeginCommandBuffer vkBeginCommandBuffer = fake_begin_command_buffer;
PFN_vkCmdResetQueryPool vkCmdResetQueryPool = fake_reset_query_pool;
PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp = fake_write_timestamp;

typedef struct TestState {
    PGRAPHState *pg;
    PGRAPHVkState *r;
} TestState;

static TestState test_state_new(void)
{
    TestState s = {
        .pg = g_new0(PGRAPHState, 1),
        .r = g_new0(PGRAPHVkState, 1),
    };
    s.pg->vk_renderer_state = s.r;
    s.r->gpu_ts_supported = true;
    s.r->gpu_ts_period_ns = 2.0f;
    memset(&g_nv2a_stats, 0, sizeof(g_nv2a_stats));
    memset(query_data, 0, sizeof(query_data));
    memset(query_result, 0, sizeof(query_result));
    query_calls = begin_calls = reset_calls = timestamp_calls = 0;
    return s;
}

static void test_state_free(TestState *s)
{
    g_free(s->r);
    g_free(s->pg);
}

static void begin_recording(TestState *s, int frame)
{
    s->r->current_frame = frame;
    s->r->in_command_buffer = false;
    pgraph_vk_begin_command_buffer(s->pg);
}

static void complete_recording(TestState *s, int frame, int rp_count,
                               unsigned multiplier)
{
    PGRAPHVkState *r = s->r;

    r->current_frame = frame;
    r->gpu_ts_rp_index = rp_count;
    gpu_timestamps_recording_complete(r);

    query_data[frame][0] = 1000;
    query_data[frame][1] = 1000 + 1000 * multiplier;
    for (int i = 0; i < rp_count; i++) {
        query_data[frame][2 + i * 2] = 1100 + 100 * i;
        query_data[frame][3 + i * 2] = 1100 + 100 * i + 100 * multiplier;
    }
    /* Model worker ownership independently of query accounting. */
    r->frame_submitted[frame] = true;
    query_result[frame] = VK_SUCCESS;
}

static void assert_totals(int64_t total, int64_t render, int count)
{
    FramePhaseTimingWork *w = &g_nv2a_stats.phase_working;

    g_assert_cmpint(w->gpu_total_ns, ==, total);
    g_assert_cmpint(w->gpu_render_ns, ==, render);
    g_assert_cmpint(w->gpu_nonrender_ns, ==, total - render);
    g_assert_cmpint(w->gpu_rp_count, ==, count);
}

static void test_two_slot_synchronous(void)
{
    TestState s = test_state_new();

    begin_recording(&s, 0);
    complete_recording(&s, 0, 2, 1);
    gpu_ts_readback(s.r, 0); /* Immediate synchronous finish. */
    assert_totals(2000, 400, 2);
    begin_recording(&s, 1);
    complete_recording(&s, 1, 1, 2);
    gpu_ts_readback(s.r, 1);
    assert_totals(6000, 800, 3);

    gpu_ts_readback(s.r, 0); /* Slot rotation before query reset. */
    gpu_ts_readback(s.r, 1); /* A later flush can revisit it too. */
    assert_totals(6000, 800, 3);
    g_assert_cmpuint(query_calls, ==, 2);
    g_assert_true(s.r->frame_submitted[0]);
    g_assert_true(s.r->frame_submitted[1]);
    test_state_free(&s);
}

static void test_deferred_and_multi_slot_flush(void)
{
    TestState s = test_state_new();
    int64_t total = 0, render = 0;
    int passes = 0;

    for (int i = 0; i < NUM_SUBMIT_FRAMES; i++) {
        begin_recording(&s, i);
        complete_recording(&s, i, i, i + 1);
        total += 2000 * (i + 1);
        render += 200 * i * (i + 1);
        passes += i;
    }
    assert_totals(0, 0, 0); /* Deferred: no immediate result collection. */
    for (int i = NUM_SUBMIT_FRAMES - 1; i >= 0; i--) {
        gpu_ts_readback(s.r, i);
    }
    assert_totals(total, render, passes);
    /* The same readback calls as slot cleanup / flush, without changing it. */
    for (int i = 0; i < NUM_SUBMIT_FRAMES; i++) {
        gpu_ts_readback(s.r, i);
        g_assert_true(s.r->frame_submitted[i]);
    }
    assert_totals(total, render, passes);
    g_assert_cmpuint(query_calls, ==, NUM_SUBMIT_FRAMES);
    test_state_free(&s);
}

static void test_unavailable_retry(void)
{
    TestState s = test_state_new();

    begin_recording(&s, 0);
    complete_recording(&s, 0, 1, 1);
    query_result[0] = VK_NOT_READY;
    gpu_ts_readback(s.r, 0);
    assert_totals(0, 0, 0);
    g_assert_true(s.r->gpu_ts_pending[0]);
    query_result[0] = VK_ERROR_UNKNOWN;
    gpu_ts_readback(s.r, 0);
    assert_totals(0, 0, 0);
    g_assert_true(s.r->gpu_ts_pending[0]);
    query_result[0] = VK_SUCCESS;
    gpu_ts_readback(s.r, 0);
    assert_totals(2000, 200, 1);
    g_assert_false(s.r->gpu_ts_pending[0]);
    gpu_ts_readback(s.r, 0);
    g_assert_cmpuint(query_calls, ==, 3);
    assert_totals(2000, 200, 1);
    test_state_free(&s);
}

static void test_reuse_and_unsubmitted_recording(void)
{
    TestState s = test_state_new();

    begin_recording(&s, 0);
    complete_recording(&s, 0, 1, 1);
    gpu_ts_readback(s.r, 0);
    assert_totals(2000, 200, 1);
    begin_recording(&s, 0);
    /* Old GPU values remain available until the recorded reset executes. */
    gpu_ts_readback(s.r, 0);
    assert_totals(2000, 200, 1);
    g_assert_cmpuint(query_calls, ==, 1);
    complete_recording(&s, 0, 2, 3);
    gpu_ts_readback(s.r, 0);
    gpu_ts_readback(s.r, 0);
    assert_totals(8000, 1400, 3);
    g_assert_cmpuint(query_calls, ==, 2);
    g_assert_cmpuint(begin_calls, ==, 2);
    g_assert_cmpuint(reset_calls, ==, 2);
    g_assert_cmpuint(timestamp_calls, ==, 2);
    test_state_free(&s);
}

static void test_disabled_and_empty_slot(void)
{
    TestState s = test_state_new();

    gpu_ts_readback(s.r, 0);
    g_assert_cmpuint(query_calls, ==, 0);
    s.r->gpu_ts_supported = false;
    s.r->gpu_ts_pending[0] = true;
    gpu_ts_readback(s.r, 0);
    begin_recording(&s, 0);
    g_assert_cmpuint(query_calls, ==, 0);
    g_assert_cmpuint(reset_calls, ==, 0);
    g_assert_cmpuint(timestamp_calls, ==, 0);
    assert_totals(0, 0, 0);
    test_state_free(&s);
}

static void test_unavailable_generation_replaced(void)
{
    TestState s = test_state_new();

    begin_recording(&s, 0);
    complete_recording(&s, 0, 1, 1);
    query_result[0] = VK_NOT_READY;
    gpu_ts_readback(s.r, 0);
    g_assert_true(s.r->gpu_ts_pending[0]);

    /*
     * Once the caller has made this slot reusable, a new recording retires
     * the unavailable old measurement. Its still-available old query values
     * must not become the new recording's result before submission.
     */
    begin_recording(&s, 0);
    query_result[0] = VK_SUCCESS;
    gpu_ts_readback(s.r, 0);
    assert_totals(0, 0, 0);
    g_assert_cmpuint(query_calls, ==, 1);
    complete_recording(&s, 0, 2, 3);
    gpu_ts_readback(s.r, 0);
    assert_totals(6000, 1200, 2);
    g_assert_cmpuint(query_calls, ==, 2);
    test_state_free(&s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/gpu-timestamps/01-two-slot-synchronous",
                    test_two_slot_synchronous);
    g_test_add_func("/gpu-timestamps/deferred-and-multi-slot-flush",
                    test_deferred_and_multi_slot_flush);
    g_test_add_func("/gpu-timestamps/unavailable-retry",
                    test_unavailable_retry);
    g_test_add_func("/gpu-timestamps/reuse-and-unsubmitted-recording",
                    test_reuse_and_unsubmitted_recording);
    g_test_add_func("/gpu-timestamps/disabled-and-empty-slot",
                    test_disabled_and_empty_slot);
    g_test_add_func("/gpu-timestamps/unavailable-generation-replaced",
                    test_unavailable_generation_replaced);
    return g_test_run();
}
