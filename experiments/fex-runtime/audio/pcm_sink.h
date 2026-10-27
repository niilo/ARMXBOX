// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <span>
namespace armxbox {
// Controller/UI calls only. The accepted packet's PCM is copied, never
// borrowed.
bool publishPcm(std::span<const uint8_t>) noexcept;
bool pcmReady() noexcept;
// Reserve before enqueuing the worker. Cancellation never resets on worker
// entry.
uint64_t beginPcmTest() noexcept;
void cancelPcmTest() noexcept;
// For thread-start failure only: release an unstarted matching reservation.
void abandonPcmTest(uint64_t token) noexcept;
struct PcmTestResults {
    char report[8192]{};
    bool passed = false;
    bool cancelled = false;
};
// Worker/controller thread only; no exception or stream work crosses into UI.
PcmTestResults runPcmTest(uint64_t token, void (*log)(const char *)) noexcept;
} // namespace armxbox
