/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real Khronos types and full production helper; no Vulkan device required. */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct PGRAPHState { unsigned unused; } PGRAPHState;
static unsigned transitions, calls;
static VkPipelineStageFlags captured_src, captured_dst;
static VkImageMemoryBarrier captured;
static const VkCommandBuffer test_command = (VkCommandBuffer)(uintptr_t)0x101;
static const VkImage test_image = (VkImage)(uintptr_t)0x202;

#define OPT_STAT_INC(name) (transitions++)
#define VK_LOG(...) ((void)0)

static void vkCmdPipelineBarrier(
    VkCommandBuffer command, VkPipelineStageFlags source,
    VkPipelineStageFlags destination, VkDependencyFlags dependency,
    uint32_t memory_count, const VkMemoryBarrier *memory,
    uint32_t buffer_count, const VkBufferMemoryBarrier *buffers,
    uint32_t image_count, const VkImageMemoryBarrier *images)
{
    assert(command == test_command && !dependency);
    assert(!memory_count && !memory && !buffer_count && !buffers);
    assert(image_count == 1 && images);
    calls++;
    captured_src = source;
    captured_dst = destination;
    captured = images[0];
}

#include "image-transition-functions.inc"

typedef struct Transition {
    VkImageLayout old_layout, new_layout;
    VkPipelineStageFlags source, destination;
    VkAccessFlags source_access, destination_access;
} Transition;

#define TOP VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
#define BOTTOM VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
#define COLOR VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
#define FRAGMENT VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
#define TRANSFER VK_PIPELINE_STAGE_TRANSFER_BIT
#define EARLY VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
#define LATE VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
#define COLOR_RW (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | \
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
#define DEPTH_RW (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | \
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
#define SHADER_R VK_ACCESS_SHADER_READ_BIT
#define TRANSFER_R VK_ACCESS_TRANSFER_READ_BIT
#define TRANSFER_W VK_ACCESS_TRANSFER_WRITE_BIT
#define UNDEFINED VK_IMAGE_LAYOUT_UNDEFINED
#define GENERAL VK_IMAGE_LAYOUT_GENERAL
#define DST VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
#define SRC VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
#define SHADER VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
#define ATTACHMENT VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
#define DEPTH VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
#define DEPTH_READ VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
#define PRESENT VK_IMAGE_LAYOUT_PRESENT_SRC_KHR

static const Transition cases[] = {
    { UNDEFINED, GENERAL, TOP, COLOR, 0, COLOR_RW },
    { UNDEFINED, DST, TOP, TRANSFER, 0, TRANSFER_W },
    { UNDEFINED, ATTACHMENT, TOP, COLOR, 0,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT },
    { UNDEFINED, DEPTH, TOP, LATE, 0, DEPTH_RW },
    { DST, SHADER, TRANSFER, FRAGMENT, TRANSFER_W, SHADER_R },
    { DST, ATTACHMENT, TRANSFER, COLOR, TRANSFER_W, COLOR_RW },
    { DST, DEPTH, TRANSFER, EARLY | LATE, TRANSFER_W, DEPTH_RW },
    { DST, SRC, TRANSFER, TRANSFER, TRANSFER_W, TRANSFER_R },
    { SHADER, DST, FRAGMENT, TRANSFER, SHADER_R, TRANSFER_W },
    { SHADER, ATTACHMENT, FRAGMENT, COLOR, SHADER_R, COLOR_RW },
    { ATTACHMENT, SRC, COLOR, TRANSFER,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, TRANSFER_R },
    { ATTACHMENT, DST, COLOR, TRANSFER, COLOR_RW, TRANSFER_W },
    { ATTACHMENT, SHADER, COLOR, FRAGMENT,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, SHADER_R },
    { PRESENT, ATTACHMENT, BOTTOM, COLOR, 0,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT },
    { ATTACHMENT, PRESENT, COLOR, BOTTOM,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0 },
    { GENERAL, SRC, COLOR, TRANSFER,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, TRANSFER_R },
    { GENERAL, DST, COLOR | FRAGMENT, TRANSFER,
      COLOR_RW | SHADER_R, TRANSFER_W },
    { GENERAL, SHADER, COLOR, FRAGMENT,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, SHADER_R },
    { DEPTH, SRC, LATE, TRANSFER,
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, TRANSFER_R },
    { DEPTH_READ, SRC, FRAGMENT, TRANSFER, SHADER_R, TRANSFER_R },
    { DEPTH, DST, EARLY | LATE, TRANSFER, DEPTH_RW, TRANSFER_W },
    { DEPTH_READ, DST, FRAGMENT, TRANSFER, SHADER_R, TRANSFER_W },
    { SRC, ATTACHMENT, TRANSFER, COLOR, TRANSFER_R, COLOR_RW },
    { SRC, DEPTH, TRANSFER, EARLY | LATE, TRANSFER_R, DEPTH_RW },
    { SRC, DEPTH_READ, TRANSFER, FRAGMENT, TRANSFER_R, SHADER_R },
    { DST, DEPTH_READ, TRANSFER, FRAGMENT, TRANSFER_W, SHADER_R },
    { SRC, DST, TRANSFER, TRANSFER, TRANSFER_R, TRANSFER_W },
    { SRC, SHADER, TRANSFER, FRAGMENT, TRANSFER_R, SHADER_R },
    { SRC, GENERAL, TRANSFER, COLOR, TRANSFER_R, COLOR_RW },
    { SRC, PRESENT, TRANSFER, BOTTOM, TRANSFER_R, 0 },
    { DST, GENERAL, TRANSFER, COLOR, TRANSFER_W, COLOR_RW },
    { SHADER, GENERAL, FRAGMENT, COLOR, SHADER_R, COLOR_RW },
};

