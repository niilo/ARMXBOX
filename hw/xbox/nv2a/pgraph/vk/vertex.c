/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024 Matt Borgerson
 *
 * Based on GL implementation:
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2024 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "renderer.h"

VkDeviceSize pgraph_vk_update_index_buffer(PGRAPHState *pg, void *data,
                                           VkDeviceSize size)
{
    nv2a_profile_inc_counter(NV2A_PROF_GEOM_BUFFER_UPDATE_2);
    return pgraph_vk_append_to_buffer(pg, BUFFER_INDEX_STAGING, &data, &size, 1,
                                      1);
}

VkDeviceSize pgraph_vk_update_vertex_inline_buffer(PGRAPHState *pg, void **data,
                                                   VkDeviceSize *sizes,
                                                   size_t count)
{
    nv2a_profile_inc_counter(NV2A_PROF_GEOM_BUFFER_UPDATE_3);
    return pgraph_vk_append_to_buffer(pg, BUFFER_VERTEX_INLINE_STAGING, data,
                                      sizes, count, 1);
}

bool pgraph_vk_init_vertex_ram_state(PGRAPHVkState *r, FrameStagingState *fs,
                                    Error **errp)
{
    assert(!fs->uploaded_bitmap && !fs->vertex_ram_stale);
    fs->vertex_ram_flush_min = VK_WHOLE_SIZE;
    fs->vertex_ram_flush_max = 0;
    fs->vertex_ram_initialized = false;
    fs->uploaded_bitmap = bitmap_new(r->bitmap_size);
    fs->vertex_ram_stale = bitmap_new(r->bitmap_size);
    if (!fs->uploaded_bitmap || !fs->vertex_ram_stale) {
        error_setg(errp, "Failed to allocate per-frame vertex RAM bitmaps");
        pgraph_vk_finalize_vertex_ram_state(fs);
        return false;
    }
    return true;
}

void pgraph_vk_finalize_vertex_ram_state(FrameStagingState *fs)
{
    g_free(fs->uploaded_bitmap);
    fs->uploaded_bitmap = NULL;
    g_free(fs->vertex_ram_stale);
    fs->vertex_ram_stale = NULL;
    fs->vertex_ram_initialized = false;
    fs->vertex_ram_flush_min = VK_WHOLE_SIZE;
    fs->vertex_ram_flush_max = 0;
}

/*
 * Called after either CPU writer updates the current, authoritative mirror.
 * Each other destination retains its own debt across multiple rotations.
 */
void pgraph_vk_vertex_ram_written(PGRAPHVkState *r, hwaddr offset,
                                  VkDeviceSize size)
{
    FrameStagingState *fs = &r->frame_staging[r->current_frame];
    size_t total = fs->vertex_ram.buffer_size;

    assert(offset <= total && size <= total - offset);
    if (!size) {
        return;
    }
    size_t start_bit = offset / TARGET_PAGE_SIZE;
    size_t end_bit = DIV_ROUND_UP(offset + size, TARGET_PAGE_SIZE);
    assert(end_bit <= r->bitmap_size);
    fs->vertex_ram_flush_min = MIN(fs->vertex_ram_flush_min, offset);
    fs->vertex_ram_flush_max = MAX(fs->vertex_ram_flush_max, offset + size);
    bitmap_set(fs->uploaded_bitmap, start_bit, end_bit - start_bit);
    for (int i = 0; i < r->num_active_frames; i++) {
        if (i != r->current_frame) {
            bitmap_set(r->frame_staging[i].vertex_ram_stale,
                       start_bit, end_bit - start_bit);
        }
    }
}

/*
 * The caller has completed the existing destination-slot fence wait. No
 * synchronization is weakened here: only the host memcpy selection changes.
 */
