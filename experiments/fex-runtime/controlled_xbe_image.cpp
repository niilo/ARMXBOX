// SPDX-License-Identifier: GPL-2.0-or-later
#include "controlled_xbe.h"
#include <algorithm>
#include <cstring>
namespace armxbox {
namespace {
constexpr uint32_t EntryKey = 0xa8fc57ab, ThunkKey = 0x5b6d40b6;
constexpr uint32_t Ordinal = 284; // RtlFillMemory, NTAPI(void *, DWORD, BYTE).
uint32_t read(std::span<const uint8_t> data, size_t at)
{
    return uint32_t(data[at]) | uint32_t(data[at + 1]) << 8 |
           uint32_t(data[at + 2]) << 16 | uint32_t(data[at + 3]) << 24;
}
void put(GeneratedXbe &data, size_t at, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        data[at + i] = value >> (8 * i);
}
bool zeros(std::span<const uint8_t> data, size_t begin, size_t end)
{
    return std::all_of(data.begin() + begin, data.begin() + end,
                       [](uint8_t b) { return b == 0; });
}
// Handwritten IA32: push fill, length, destination; call [kernel thunk];
// load filled word, publish it to data, controlled HLT completion.
constexpr auto Text = [] {
    std::array<uint8_t, 26> text{ 0x6a, 0x5a, 0x6a, 0x20, 0x68, 0, 0,    0,   0,
                                  0xff, 0x15, 0,    0,    0,    0, 0xa1, 0,   0,
                                  0,    0,    0xa3, 0,    0,    0, 0,    0xf4 };
    auto imm = [&](unsigned offset, uint32_t address) {
        for (unsigned i = 0; i < 4; ++i)
            text[offset + i] = address >> (8 * i);
    };
    imm(5, XbeFill);
    imm(11, XbeThunk);
    imm(16, XbeFill);
    imm(21, XbeThunk + 0x20);
    return text;
}();
} // namespace
GeneratedXbe generateControlledXbe()
{
    GeneratedXbe bytes{};
    put(bytes, 0, 0x48454258); // XBEH, little endian.
    put(bytes, 0x104, XbeBase);
    put(bytes, 0x108, 0x1000);
    put(bytes, 0x10c, XbeImageSize);
    put(bytes, 0x110, 0x178);
    put(bytes, 0x118, XbeBase + 0x300);
    put(bytes, 0x11c, 2);
    put(bytes, 0x120, XbeBase + 0x180);
    put(bytes, 0x128, XbeEntry ^ EntryKey);
    put(bytes, 0x130, 0x2000);
    put(bytes, 0x158, XbeThunk ^ ThunkKey);
    put(bytes, 0x300, 0x1d0);
    for (unsigned i = 0; i < 2; ++i) {
        size_t at = 0x180 + 56 * i;
        put(bytes, at, i ? 3 : 6); // Writable/preload or executable/preload.
        put(bytes, at + 4, XbeBase + 0x1000 * (i + 1));
        put(bytes, at + 8, i ? 0x2000 : 0x1000);
        put(bytes, at + 12, 0x1000 * (i + 1));
        put(bytes, at + 16, 0x1000);
        put(bytes, at + 20, XbeBase + 0x600 + 16 * i);
    }
    std::memcpy(bytes.data() + 0x600, ".text", 6);
    std::memcpy(bytes.data() + 0x610, ".data", 6);
    std::memcpy(bytes.data() + 0x700, "ARMXBOX generated XBE v1", 25);
    std::copy(std::begin(Text), std::end(Text), bytes.begin() + 0x1000);
    bytes[0x1100] = 0xf4;
    put(bytes, 0x2000, 0x80000000U | Ordinal);
    return bytes;
}
bool admitControlledXbe(std::span<const uint8_t> bytes, XbePlan &out)
{
    out = {};
    // Fixed bounds are the admission profile, not unchecked file assertions.
    // No pointer casts or absolute guest pointer dereferences occur here.
    if (bytes.size() != 0x3000 || read(bytes, 0) != 0x48454258 ||
        read(bytes, 0x104) != XbeBase || read(bytes, 0x108) != 0x1000 ||
        read(bytes, 0x10c) != XbeImageSize || read(bytes, 0x110) != 0x178 ||
        !zeros(bytes, 4, 0x104) || read(bytes, 0x114) ||
        read(bytes, 0x118) != XbeBase + 0x300 || read(bytes, 0x11c) != 2 ||
        read(bytes, 0x120) != XbeBase + 0x180 || read(bytes, 0x124) ||
        (read(bytes, 0x128) ^ EntryKey) != XbeEntry || read(bytes, 0x12c) ||
        read(bytes, 0x130) != 0x2000 || !zeros(bytes, 0x134, 0x158) ||
        (read(bytes, 0x158) ^ ThunkKey) != XbeThunk ||
        !zeros(bytes, 0x15c, 0x178) || read(bytes, 0x300) != 0x1d0 ||
        !zeros(bytes, 0x304, 0x4d0) ||
        std::memcmp(bytes.data() + 0x700, "ARMXBOX generated XBE v1", 25))
        return false;
    XbePlan plan;
    plan.entry = XbeEntry;
    plan.thunk = XbeThunk;
    for (unsigned i = 0; i < 2; ++i) {
        const size_t at = 0x180 + 56 * i;
        const uint32_t offset = 0x1000 * (i + 1), virt = i ? 0x2000 : 0x1000;
        if (read(bytes, at) != (i ? 3U : 6U) ||
            read(bytes, at + 4) != XbeBase + offset ||
            read(bytes, at + 8) != virt || read(bytes, at + 12) != offset ||
            read(bytes, at + 16) != 0x1000 ||
            read(bytes, at + 20) != XbeBase + 0x600 + 16 * i ||
            !zeros(bytes, at + 24, at + 56) ||
            std::memcmp(bytes.data() + 0x600 + 16 * i, i ? ".data" : ".text",
                        6))
            return false;
        // Subtraction bounds avoid wrapping either file or guest extents.
        if (offset > XbeImageSize || virt > XbeImageSize - offset ||
            offset > bytes.size() || 0x1000 > bytes.size() - offset)
            return false;
        plan.rawOffset[i] = offset;
        plan.rawSize[i] = 0x1000;
        plan.virtualOffset[i] = offset;
        plan.virtualSize[i] = virt;
    }
    // Only this bounded handwritten program and one terminated kernel thunk
    // are admitted. No arbitrary x86 bytes, TLS, libraries or extra services.
    if (std::memcmp(bytes.data() + 0x1000, Text.data(), Text.size()) ||
        !zeros(bytes, 0x1000 + Text.size(), 0x1100) || bytes[0x1100] != 0xf4 ||
        !zeros(bytes, 0x1101, 0x2000) ||
        read(bytes, 0x2000) != (0x80000000U | Ordinal) ||
        !zeros(bytes, 0x2004, 0x3000))
        return false;
    out = plan;
    return true;
}
} // namespace armxbox

