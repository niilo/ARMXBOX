// SPDX-License-Identifier: GPL-2.0-or-later
// Offscreen owned-packet consumer. Software Vulkan is not Pocket S proof.
#include "output_vulkan.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
constexpr unsigned Width = armxbox::OutputWidth, Height = armxbox::OutputHeight;
using Pixels = armxbox::OutputPixels;
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
std::vector<uint32_t> readShader(const std::string &path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(bool(input), "open SPIR-V");
    const auto bytes = input.tellg();
    require(bytes > 0 && bytes < 65536 && bytes % 4 == 0, "SPIR-V length");
    std::vector<uint32_t> words(size_t(bytes) / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char *>(words.data()), bytes);
    require(bool(input), "read SPIR-V");
    return words;
}
} // namespace
int main(int argc, char **argv)
{
    try {
        require((argc == 2 ||
                 (argc == 3 && std::string(argv[2]) == "--no-validation")),
                "usage: armxbox_output_vulkan_test shader-directory "
                "[--no-validation]");
        {
            const auto vertex =
                readShader(std::string(argv[1]) + "/output.vert.spv");
            const auto fragment =
                readShader(std::string(argv[1]) + "/output.frag.spv");
            armxbox::OutputVulkan gpu(vertex, fragment, argc == 2);
            std::printf("Vulkan %s\n", gpu.detail());
            armxbox::OutputQueue queue;
            Pixels first{};
            for (unsigned iteration = 1; iteration <= 4; ++iteration) {
                auto source = armxbox::outputFixture(0x13000, iteration);
                // Asymmetry catches transpose/orientation/channel mistakes
                // hidden by a symmetric checker. Inset geometry catches ignored
                // vertices.
                if (iteration % 2 == 0) {
                    for (unsigned i = 0; i < 16; ++i) {
                        source[0xa0 + 4 * i] = 11 + 9 * i;
                        source[0xa1 + 4 * i] = 197 - 7 * i;
                        source[0xa2 + 4 * i] = 31 + 5 * i;
                        source[0xa3 + 4 * i] = 83 + 11 * i;
                    }
                    const std::array<float, 6> inset{ -.75f, -.5f, -.75f,
                                                      .75f,  .5f,  -.5f };
                    for (unsigned i = 0; i < 6; ++i) {
                        auto bits = std::bit_cast<uint32_t>(inset[i]);
                        for (unsigned b = 0; b < 4; ++b)
                            source[0x80 + 4 * i + b] = bits >> (8 * b);
                    }
                }
                require(queue.submit({ 0x13000, source, 1, true, false },
                                     0x13000) == armxbox::OutputError::None,
                        "capture owned test packet");
                auto lease = queue.acquire();
                auto *packet = queue.get(lease);
                require(packet, "packet lease");
                source.fill(0x39); // Host consumer must use its owned snapshot.
                const auto pixels = gpu.render(*packet);
                require(!armxbox::outputPixelMismatches(pixels, *packet),
                        "pixel oracle mismatch");
                std::printf(
                    "PASS Vulkan owned packet %u: scalar triangle/nearest RGBA "
                    "oracle; source overwritten\n",
                    iteration);
                if (iteration == 1 || iteration == 2) {
                    const auto stem = std::string(argv[1]) + "/offscreen-" +
                                      std::to_string(iteration);
                    std::ofstream raw(stem + ".rgba", std::ios::binary);
                    raw.write(reinterpret_cast<const char *>(pixels.data()),
                              pixels.size());
                    require(bool(raw), "write RGBA evidence");
                    std::ofstream ppm(stem + ".ppm", std::ios::binary);
                    ppm << "P6\n" << Width << " " << Height << "\n255\n";
                    for (size_t i = 0; i < pixels.size(); i += 4)
                        ppm.write(
                            reinterpret_cast<const char *>(pixels.data() + i),
                            3);
                    require(bool(ppm), "write PPM evidence");
                }
                if (iteration == 1)
                    first = pixels;
                if (iteration == 3)
                    require(first == pixels, "warm canonical frame differs");
                if (iteration == 2) {
                    auto corrupt = pixels;
                    for (size_t i = 0; i < corrupt.size(); i += 4)
                        std::swap(corrupt[i], corrupt[i + 2]);
                    require(armxbox::outputPixelMismatches(corrupt, *packet),
                            "channel negative control");
                    for (unsigned y = 0; y < Height; ++y)
                        std::memcpy(corrupt.data() + 4 * y * Width,
                                    pixels.data() +
                                        4 * (Height - 1 - y) * Width,
                                    4 * Width);
                    require(armxbox::outputPixelMismatches(corrupt, *packet),
                            "orientation negative control");
                    corrupt.fill(0x5d);
                    require(armxbox::outputPixelMismatches(corrupt, *packet),
                            "missing copy negative control");
                    corrupt.fill(0);
                    for (size_t i = 3; i < corrupt.size(); i += 4)
                        corrupt[i] = 255;
                    require(armxbox::outputPixelMismatches(corrupt, *packet),
                            "missing draw negative control");
                    std::puts("PASS pixel oracle rejects channel swap, "
                              "vertical flip, missing copy and missing draw");
                }
                require(queue.retire(lease) && !queue.get(lease),
                        "retire completed GPU packet");
            }
            gpu.finish();
        }
        if (argc == 2)
            std::puts("PASS Vulkan validation and synchronization validation: "
                      "zero errors");
        else
            std::puts("PASS Vulkan without validation layers/extensions");
        std::puts("Result: cloud offscreen consumer PASS; not Android/Turnip, "
                  "PCM playback or game FPS proof");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL offscreen consumer: %s\n", error.what());
        return 1;
    }
}