void pgraph_vk_propagate_vertex_ram(PGRAPHVkState *r, int next_frame)
{
    assert(next_frame >= 0 && next_frame < r->num_active_frames);
    FrameStagingState *cur = &r->frame_staging[r->current_frame];
    FrameStagingState *next = &r->frame_staging[next_frame];

#if NV2A_PERF_LOG
    g_nv2a_stats.vram_working.rotations++;
#endif
    if (next_frame == r->current_frame) {
        return;
    }
    size_t total = cur->vertex_ram.buffer_size;
    assert(total == next->vertex_ram.buffer_size);
    assert(r->bitmap_size == DIV_ROUND_UP(total, TARGET_PAGE_SIZE));
    memcpy(next->uploaded_bitmap, cur->uploaded_bitmap,
           BITS_TO_LONGS(r->bitmap_size) * sizeof(unsigned long));
    next->vertex_ram_flush_min = VK_WHOLE_SIZE;
    next->vertex_ram_flush_max = 0;
    if (!next->vertex_ram_initialized) {
#if NV2A_PERF_LOG
        int64_t first_copy_start = nv2a_clock_ns();
#endif
        memcpy(next->vertex_ram.mapped, cur->vertex_ram.mapped, total);
#if NV2A_PERF_LOG
        int64_t first_copy_ns = nv2a_clock_ns() - first_copy_start;
        g_nv2a_stats.vram_working.first_copy_ns += MAX(first_copy_ns, 0);
        g_nv2a_stats.vram_working.first_copy_runs++;
        g_nv2a_stats.vram_working.first_copy_bytes += total;
#endif
        next->vertex_ram_flush_min = 0;
        next->vertex_ram_flush_max = total;
        next->vertex_ram_initialized = true;
    } else {
        size_t first = find_next_bit(next->vertex_ram_stale, r->bitmap_size, 0);
        while (first < r->bitmap_size) {
            size_t end = find_next_zero_bit(next->vertex_ram_stale,
                                            r->bitmap_size, first);
            size_t offset = first * TARGET_PAGE_SIZE;
            size_t limit = MIN(end * TARGET_PAGE_SIZE, total);
            size_t size = limit - offset;
#if NV2A_PERF_LOG
            int64_t span_copy_start = nv2a_clock_ns();
#endif
            memcpy(next->vertex_ram.mapped + offset,
                   cur->vertex_ram.mapped + offset, size);
#if NV2A_PERF_LOG
            int64_t span_copy_ns = nv2a_clock_ns() - span_copy_start;
            g_nv2a_stats.vram_working.span_copy_ns += MAX(span_copy_ns, 0);
            g_nv2a_stats.vram_working.span_copy_runs++;
            g_nv2a_stats.vram_working.span_copy_bytes += size;
#endif
            next->vertex_ram_flush_min = MIN(next->vertex_ram_flush_min,
                                             offset);
            next->vertex_ram_flush_max = MAX(next->vertex_ram_flush_max, limit);
            first = find_next_bit(next->vertex_ram_stale, r->bitmap_size, end);
        }
    }
    bitmap_clear(next->vertex_ram_stale, 0, r->bitmap_size);
}

static void trim_unchanged_vertex_ram_pages(const StorageBuffer *vram,
                                             hwaddr *offset,
                                             const uint8_t **data,
                                             VkDeviceSize *size)
{
    if (*offset % TARGET_PAGE_SIZE || *size % TARGET_PAGE_SIZE) {
        if (!memcmp(vram->mapped + *offset, *data, *size)) {
            *size = 0;
        }
        return;
    }

    while (*size && !memcmp(vram->mapped + *offset, *data,
                            TARGET_PAGE_SIZE)) {
        *offset += TARGET_PAGE_SIZE;
        *data += TARGET_PAGE_SIZE;
        *size -= TARGET_PAGE_SIZE;
    }
    /* The first remaining page differs, so do not compare it twice. */
    while (*size > TARGET_PAGE_SIZE &&
           !memcmp(vram->mapped + *offset + *size - TARGET_PAGE_SIZE,
                   *data + *size - TARGET_PAGE_SIZE, TARGET_PAGE_SIZE)) {
        *size -= TARGET_PAGE_SIZE;
    }
}