namespace armxbox {
std::array<bool, 4> controlledXbeAdmissionControls()
{
    const auto good = generateControlledXbe();
    XbePlan plan;
    std::array<bool, 4> result{ admitControlledXbe(good, plan), true, true,
                                true };
    auto rejects = [&](const GeneratedXbe &bytes, size_t size = 0x3000) {
        plan.entry = 0xffffffff;
        return !admitControlledXbe(std::span(bytes.data(), size), plan) &&
               plan.entry == 0 && plan.thunk == 0;
    };
    // Every short file is rejected before any fixed-offset read.
    for (size_t length = 0; length < good.size(); ++length)
        result[1] &= rejects(good, length);
    for (size_t offset :
         { 0U, 4U, 0x104U, 0x108U, 0x10cU, 0x110U, 0x114U, 0x118U, 0x11cU,
           0x120U, 0x124U, 0x128U, 0x130U, 0x300U, 0x304U, 0x700U }) {
        auto bytes = good;
        put(bytes, offset, 0xffffffff);
        result[1] &= rejects(bytes);
    }
    // Raw/virtual overflow, image overlap, W+X/unknown flags, header/name
    // pointers and unsupported shared-page/digest records are all refused.
    for (unsigned section = 0; section < 2; ++section) {
        for (unsigned field = 0; field < 14; ++field) {
            auto bytes = good;
            put(bytes, 0x180 + 56 * section + 4 * field, 0xffffffff);
            result[2] &= rejects(bytes);
        }
    }
    for (size_t offset :
         { 0x12cU, 0x134U, 0x138U,  0x13cU,  0x140U,  0x144U,  0x148U, 0x14cU,
           0x150U, 0x154U, 0x158U,  0x15cU,  0x160U,  0x164U,  0x168U, 0x16cU,
           0x170U, 0x174U, 0x2000U, 0x2004U, 0x1000U, 0x1100U, 0x610U }) {
        auto bytes = good;
        put(bytes, offset, 0xffffffff);
        result[3] &= rejects(bytes);
    }
    auto overlap = good;
    put(overlap, 0x1b8 + 4, XbeBase + 0x1000);
    result[2] &= rejects(overlap);
    put(overlap, 0x1b8 + 12, 0x1000);
    result[2] &= rejects(overlap);
    auto writableCode = good;
    put(writableCode, 0x180, 7);
    result[2] &= rejects(writableCode);
    auto dataEntry = good;
    put(dataEntry, 0x128, XbeThunk ^ 0xa8fc57ab);
    result[1] &= rejects(dataEntry);
    auto unknown = good;
    put(unknown, 0x2000, 0x80000001);
    result[3] &= rejects(unknown);
    auto debug = good;
    put(debug, 0x128, XbeEntry ^ 0x94859d4b);
    result[3] &= rejects(debug);
    return result;
}
} // namespace armxbox