static const VkFormat formats[] = {
    VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R5G6B5_UNORM_PACK16,
    VK_FORMAT_D16_UNORM, VK_FORMAT_D24_UNORM_S8_UINT,
    VK_FORMAT_D32_SFLOAT_S8_UINT,
};

static void check_ranges(VkFormat format, const Transition *c)
{
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D16_UNORM) {
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    } else if (format == VK_FORMAT_D24_UNORM_S8_UINT ||
               format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    assert(captured.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    assert(!captured.pNext && captured.image == test_image);
    assert(captured.oldLayout == c->old_layout);
    assert(captured.newLayout == c->new_layout);
    assert(captured.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    assert(captured.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    assert(captured.subresourceRange.aspectMask == aspect);
    assert(!captured.subresourceRange.baseMipLevel);
    assert(captured.subresourceRange.levelCount == VK_REMAINING_MIP_LEVELS);
    assert(!captured.subresourceRange.baseArrayLayer);
    assert(captured.subresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS);
}

int main(int argc, char **argv)
{
    bool unchanged_only = argc == 2 && !strcmp(argv[1], "--unchanged-only");
    PGRAPHState pg = {0};
    if (!unchanged_only) {
        /* A prior direct sample must complete before overwriting its image. */
        pgraph_vk_transition_image_layout(&pg, test_command, test_image,
                                         formats[0], GENERAL, DST);
        if (!(captured_src & FRAGMENT) ||
            !(captured.srcAccessMask & SHADER_R)) {
            fputs("GENERAL upload does not order prior fragment sampling\n",
                  stderr);
            return 1;
        }
    }
    unsigned tested = 0, identities = 0;
    for (unsigned f = 0; f < sizeof(formats) / sizeof(formats[0]); f++) {
        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            const Transition *c = &cases[i];
            if (unchanged_only && c->old_layout == GENERAL &&
                c->new_layout == DST) {
                continue;
            }
            calls = transitions = 0;
            pgraph_vk_transition_image_layout(&pg, test_command, test_image,
                                             formats[f], c->old_layout,
                                             c->new_layout);
            assert(calls == 1 && transitions == 1);
            assert(captured_src == c->source && captured_dst == c->destination);
            assert(captured.srcAccessMask == c->source_access);
            assert(captured.dstAccessMask == c->destination_access);
            check_ranges(formats[f], c);
            tested++;

            calls = transitions = 0;
            pgraph_vk_transition_image_layout(&pg, test_command, test_image,
                                             formats[f], c->old_layout,
                                             c->old_layout);
            assert(!calls && !transitions);
            identities++;
        }
    }
    printf("image transitions: %u scopes/ranges/aspects and %u identity "
           "no-ops passed using real Vulkan types%s\n", tested, identities,
           unchanged_only ? " (unchanged branches)" : "");
    return 0;
}