void pgraph_vk_update_vertex_ram_buffer(PGRAPHState *pg, hwaddr offset,
                                        void *data, VkDeviceSize size)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    pgraph_vk_download_surfaces_in_range_if_dirty(pg, offset, size);

    StorageBuffer *vram = get_staging_buffer(r, BUFFER_VERTEX_RAM);
    assert(offset <= vram->buffer_size && size <= vram->buffer_size - offset);
    if (!size) {
        return;
    }
    const uint8_t *source = data;
    size_t start_bit = offset / TARGET_PAGE_SIZE;
    size_t end_bit = TARGET_PAGE_ALIGN(offset + size) / TARGET_PAGE_SIZE;
    size_t nbits = end_bit - start_bit;
    bool overlaps = find_next_bit(r->cb_vertex_pages, end_bit,
                                   start_bit) < end_bit;

    FrameStagingState *fs = &r->frame_staging[r->current_frame];
    if (overlaps && fs->vertex_ram_initialized &&
        find_next_zero_bit(fs->uploaded_bitmap, end_bit,
                           start_bit) == end_bit) {
        hwaddr requested_offset = offset;
        VkDeviceSize requested_size = size;
#if NV2A_PERF_LOG
        g_nv2a_stats.vram_working.upload_checks++;
        g_nv2a_stats.vram_working.upload_check_bytes += size;
#endif
        /*
         * A dirty page can still contain identical vertex data. Compare only
         * potential hazards, after surface downloads have made data current.
         * Skipped bytes are never written while recorded draws read them.
         */
        trim_unchanged_vertex_ram_pages(vram, &offset, &source, &size);
        if (!size) {
#if NV2A_PERF_LOG
            g_nv2a_stats.vram_working.upload_nops++;
            g_nv2a_stats.vram_working.upload_nop_bytes += requested_size;
            g_nv2a_stats.vram_working.upload_finishes_avoided++;
#endif
            bitmap_set(r->cb_vertex_pages, start_bit, nbits);
            return;
        }
        start_bit = offset / TARGET_PAGE_SIZE;
        end_bit = TARGET_PAGE_ALIGN(offset + size) / TARGET_PAGE_SIZE;
        nbits = end_bit - start_bit;
        overlaps = find_next_bit(r->cb_vertex_pages, end_bit,
                                  start_bit) < end_bit;
        if (overlaps) {
            /* Finish can change source data; preserve the original copy. */
            offset = requested_offset;
            size = requested_size;
            source = data;
            start_bit = offset / TARGET_PAGE_SIZE;
            end_bit = TARGET_PAGE_ALIGN(offset + size) / TARGET_PAGE_SIZE;
            nbits = end_bit - start_bit;
        } else {
#if NV2A_PERF_LOG
            g_nv2a_stats.vram_working.upload_trim_bytes +=
                requested_size - size;
            g_nv2a_stats.vram_working.upload_finishes_avoided++;
#endif
        }
    }

    /*
     * Draws read their vertices straight out of the RAM buffer, so a draw that
     * has been recorded but not yet submitted still depends on what these pages
     * hold. A title that reuses one vertex arena for several draws in a frame
     * -- Morrowind's title screen builds both its background quad and its
     * "Press START" quad at the same guest address -- would otherwise have the
     * earlier draw pick up the later draw's vertices at submission time.
     *
     * The per-frame buffer only isolates frames from each other; it does not
     * help within a command buffer. Submit what is already recorded before
     * overwriting a page any of it reads.
     */
    if (overlaps) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_VERTEX_BUFFER_DIRTY);
    }

    nv2a_profile_inc_counter(NV2A_PROF_GEOM_BUFFER_UPDATE_1);
    vram = get_staging_buffer(r, BUFFER_VERTEX_RAM);
    memcpy(vram->mapped + offset, source, size);

    pgraph_vk_vertex_ram_written(r, offset, size);
    /*
     * Reserve uploads conservatively too. Final vertex bindings also mark
     * clean reads, which do not pass through this copy function.
     */
    bitmap_set(r->cb_vertex_pages, start_bit, nbits);
}

