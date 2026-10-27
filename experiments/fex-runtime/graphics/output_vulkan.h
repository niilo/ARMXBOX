// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "output_packet.h"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
namespace armxbox {
constexpr unsigned OutputWidth = 64, OutputHeight = 64;
using OutputPixels = std::array<uint8_t, OutputWidth * OutputHeight * 4>;
// Serialized diagnostic consumer; the caller owns the packet until render()
// returns. Throws on unavailable capabilities, Vulkan failures or validation
// errors. Shader spans are consumed during construction, not retained.
class OutputVulkan {
    struct Impl;
    std::unique_ptr<Impl> impl;

public:
    OutputVulkan(std::span<const uint32_t> vertex,
                 std::span<const uint32_t> fragment, bool validation);
    ~OutputVulkan();
    OutputVulkan(const OutputVulkan &) = delete;
    OutputVulkan &operator=(const OutputVulkan &) = delete;
    OutputPixels render(const OutputPacket &);
    const char *detail() const noexcept;
    // Explicit teardown permits observing validation errors through cleanup.
    // Destruction also cleans resources, but never throws.
    void finish();
};
unsigned outputPixelMismatches(const OutputPixels &, const OutputPacket &);
} // namespace armxbox
