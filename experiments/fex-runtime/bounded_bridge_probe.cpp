#include "bounded_bridge_probe.h"
#include "execution_boundary.h"
#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/Utils/AllocatorHooks.h>
#include <atomic>
#include <bit>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>

namespace armxbox {
namespace {
using Context = FEXCore::Context::Context;
using Thread = FEXCore::Core::InternalThreadState;
using State = FEXCore::Core::CPUState;
using Reason = FEXCore::Context::ExecutionResult;
constexpr const char *Labels[] = {
    "bounded bridge typed service returns exact result and stdcall stack",
    "bounded bridge warm repeat retains one admission allowance",
    "bounded callback restores caller SIMD MMX x87 and control state",
    "bounded callback restores caller flags GPR and FS state",
    "bounded bridge rejects incomplete readable argument frame",
    "bounded bridge rejects overflowing IA32 argument frame",
    "bounded bridge rejects unowned caller return address",
    "bounded bridge rejects unexpected service stub PC",
    "bounded bridge rejects stop budget and wrong exception metadata",
    "bounded bridge enforces reverse callback depth cap",
    "bounded bridge enforces service count cap",
    "bounded callback exhausts shared allowance without caller completion",
    "nonterminating reverse callback returns on shared admission allowance",
    "callback stop remains latched across host dispatch and repeated entry",
    "callback fault retains exact attribution without caller completion",
    "bounded bridge rejects malformed callback completion frame",
};
static_assert(std::size(Labels) == BridgeCheckCount);

// Architectural snapshot only: never restore live engine/cache/control fields.
struct Snapshot {
    State state;
    uint32_t flags;
    Snapshot(Context *context, Thread *thread)
        : flags(context->ReconstructCompactedEFLAGS(thread, false, nullptr, 0))
    {
        copyArchitecture(state, thread->CurrentFrame->State);
    }
    static void copyArchitecture(State &to, const State &state)
    {
        to.rip = state.rip;
        std::memcpy(to.gregs, state.gregs, sizeof to.gregs);
        to.xmm = state.xmm;
        std::memcpy(to.avx_high, state.avx_high, sizeof to.avx_high);
        std::memcpy(to.mm, state.mm, sizeof to.mm);
        to.es_idx = state.es_idx;
        to.cs_idx = state.cs_idx;
        to.ss_idx = state.ss_idx;
        to.ds_idx = state.ds_idx;
        to.gs_idx = state.gs_idx;
        to.fs_idx = state.fs_idx;
        to.es_cached = state.es_cached;
        to.cs_cached = state.cs_cached;
        to.ss_cached = state.ss_cached;
        to.ds_cached = state.ds_cached;
        to.gs_cached = state.gs_cached;
        to.fs_cached = state.fs_cached;
        std::memcpy(to.segment_arrays, state.segment_arrays,
                    sizeof to.segment_arrays);
        to.mxcsr = state.mxcsr;
        to.FCW = state.FCW;
        to.AbridgedFTW = state.AbridgedFTW;
        // x87 status/TOP occupies flags[32..47], separate from canonical
        // EFLAGS.
        std::memcpy(to.flags + 32, state.flags + 32, 16);
    }
    void restore(Context *context, Thread *thread) const
    {
        copyArchitecture(thread->CurrentFrame->State, state);
        context->SetFlagsFromCompactedEFLAGS(thread, flags);
    }
};

// The call/return stack is a cache of guest/host PC pairs, not guest stack RAM.
// Discard it rather than resurrect host PCs after compiling a callback.
void resetShadow(Thread *thread)
{
    // Explicit zeroing avoids treating an unchecked madvise hint as proof that
    // stale host return PCs were removed. Diagnostic-only; not a hot-path
    // policy.
    std::memset(thread->CallRetStackBase, 0, Thread::CALLRET_STACK_SIZE);
    thread->CurrentFrame->State.callret_sp =
        reinterpret_cast<uintptr_t>(thread->CallRetStackBase) +
        Thread::CALLRET_STACK_SIZE / 4;
}

struct Bridge {
    Context *context;
    Thread *thread;
    uintptr_t code, stack;
    unsigned services = 0;
    unsigned depth = 0;
    unsigned hostBodies = 0;
    uint32_t args[3]{};
    // Called only while the worker is stopped and owned by this host dispatch.
    bool admit(Reason reason)
    {
        const auto &state = thread->CurrentFrame->State;
        const auto &fault = thread->CurrentFrame->SynchronousFaultData;
        if (reason != Reason::GuestExit || state.rip != code + 0x100 ||
            !fault.FaultToTopAndGeneratedException || fault.Signal != SIGSEGV ||
            fault.TrapNo != 13 || fault.err_code != 0 || depth >= 1 ||
            services >= 1)
            return false;
        const uint64_t esp = state.gregs[4];
        if (esp > UINT32_MAX || esp < stack ||
            esp > stack + 0x1000 - sizeof args)
            return false;
        std::memcpy(args, reinterpret_cast<const void *>(esp), sizeof args);
        if (args[0] != code + 9)
            return false;
        ++services;
        ++hostBodies;
        return true;
    }
    bool completed(Reason reason) const
    {
        const auto &state = thread->CurrentFrame->State;
        const auto &fault = thread->CurrentFrame->SynchronousFaultData;
        return reason == Reason::GuestExit && state.rip == code + 0x180 &&
               state.gregs[4] == stack + 0x804 &&
               fault.FaultToTopAndGeneratedException &&
               fault.Signal == SIGSEGV && fault.TrapNo == 13 &&
               fault.err_code == 0;
    }
};
} // namespace
const char *boundedBridgeCheckLabel(unsigned i)
{
    return Labels[i];
}
BoundedBridgeResults runBoundedBridgeProbe(Context *context, Thread *thread,
                                           uintptr_t code, uintptr_t data,
                                           uintptr_t stack,
                                           void (*log)(const char *))
{
    BoundedBridgeResults result;
    auto &state = thread->CurrentFrame->State;
    auto &frame = *thread->CurrentFrame;
    uint64_t allowance = 0;
    auto run = [&]() {
        ControlledInvocation call{ context, allowance };
        char stage[192];
        std::snprintf(stage, sizeof stage,
                      "bridge entry %u budget=%llu pc=%llx", result.entries,
                      (unsigned long long)allowance,
                      (unsigned long long)state.rip);
        if (log)
            log(stage);
        result.abiMask &= executeControlledGuest(call, thread);
        ++result.entries;
        const auto left = frame.ExecutionInstructionsRemaining;
        if (left > allowance) {
            std::snprintf(result.detail, sizeof result.detail,
                          "invalid remaining admission allowance");
            allowance = 0;
            return Reason::Unsupported;
        }
        allowance = left;
        std::snprintf(stage, sizeof stage,
                      "bridge return %u reason=%u remaining=%llu pc=%llx",
                      result.entries, unsigned(call.reason),
                      (unsigned long long)allowance,
                      (unsigned long long)state.rip);
        if (log)
            log(stage);
        return call.reason;
    };
    auto prepare = [&](bool publish = true) {
        context->ResetStopRequest(thread);
        if (publish) {
            context->ClearCodeCache(thread);
            std::memset(reinterpret_cast<void *>(code), 0xf4, 0x200);
            const uint8_t caller[] = { 0x6a, 22, 0x6a, 20, 0xe8,
                                       0xf7, 0,  0,    0,  0xf4 };
            const uint8_t callback[] = {
                0x8b, 0x44, 0x24, 4,    0x83, 0xc0, 1,    0x0f, 0x57,
                0xc0, 0x0f, 0xef, 0xc0, 0x0f, 0x77, 0xdb, 0xe3, 0x0f,
                0xae, 0x15, 0,    0,    0,    0, // LDMXCSR owned RAM
                0x64, 0x8b, 0x0d, 0,    0,    0,    0, // MOV ECX, FS:[0]
                0xfc, 0x43, 0x31, 0xf6, 0x31, 0xff, 0xc3
            };
            std::memcpy(reinterpret_cast<void *>(code), caller, sizeof caller);
            std::memcpy(reinterpret_cast<void *>(code + 0x140), callback,
                        sizeof callback);
            const uint32_t controlAddress = data + 0x2010;
            std::memcpy(reinterpret_cast<void *>(code + 0x154), &controlAddress,
                        4);
        }
        resetShadow(thread);
        *reinterpret_cast<uint32_t *>(data + 0x2000) = 0xabcddcba;
        *reinterpret_cast<uint32_t *>(data + 0x2010) = 0x1f80;
        state.rip = code;
        state.gregs[4] = stack + 0x1000;
        for (unsigned i : { 0U, 1U, 2U, 3U, 5U, 6U, 7U })
            state.gregs[i] = 0x12340000 + i;
        state.fs_cached = data;
        state.fs_idx = 0;
        state.FCW = 0x77f;
        state.mxcsr = 0x3f80;
        state.AbridgedFTW = 0xff;
        std::memset(state.flags + 32, 0, 16);
        state.flags[40] = 1; // x87 C0
        state.flags[43] = 3; // x87 TOP
        state.flags[46] = 1; // x87 C3
        for (unsigned i = 0; i < 8; ++i) {
            state.mm[i][0] = 0x8000000000000000ULL + i;
            state.mm[i][1] = 0x3fff;
        }
        std::memset(&state.xmm, 0x5a, sizeof state.xmm);
        context->SetFlagsFromCompactedEFLAGS(thread, 0xed7);
        frame.SynchronousFaultData = {};
    };
    auto callbackEntry = [&]() {
        resetShadow(thread);
        state.fs_cached =
            data + 0x2000; // Callback has distinct constructed TLS.
        auto *words = reinterpret_cast<uint32_t *>(stack + 0x800);
        words[0] = code + 0x180;
        words[1] = 20;
        state.rip = code + 0x140;
        state.gregs[4] = stack + 0x800;
    };
    auto normal = [&](bool verifyState) {
        prepare(verifyState);
        allowance = 64;
        Bridge bridge{ context, thread, code, stack };
        const auto first = run();
        if (!bridge.admit(first))
            return false;
        const uint64_t afterCaller = allowance;
        Snapshot saved(context, thread);
        callbackEntry();
        ++bridge.depth;
        const auto returned = run();
        --bridge.depth;
        const bool completed = bridge.completed(returned);
        const uint32_t value = state.gregs[0];
        if (!completed)
            return false;
        const bool changedFPU =
            std::memcmp(&state.xmm, &saved.state.xmm, sizeof state.xmm) != 0 &&
            std::memcmp(state.mm, saved.state.mm, sizeof state.mm) != 0 &&
            state.FCW == 0x37f && state.mxcsr == 0x1f80 &&
            state.AbridgedFTW == 0 && state.flags[43] == 0;
        const bool changedCPU = state.gregs[1] == 0xabcddcba &&
                                state.gregs[3] != saved.state.gregs[3] &&
                                state.fs_cached != saved.state.fs_cached &&
                                context->ReconstructCompactedEFLAGS(
                                    thread, false, nullptr, 0) != saved.flags;
        saved.restore(context, thread);
        resetShadow(thread);
        if (verifyState) {
            result.passed[2] =
                changedFPU &&
                !std::memcmp(&state.xmm, &saved.state.xmm, sizeof state.xmm) &&
                !std::memcmp(state.mm, saved.state.mm, sizeof state.mm) &&
                state.FCW == saved.state.FCW &&
                state.mxcsr == saved.state.mxcsr &&
                state.AbridgedFTW == saved.state.AbridgedFTW &&
                !std::memcmp(state.flags + 32, saved.state.flags + 32, 16);
            result.passed[3] =
                changedCPU &&
                context->ReconstructCompactedEFLAGS(thread, false, nullptr,
                                                    0) == saved.flags &&
                !std::memcmp(state.gregs, saved.state.gregs,
                             sizeof state.gregs) &&
                state.fs_cached == data && state.fs_idx == saved.state.fs_idx;
        }
        state.gregs[0] = value + bridge.args[2] - 1;
        state.gregs[4] = saved.state.gregs[4] + 12;
        state.rip = bridge.args[0];
        const uint64_t afterCallback = allowance;
        const auto final = run();
        return first == Reason::GuestExit && afterCaller == 60 &&
               afterCallback == 46 && allowance == 45 &&
               final == Reason::GuestExit && state.rip == code + 9 &&
               state.gregs[0] == 42 && state.gregs[4] == stack + 0x1000 &&
               state.gregs[3] == saved.state.gregs[3] && bridge.hostBodies == 1;
    };
    result.passed[0] = normal(true);
    result.passed[1] = normal(false);
    // All rejection cases reach the real service first; bad frames must not
    // reach a host body. A wrapping/unmapped ESP is never dereferenced.
    for (unsigned test = 4; test <= 10; ++test) {
        prepare();
        allowance = 64;
        Bridge bridge{ context, thread, code, stack };
        const auto reason = run();
        if (reason != Reason::GuestExit || state.rip != code + 0x100)
            continue;
        if (test == 4) {
            state.gregs[4] = stack + 0x1000 - 8;
            // Supply otherwise-valid words across the admitted stack boundary.
            // The final word is mapped fixture RAM, but outside the readable
            // argument window and must never be consumed by the bridge.
            const uint32_t words[] = { uint32_t(code + 9), 20, 22 };
            std::memcpy(reinterpret_cast<void *>(state.gregs[4]), words,
                        sizeof words);
        }
        if (test == 5)
            state.gregs[4] = 0xfffffffcULL;
        if (test == 6)
            *reinterpret_cast<uint32_t *>(state.gregs[4]) = code + 0x300;
        if (test == 7)
            state.rip = code + 0x101;
        if (test == 8) {
            bool rejected = !bridge.admit(Reason::StopRequested) &&
                            !bridge.admit(Reason::BudgetExhausted);
            frame.SynchronousFaultData.TrapNo = 6;
            result.passed[test] =
                rejected && !bridge.admit(reason) && !bridge.hostBodies;
            continue;
        }
        if (test == 9)
            bridge.depth = 1;
        if (test == 10)
            bridge.services = 1;
        result.passed[test] = !bridge.admit(reason) && !bridge.hostBodies;
    }
    // Finite callback gate before the looping callback variant.
    prepare();
    allowance = 5;
    Bridge finite{ context, thread, code, stack };
    const auto finiteFirst = run();
    bool finiteGate = finite.admit(finiteFirst) && allowance == 1;
    Snapshot suspended(context, thread);
    if (finiteGate) {
        callbackEntry();
        const auto reason = run();
        result.passed[11] = reason == Reason::BudgetExhausted &&
                            allowance == 0 && state.rip == code + 0x144 &&
                            state.gregs[0] == 20 && !finite.completed(reason) &&
                            suspended.state.rip == code + 0x100;
    }
    if (result.passed[11]) {
        prepare();
        allowance = 20;
        Bridge loop{ context, thread, code, stack };
        const auto first = run();
        if (loop.admit(first)) {
            const uint8_t spin[] = { 0x40, 0xeb, 0xfd };
            // Clear the cache before publishing a modified owned code fixture.
            context->ClearCodeCache(thread);
            std::memcpy(reinterpret_cast<void *>(code + 0x140), spin,
                        sizeof spin);
            callbackEntry();
            state.gregs[0] = 0;
            const auto reason = run();
            result.passed[12] = reason == Reason::BudgetExhausted &&
                                allowance == 0 && state.rip == code + 0x140 &&
                                state.gregs[0] == 8 && !loop.completed(reason);
        }
    }
    prepare();
    allowance = 64;
    Bridge stopped{ context, thread, code, stack };
    const auto stoppedFirst = run();
    if (stopped.admit(stoppedFirst)) {
        callbackEntry();
        const Snapshot before(context, thread);
        const bool requested = context->RequestStop(thread);
        const auto first = run();
        const auto second = run();
        result.passed[13] =
            requested && first == Reason::StopRequested && second == first &&
            allowance == 60 && state.rip == before.state.rip &&
            !std::memcmp(state.gregs, before.state.gregs, sizeof state.gregs) &&
            !stopped.admit(first) && stopped.hostBodies == 1 &&
            context->ResetStopRequest(thread);
    }
    prepare();
    allowance = 64;
    Bridge faulted{ context, thread, code, stack };
    const auto faultFirst = run();
    if (faulted.admit(faultFirst)) {
        context->ClearCodeCache(thread);
        const uint8_t ud2[] = { 0x0f, 0x0b };
        std::memcpy(reinterpret_cast<void *>(code + 0x140), ud2, sizeof ud2);
        callbackEntry();
        const auto reason = run();
        result.passed[14] = reason == Reason::GuestExit &&
                            state.rip == code + 0x140 &&
                            frame.SynchronousFaultData.Signal == SIGILL &&
                            frame.SynchronousFaultData.TrapNo == 6 &&
                            !faulted.completed(reason) && allowance == 59 &&
                            faulted.hostBodies == 1;
    }
    prepare();
    allowance = 64;
    Bridge malformed{ context, thread, code, stack };
    const auto malformedFirst = run();
    if (malformed.admit(malformedFirst)) {
        callbackEntry();
        const auto reason = run();
        bool rejected = malformed.completed(reason);
        state.gregs[4] += 4;
        rejected &= !malformed.completed(reason);
        state.gregs[4] -= 4;
        state.rip += 1;
        rejected &= !malformed.completed(reason);
        result.passed[15] = rejected && malformed.hostBodies == 1;
    }
#ifdef __ANDROID__
    if (result.passed[11] && result.passed[12]) {
        prepare();
        allowance = 10000000;
        Bridge native{ context, thread, code, stack };
        const auto first = run();
        if (native.admit(first)) {
            context->ClearCodeCache(thread);
            const uint8_t loop[] = { 0xf0, 0xff, 0x05, 0, 0, 0, 0, 0xeb, 0xf7 };
            std::memcpy(reinterpret_cast<void *>(code + 0x140), loop,
                        sizeof loop);
            const uint32_t address = data + 0x2000;
            std::memcpy(reinterpret_cast<void *>(code + 0x143), &address, 4);
            const Snapshot suspendedCaller(context, thread);
            callbackEntry();
            const Snapshot callbackState(context, thread);
            auto &word = *reinterpret_cast<uint32_t *>(address);
            std::atomic_ref<uint32_t> heartbeat(word);
            heartbeat.store(0);
            FEXCore::Allocator::UninstallTLSData(thread);
            ControlledInvocation call{ context, allowance };
            uint64_t mask = 0;
            std::thread worker([&] {
                FEXCore::Allocator::RegisterTLSData(thread);
                mask = executeControlledGuest(call, thread);
                FEXCore::Allocator::UninstallTLSData(thread);
            });
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!heartbeat.load(std::memory_order_acquire) &&
                   std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            const bool progress = heartbeat.load(std::memory_order_acquire) > 0;
            const auto start = std::chrono::steady_clock::now();
            const bool requested = context->RequestStop(thread);
            worker.join();
            result.stopLatencyUs =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count();
            result.abiMask &= mask;
            ++result.entries;
            FEXCore::Allocator::RegisterTLSData(thread);
            const bool exactPC =
                state.rip == code + 0x140 || state.rip == code + 0x147;
            const uint32_t value = heartbeat.load();
            uint32_t expectedFlags = 0x603; // CF/DF/IF and architectural bit1.
            expectedFlags |= !(std::popcount(value & 0xff) & 1) ? 4 : 0;
            expectedFlags |= (value & 15) == 0 ? 0x10 : 0;
            expectedFlags |= value == 0 ? 0x40 : 0;
            expectedFlags |= value >> 31 ? 0x80 : 0;
            expectedFlags |= value == 0x80000000U ? 0x800 : 0;
            result.native[0] =
                context->ReconstructCompactedEFLAGS(thread, false, nullptr,
                                                    0) == expectedFlags &&
                progress && requested && call.reason == Reason::StopRequested &&
                exactPC &&
                !std::memcmp(state.gregs, callbackState.state.gregs,
                             sizeof state.gregs) &&
                !native.completed(call.reason) && native.hostBodies == 1 &&
                !frame.SynchronousFaultData.FaultToTopAndGeneratedException;
            const uint64_t remaining = frame.ExecutionInstructionsRemaining;
            const uint64_t stoppedPC = state.rip;
            const uint32_t stoppedFlags =
                context->ReconstructCompactedEFLAGS(thread, false, nullptr, 0);
            allowance = remaining;
            const auto again = run();
            result.native[1] =
                remaining > 0 && remaining < 9999996 &&
                again == Reason::StopRequested && allowance == remaining &&
                state.rip == stoppedPC &&
                context->ReconstructCompactedEFLAGS(thread, false, nullptr,
                                                    0) == stoppedFlags &&
                !std::memcmp(state.gregs, callbackState.state.gregs,
                             sizeof state.gregs) &&
                suspendedCaller.state.rip == code + 0x100 &&
                suspendedCaller.state.gregs[4] == stack + 0x1000 - 12 &&
                native.hostBodies == 1 && context->ResetStopRequest(thread);
        }
    }
#endif
    resetShadow(thread);
    return result;
}
} // namespace armxbox