static void update_memory_buffer(NV2AState *d, hwaddr addr, hwaddr size)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(r->num_vertex_ram_buffer_syncs <
           ARRAY_SIZE(r->vertex_ram_buffer_syncs));
    r->vertex_ram_buffer_syncs[r->num_vertex_ram_buffer_syncs++] =
        (MemorySyncRequirement){ .addr = addr, .size = size };
}

static const VkFormat float_to_count[] = {
    VK_FORMAT_R32_SFLOAT,
    VK_FORMAT_R32G32_SFLOAT,
    VK_FORMAT_R32G32B32_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
};

static const VkFormat ub_to_count[] = {
    VK_FORMAT_R8_UNORM,
    VK_FORMAT_R8G8_UNORM,
    VK_FORMAT_R8G8B8_UNORM,
    VK_FORMAT_R8G8B8A8_UNORM,
};

static const VkFormat s1_to_count[] = {
    VK_FORMAT_R16_SNORM,
    VK_FORMAT_R16G16_SNORM,
    VK_FORMAT_R16G16B16_SNORM,
    VK_FORMAT_R16G16B16A16_SNORM,
};

static const VkFormat s32k_to_count[] = {
    VK_FORMAT_R16_SSCALED,
    VK_FORMAT_R16G16_SSCALED,
    VK_FORMAT_R16G16B16_SSCALED,
    VK_FORMAT_R16G16B16A16_SSCALED,
};

/* The same bits read as integers, for devices without SSCALED vertex fetch.
 * The shader converts them (see VshState.int16_attrs), which is exactly what
 * SSCALED does: the signed value as a float, not normalized. */
static const VkFormat s32k_int_to_count[] = {
    VK_FORMAT_R16_SINT,
    VK_FORMAT_R16G16_SINT,
    VK_FORMAT_R16G16B16_SINT,
    VK_FORMAT_R16G16B16A16_SINT,
};

/*
 * SSCALED vertex formats are optional in Vulkan. Qualcomm's stock Adreno 610
 * driver (512.615.86) reports no VERTEX_BUFFER support for any of them, while
 * the 512.805 blob does. Without the fallback every S32K attribute on that
 * driver was fetched as garbage: Forza Motorsport passes its world texture
 * coordinates as S32K, so the road, sky and grandstands each sampled a single
 * texel and drew as flat colour, while cars (float texcoords) were fine.
 */
void pgraph_vk_init_vertex_formats(PGRAPHVkState *r)
{
    r->sscaled_vertex_supported = true;
    for (int i = 0; i < ARRAY_SIZE(s32k_to_count); i++) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(r->physical_device,
                                            s32k_to_count[i], &props);
        if (!(props.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)) {
            r->sscaled_vertex_supported = false;
        }
    }
#ifndef __ANDROID__
    /* Lets the desktop build exercise the fallback. */
    const char *env = getenv("XEMU_VK_SSCALED_VERTEX");
    if (env && env[0] == '0') {
        r->sscaled_vertex_supported = false;
    }
#endif
    if (!r->sscaled_vertex_supported) {
        fprintf(stderr, "SSCALED vertex formats unsupported: converting S32K "
                        "attributes in the vertex shader\n");
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_INFO, "hakuX-vk",
                            "SSCALED vertex formats unsupported: converting "
                            "S32K attributes in the vertex shader");
#endif
    }
}

static char const * const vertex_data_array_format_to_str[] = {
    [NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D] = "UB_D3D",
    [NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL] = "UB_OGL",
    [NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1] = "S1",
    [NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F] = "F",
    [NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K] = "S32K",
    [NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP] = "CMP",
};

static void update_inline_attribute_value(PGRAPHState *pg, unsigned int index,
                                           const uint8_t *data)
{
    VertexAttribute *attr = &pg->vertex_attributes[index];
    float previous[4];

    memcpy(previous, attr->inline_value, sizeof(previous));
    pgraph_update_inline_value(attr, data);
    if ((pg->uniform_attrs & (1 << index)) &&
        memcmp(previous, attr->inline_value, sizeof(previous))) {
        pg->vk_renderer_state->uniforms_changed = true;
    }
}

static void update_immediate_attribute_values(PGRAPHState *pg,
                                               bool inline_buffer)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    uint16_t mask = 0;

    /* Immediate methods change values without changing the array layout. */
    for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        const VertexAttribute *attr = &pg->vertex_attributes[i];
        if (inline_buffer ? attr->inline_buffer_populated : attr->count != 0) {
            continue;
        }
        mask |= 1 << i;
        if (!(r->immediate_attrs_valid & (1 << i)) ||
            memcmp(r->immediate_attr_values[i], attr->inline_value,
                   sizeof(attr->inline_value))) {
            r->uniforms_changed = true;
            memcpy(r->immediate_attr_values[i], attr->inline_value,
                   sizeof(attr->inline_value));
        }
    }
    r->immediate_attrs_valid = mask;
}

void pgraph_vk_bind_vertex_attributes(NV2AState *d, unsigned int min_element,
                                      unsigned int max_element,
                                      bool inline_data,
                                      unsigned int inline_stride,
                                      unsigned int provoking_element)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    update_immediate_attribute_values(pg, false);
    unsigned int num_elements = max_element - min_element + 1;

    if (inline_data) {
        r->cached_num_active_bindings = 0;
    } else if (pg->vertex_attr_gen == r->last_vertex_attr_gen &&
               r->cached_num_active_bindings > 0) {
        OPT_STAT_INC(vtx_cache_hits);
        pg->compressed_attrs = r->cached_compressed_attrs;
        pg->uniform_attrs = r->cached_uniform_attrs;
        pg->swizzle_attrs = r->cached_swizzle_attrs;
        pg->int16_attrs = r->cached_int16_attrs;
        r->num_active_vertex_attribute_descriptions = r->cached_num_active_attrs;
        r->num_active_vertex_binding_descriptions = r->cached_num_active_bindings;
        memcpy(r->vertex_attribute_descriptions, r->cached_attr_descs,
               sizeof(r->cached_attr_descs));
        memcpy(r->vertex_binding_descriptions, r->cached_bind_descs,
               sizeof(r->cached_bind_descs));
        memcpy(r->vertex_attribute_to_description_location,
               r->cached_attr_to_desc_loc,
               sizeof(r->cached_attr_to_desc_loc));
        memcpy(r->vertex_attribute_offsets, r->cached_attr_offsets,
               sizeof(r->cached_attr_offsets));
        for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
            if (!pg->vertex_attributes[i].count) {
                continue;
            }
            hwaddr base = r->cached_attr_layout[i].base_addr;
            size_t stride = r->cached_attr_layout[i].stride;
            if (stride) {
                size_t element_size = pg->vertex_attributes[i].size *
                                      pg->vertex_attributes[i].count;
                hwaddr size = (num_elements - 1) * stride + element_size;
                update_memory_buffer(d, base + min_element * stride, size);
            }
            /*
             * Layout generations do not track guest RAM writes or the
             * provoking index. Preserve the cold path's current-value latch,
             * including RAM-backed constants whose stride is zero.
             */
            update_inline_attribute_value(pg, i,
                d->vram_ptr + base + provoking_element * stride);
        }
        return;
    }
    OPT_STAT_INC(vtx_cache_misses);

    if (inline_data) {
        NV2A_VK_DGROUP_BEGIN("%s (num_elements: %d inline stride: %d)",
                             __func__, num_elements, inline_stride);
    } else {
        NV2A_VK_DGROUP_BEGIN("%s (num_elements: %d)", __func__, num_elements);
    }

    pg->compressed_attrs = 0;
    pg->uniform_attrs = 0;
    pg->swizzle_attrs = 0;
    pg->int16_attrs = 0;

    r->num_active_vertex_attribute_descriptions = 0;
    r->num_active_vertex_binding_descriptions = 0;

    for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        r->cached_attr_layout[i].stride = 0;
        r->cached_attr_layout[i].base_addr = 0;
        VertexAttribute *attr = &pg->vertex_attributes[i];
        NV2A_VK_DGROUP_BEGIN("[attr %02d] format=%s, count=%d, stride=%d", i,
                             vertex_data_array_format_to_str[attr->format],
                             attr->count, attr->stride);
        r->vertex_attribute_to_description_location[i] = -1;
        if (!attr->count) {
            pg->uniform_attrs |= 1 << i;
            NV2A_VK_DPRINTF("inline_value = {%f, %f, %f, %f}",
                            attr->inline_value[0], attr->inline_value[1],
                            attr->inline_value[2], attr->inline_value[3]);
            NV2A_VK_DGROUP_END();
            continue;
        }

        VkFormat vk_format;
        bool needs_conversion = false;
        bool d3d_swizzle = false;
        bool int16_attr = false;

        switch (attr->format) {
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D:
            assert(attr->count == 4);
            d3d_swizzle = true;
            /* fallthru */
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL:
            assert(attr->count <= ARRAY_SIZE(ub_to_count));
            vk_format = ub_to_count[attr->count - 1];
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1:
            assert(attr->count <= ARRAY_SIZE(s1_to_count));
            vk_format = s1_to_count[attr->count - 1];
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F:
            assert(attr->count <= ARRAY_SIZE(float_to_count));
            vk_format = float_to_count[attr->count - 1];
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K:
            assert(attr->count <= ARRAY_SIZE(s32k_to_count));
            if (r->sscaled_vertex_supported) {
                vk_format = s32k_to_count[attr->count - 1];
            } else {
                vk_format = s32k_int_to_count[attr->count - 1];
                int16_attr = true;
            }
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP:
            vk_format =
                VK_FORMAT_R32_SINT; // VK_FORMAT_B10G11R11_UFLOAT_PACK32 ??
            /* 3 signed, normalized components packed in 32-bits. (11,11,10) */
            assert(attr->count == 1);
            needs_conversion = true;
            break;
        default:
            fprintf(stderr, "Unknown vertex type: 0x%x\n", attr->format);
            assert(false);
            break;
        }

        nv2a_profile_inc_counter(NV2A_PROF_ATTR_BIND);
        hwaddr attrib_data_addr;
        size_t stride;
        size_t element_size = attr->size * attr->count;
        assert(element_size <= sizeof(attr->inline_value));

        hwaddr start = 0;
        if (inline_data) {
            attrib_data_addr = attr->inline_array_offset;
            stride = inline_stride;
        } else {
            hwaddr dma_len;
            uint8_t *attr_data = (uint8_t *)nv_dma_map(
                d, attr->dma_select ? pg->dma_vertex_b : pg->dma_vertex_a,
                &dma_len);
            assert(attr->offset < dma_len);
            attrib_data_addr = attr_data + attr->offset - d->vram_ptr;
            stride = attr->stride;
            start = attrib_data_addr + min_element * stride;
            if (stride) {
                /* Include the final element, even for overlapping strides. */
                hwaddr size = (num_elements - 1) * stride + element_size;
                update_memory_buffer(d, start, size);
            }
            r->cached_attr_layout[i].base_addr = attrib_data_addr;
            r->cached_attr_layout[i].stride = stride;
        }

        uint32_t provoking_element_index = provoking_element - min_element;
        const uint8_t *last_entry;

        if (inline_data) {
            last_entry =
                (uint8_t *)pg->inline_array + attr->inline_array_offset;
        } else {
            last_entry = d->vram_ptr + start;
        }
        if (!stride) {
            // Stride of 0 indicates that only the first element should be
            // used.
            pg->uniform_attrs |= 1 << i;
            update_inline_attribute_value(pg, i, last_entry);
            NV2A_VK_DPRINTF("inline_value = {%f, %f, %f, %f}",
                            attr->inline_value[0], attr->inline_value[1],
                            attr->inline_value[2], attr->inline_value[3]);
            NV2A_VK_DGROUP_END();
            continue;
        }

        NV2A_VK_DPRINTF("offset = %08" HWADDR_PRIx, attrib_data_addr);
        last_entry += stride * provoking_element_index;
        update_inline_attribute_value(pg, i, last_entry);

        r->vertex_attribute_to_description_location[i] =
            r->num_active_vertex_binding_descriptions;

        r->vertex_binding_descriptions
            [r->num_active_vertex_binding_descriptions++] =
            (VkVertexInputBindingDescription){
                .binding = r->vertex_attribute_to_description_location[i],
                .stride = stride,
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            };

        r->vertex_attribute_descriptions
            [r->num_active_vertex_attribute_descriptions++] =
            (VkVertexInputAttributeDescription){
                .binding = r->vertex_attribute_to_description_location[i],
                .location = i,
                .format = vk_format,
            };

        r->vertex_attribute_offsets[i] = attrib_data_addr;

        if (needs_conversion) {
            pg->compressed_attrs |= (1 << i);
        }
        if (d3d_swizzle) {
            pg->swizzle_attrs |= (1 << i);
        }
        if (int16_attr) {
            pg->int16_attrs |= (1 << i);
        }

        NV2A_VK_DGROUP_END();
    }

    if (!inline_data) {
        r->last_vertex_attr_gen = pg->vertex_attr_gen;
        r->cached_num_active_bindings = r->num_active_vertex_binding_descriptions;
        r->cached_num_active_attrs = r->num_active_vertex_attribute_descriptions;
        memcpy(r->cached_attr_descs, r->vertex_attribute_descriptions,
               sizeof(r->cached_attr_descs));
        memcpy(r->cached_bind_descs, r->vertex_binding_descriptions,
               sizeof(r->cached_bind_descs));
        memcpy(r->cached_attr_to_desc_loc,
               r->vertex_attribute_to_description_location,
               sizeof(r->cached_attr_to_desc_loc));
        memcpy(r->cached_attr_offsets, r->vertex_attribute_offsets,
               sizeof(r->cached_attr_offsets));
        r->cached_compressed_attrs = pg->compressed_attrs;
        r->cached_uniform_attrs = pg->uniform_attrs;
        r->cached_swizzle_attrs = pg->swizzle_attrs;
        r->cached_int16_attrs = pg->int16_attrs;
    }

    NV2A_VK_DGROUP_END();
}

void pgraph_vk_bind_vertex_attributes_inline(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    update_immediate_attribute_values(pg, true);
    r->cached_num_active_bindings = 0;

    pg->compressed_attrs = 0;
    pg->uniform_attrs = 0;
    pg->swizzle_attrs = 0;
    pg->int16_attrs = 0;

    r->num_active_vertex_attribute_descriptions = 0;
    r->num_active_vertex_binding_descriptions = 0;

    for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attr = &pg->vertex_attributes[i];
        if (attr->inline_buffer_populated) {
            r->vertex_attribute_to_description_location[i] =
                r->num_active_vertex_binding_descriptions;
            r->vertex_binding_descriptions
                [r->num_active_vertex_binding_descriptions++] =
                (VkVertexInputBindingDescription){
                    .binding =
                        r->vertex_attribute_to_description_location[i],
                    .stride = 4 * sizeof(float),
                    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                };
            r->vertex_attribute_descriptions
                [r->num_active_vertex_attribute_descriptions++] =
                (VkVertexInputAttributeDescription){
                    .binding =
                        r->vertex_attribute_to_description_location[i],
                    .location = i,
                    .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                };
            memcpy(attr->inline_value,
                   attr->inline_buffer + (pg->inline_buffer_length - 1) * 4,
                   sizeof(attr->inline_value));
        } else {
            r->vertex_attribute_to_description_location[i] = -1;
            pg->uniform_attrs |= 1 << i;
        }
    }
}
