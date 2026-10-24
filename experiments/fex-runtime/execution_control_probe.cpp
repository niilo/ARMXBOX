#include "execution_control_probe.h"
#include "execution_boundary.h"
#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/HostFeatures.h>
#include <FEXCore/Core/SignalDelegator.h>
#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/Utils/AllocatorHooks.h>
#include <atomic>
#include <bit>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <thread>

namespace armxbox {
namespace {
using Context = FEXCore::Context::Context;
using Reason = FEXCore::Context::ExecutionResult;
using Thread = FEXCore::Core::InternalThreadState;
constexpr const char *Labels[] = {
    "default and foreign-owner contexts reject controlled APIs",
    "checkpoint contexts reject incompatible mode and caches",
    "zero admission budget returns before any guest instruction",
    "one instruction budget stops before next register change",
    "warm budget resume continues at exact guest PC",
    "guest HLT remains a guest exit distinct from budget stop",
    "direct nonterminating loop returns on admission budget",
    "conditional backedge returns with coherent guest state",
    "linked guest blocks share one admission budget",
    "pre-entry stop stays latched across repeated runs",
    "explicit stopped-thread reset permits execution again",
    "controlled divide fault can be repaired and resumed",
    "checkpoint preserves SIMD state before pending store",
    "checkpoint preserves x87 stack across stop and resume",
    "checkpoint preserves MMX data and EMMS tag state across resume",
};
static_assert(std::size(Labels) == ExecutionCheckCount);
struct Invocation {
    Context *context;
    uint64_t budget;
    Reason reason = Reason::Unsupported;
};
uint64_t enter(Invocation &call, Thread *thread)
{
#ifdef __aarch64__
    uint64_t checks[6]{};
    armxbox_probe_execute_abi(
        &call, thread,
        [](void *opaque, void *cpu) {
            auto &call = *static_cast<Invocation *>(opaque);
            uint64_t fpcr, fpsr;
            asm volatile("mrs %0, fpcr\n mrs %1, fpsr"
                         : "=r"(fpcr), "=r"(fpsr)::"memory");
            call.reason = call.context->ExecuteControlledThread(
                static_cast<Thread *>(cpu), call.budget);
            asm volatile("msr fpcr, %0\n msr fpsr, %1" ::"r"(fpcr), "r"(fpsr)
                         : "memory");
        },
        checks);
    uint64_t mask = 0;
    for (unsigned i = 0; i < 6; ++i)
        mask |= uint64_t(checks[i] != 0) << i;
    return mask;
#else
    call.reason = call.context->ExecuteControlledThread(thread, call.budget);
    return 0;
#endif
}
} // namespace
const char *executionCheckLabel(unsigned i)
{
    return Labels[i];
}
ExecutionControlResults runExecutionControlProbe(
    const FEXCore::HostFeatures &features, FEXCore::HLE::SyscallHandler *calls,
    uintptr_t code, uintptr_t data, uintptr_t stack, void (*log)(const char *))
{
    ExecutionControlResults result;
    using Profile = FEXCore::CPUID::IdentityProfile;
    {
        auto plain = Context::CreateNewContext(features);
        result.passed[0] =
            plain &&
            plain->ExecuteControlledThread(nullptr, 1) == Reason::Unsupported &&
            !plain->RequestStop(nullptr) && !plain->ResetStopRequest(nullptr);
    }
    result.passed[1] = true;
    for (auto option : { FEXCore::Config::CONFIG_IS64BIT_MODE,
                         FEXCore::Config::CONFIG_DISKCACHE,
                         FEXCore::Config::CONFIG_ENABLECODECACHINGWIP,
                         FEXCore::Config::CONFIG_ENABLECODECACHEVALIDATION }) {
        FEXCore::Config::Set(option, "1");
        result.passed[1] &=
            !Context::CreateNewContext(features, Profile::Default, true);
        FEXCore::Config::Set(option, "0");
    }
    FEXCore::SignalDelegator signals;
    auto context =
        Context::CreateNewContext(features, Profile::XboxReference, true);
    if (!context)
        return result;
    context->SetSignalDelegator(&signals);
    context->SetSyscallHandler(calls);
    if (!context->InitCore())
        return result;
    auto *thread = context->CreateThread(nullptr);
    FEXCore::Allocator::RegisterTLSData(thread);
    auto &state = thread->CurrentFrame->State;
    {
        auto foreign =
            Context::CreateNewContext(features, Profile::XboxReference, true);
        result.passed[0] &=
            foreign && !foreign->RequestStop(thread) &&
            !foreign->ResetStopRequest(thread) &&
            foreign->ExecuteControlledThread(thread, 1) == Reason::Unsupported;
    }

    FEXCore::Core::CPUState::gdt_segment gdt[32]{};
    gdt[1].D = 1;
    state.segment_arrays[0] = gdt;
    state.cs_idx = 8;
    constexpr size_t ShadowSize = Thread::CALLRET_STACK_SIZE;
    void *shadow =
        FEXCore::Allocator::mmap(nullptr, ShadowSize, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (shadow == MAP_FAILED) {
        FEXCore::Allocator::UninstallTLSData(thread);
        context->DestroyThread(thread);
        return result;
    }
    thread->CallRetStackBase = shadow;
    state.callret_sp = reinterpret_cast<uintptr_t>(shadow) + ShadowSize / 4;
    state.mxcsr = 0x1f80;
    auto &pointers = thread->CurrentFrame->Pointers;
    pointers.GuestSignal_SIGILL = pointers.ThreadStopHandlerSpillSRA;
    pointers.GuestSignal_SIGTRAP = pointers.ThreadStopHandlerSpillSRA;
    pointers.GuestSignal_SIGSEGV = pointers.ThreadStopHandlerSpillSRA;
    pointers.GuestSignal_SynchronousFault = pointers.ThreadStopHandlerSpillSRA;
    auto setup = [&](std::initializer_list<uint8_t> bytes) {
        context->ClearCodeCache(thread);
        std::memcpy(reinterpret_cast<void *>(code), bytes.begin(),
                    bytes.size());
        state.rip = code;
        state.gregs[0] = 0xffffffff;
        state.gregs[3] = 0;
        state.gregs[1] = 0;
        state.gregs[2] = 0;
        state.gregs[4] = stack;
        context->SetFlagsFromCompactedEFLAGS(thread, 0xed7);
    };
    auto run = [&](uint64_t budget) {
        char stage[160];
        std::snprintf(stage, sizeof stage,
                      "control entry %u budget=%llu pc=%llx", result.entries,
                      (unsigned long long)budget,
                      (unsigned long long)state.rip);
        if (log)
            log(stage);
        Invocation call{ context.get(), budget };
        result.abiMask &= enter(call, thread);
        ++result.entries;
        std::snprintf(stage, sizeof stage,
                      "control return %u reason=%u remaining=%llu pc=%llx",
                      result.entries, unsigned(call.reason),
                      (unsigned long long)
                          thread->CurrentFrame->ExecutionInstructionsRemaining,
                      (unsigned long long)state.rip);
        if (log)
            log(stage);
        return call.reason;
    };
    auto cleanStop = [&] {
        const auto &fault = thread->CurrentFrame->SynchronousFaultData;
        return !fault.FaultToTopAndGeneratedException && fault.Signal == 0 &&
               fault.TrapNo == 0;
    };
    setup({ 0x40, 0x43, 0xf4 });
    result.passed[2] = run(0) == Reason::BudgetExhausted && state.rip == code &&
                       state.gregs[0] == 0xffffffff && cleanStop();
    result.passed[3] =
        run(1) == Reason::BudgetExhausted && state.rip == code + 1 &&
        state.gregs[0] == 0 && state.gregs[3] == 0 && cleanStop() &&
        context->ReconstructCompactedEFLAGS(thread, false, nullptr, 0) == 0x657;
    // Do not admit nonterminating fixtures unless finite stop/state checks
    // work.
    if (!result.passed[2] || !result.passed[3]) {
        std::snprintf(
            result.detail, sizeof result.detail,
            "finite checkpoint gate failed: pc=%llx eax=%llx flags=%x; "
            "nonterminating tests not admitted",
            (unsigned long long)state.rip, (unsigned long long)state.gregs[0],
            context->ReconstructCompactedEFLAGS(thread, false, nullptr, 0));
    } else {
        result.passed[4] = run(1) == Reason::BudgetExhausted &&
                           state.rip == code + 2 && state.gregs[0] == 0 &&
                           state.gregs[3] == 1 && cleanStop();
        result.passed[5] =
            run(1) == Reason::GuestExit && state.rip == code + 2 &&
            thread->CurrentFrame->SynchronousFaultData.TrapNo == 13;
        setup({ 0x40, 0xeb, 0xfd });
        state.gregs[0] = 0;
        result.passed[6] =
            run(20) == Reason::BudgetExhausted && state.gregs[0] == 10 &&
            state.rip == code && cleanStop() &&
            context->ReconstructCompactedEFLAGS(thread, false, nullptr, 0) ==
                0x607;
        setup({ 0x40, 0x49, 0x75, 0xfc });
        state.gregs[0] = 0;
        state.gregs[1] = 0xffffffff;
        result.passed[7] =
            run(30) == Reason::BudgetExhausted && state.gregs[0] == 10 &&
            state.gregs[1] == 0xfffffff5 && state.rip == code && cleanStop();
        setup({ 0xe9, 0xfb, 0, 0, 0 });
        const uint8_t linked[] = {
            0x40, 0xe9, 0xfa, 0xfe, 0xff, 0xff
        }; // jump back to code
        std::memcpy(reinterpret_cast<void *>(code + 0x100), linked,
                    sizeof linked);
        state.gregs[0] = 0;
        result.passed[8] = run(9) == Reason::BudgetExhausted &&
                           state.gregs[0] == 3 && state.rip == code &&
                           cleanStop();
        setup({ 0x40, 0x43, 0xf4 });
        result.passed[9] =
            context->RequestStop(thread) && context->RequestStop(thread) &&
            run(10) == Reason::StopRequested &&
            run(10) == Reason::StopRequested && state.rip == code &&
            state.gregs[0] == 0xffffffff && cleanStop();
        result.passed[10] = context->ResetStopRequest(thread) &&
                            run(1) == Reason::BudgetExhausted &&
                            state.gregs[0] == 0 && state.rip == code + 1;
        setup({ 0xf7, 0xf1, 0x40, 0xf4 });
        state.gregs[0] = 6;
        result.passed[11] =
            run(1) == Reason::GuestExit &&
            thread->CurrentFrame->SynchronousFaultData.Signal == SIGFPE &&
            state.rip == code && state.gregs[0] == 6;
        state.gregs[1] = 2;
        result.passed[11] &= run(2) == Reason::BudgetExhausted &&
                             state.gregs[0] == 4 && state.rip == code + 3;
        // The inherited alias fixture maps data and data+0x1000 to the same
        // backing.
        const uint32_t output = data + 0x2000;
        setup({ 0x0f, 0x57, 0xc0, 0x0f, 0x58, 0xc1, 0x0f, 0x11, 0x05, 0, 0, 0,
                0, 0xf4 });
        std::memcpy(reinterpret_cast<void *>(code + 9), &output, 4);
        __uint128_t xmm[16]{};
        const uint32_t lanes[] = { 0x3f800000, 0x40000000, 0x40400000,
                                   0x40800000 };
        std::memcpy(&xmm[1], lanes, sizeof lanes);
        context->SetXMMRegistersFromState(thread, xmm, nullptr);
        *reinterpret_cast<uint32_t *>(output) = 0x12345678;
        const auto simdReason = run(2);
        context->ReconstructXMMRegisters(thread, xmm, nullptr);
        result.passed[12] = simdReason == Reason::BudgetExhausted &&
                            state.rip == code + 6 &&
                            !std::memcmp(&xmm[0], lanes, sizeof lanes) &&
                            *reinterpret_cast<uint32_t *>(output) == 0x12345678;
        setup({ 0xdb, 0xe3, 0xd9, 0xe8, 0xd9, 0x1d, 0, 0, 0, 0, 0xf4 });
        std::memcpy(reinterpret_cast<void *>(code + 6), &output, 4);
        *reinterpret_cast<uint32_t *>(output) = 0x12345678;
        result.passed[13] = run(2) == Reason::BudgetExhausted &&
                            state.rip == code + 4 &&
                            *reinterpret_cast<uint32_t *>(output) == 0x12345678;
        result.passed[13] &=
            run(1) == Reason::BudgetExhausted && state.rip == code + 10 &&
            *reinterpret_cast<uint32_t *>(output) == 0x3f800000;
        setup({ 0x0f, 0x6f, 0x05, 0, 0, 0, 0, 0x0f, 0x7f, 0x05, 0, 0, 0, 0,
                0x0f, 0x77, 0xf4 });
        const uint32_t mmxInput = data;
        std::memcpy(reinterpret_cast<void *>(code + 3), &mmxInput, 4);
        std::memcpy(reinterpret_cast<void *>(code + 10), &output, 4);
        *reinterpret_cast<uint64_t *>(data) = 0x0123456789abcdefULL;
        *reinterpret_cast<uint64_t *>(output) = 0;
        result.passed[14] = run(1) == Reason::BudgetExhausted &&
                            state.rip == code + 7 &&
                            *reinterpret_cast<uint64_t *>(output) == 0;
        result.passed[14] &=
            run(2) == Reason::BudgetExhausted && state.rip == code + 16 &&
            *reinterpret_cast<uint64_t *>(output) == 0x0123456789abcdefULL &&
            state.AbridgedFTW == 0;
        if (!result.passed[14]) {
            std::snprintf(
                result.detail, sizeof result.detail,
                "MMX checkpoint pc=%llx value=%llx ftw=%x",
                (unsigned long long)state.rip,
                (unsigned long long)*reinterpret_cast<uint64_t *>(output),
                unsigned(state.AbridgedFTW));
        }
#ifdef __ANDROID__
        // An atomic guest heartbeat establishes actual progress before
        // requesting stop. A large finite admission budget is a fallback if a
        // request fails.
        for (unsigned trial = 0; trial < 2; ++trial) {
            setup({ 0xf0, 0xff, 0x05, 0, 0, 0, 0, 0xeb, 0xf7 });
            const uint32_t heartbeatAddress = data;
            std::memcpy(reinterpret_cast<void *>(code + 3), &heartbeatAddress,
                        4);
            auto &heartbeatWord = *reinterpret_cast<uint32_t *>(data);
            std::atomic_ref<uint32_t> heartbeat(heartbeatWord);
            heartbeat.store(0);
            context->ResetStopRequest(thread);
            FEXCore::Allocator::UninstallTLSData(thread);
            std::array<uint64_t, 8> before{};
            std::memcpy(before.data(), state.gregs, sizeof before);
            Invocation call{ context.get(), 10000000 };
            uint64_t mask = 0;
            std::thread worker([&] {
                FEXCore::Allocator::RegisterTLSData(thread);
                mask = enter(call, thread);
                FEXCore::Allocator::UninstallTLSData(thread);
            });
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (heartbeat.load(std::memory_order_acquire) == 0 &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            const bool progress = heartbeat.load(std::memory_order_acquire) > 0;
            const bool running =
                std::atomic_ref(thread->CurrentFrame->ExecutionControlState)
                    .load(std::memory_order_acquire) &
                1;
            const bool busy =
                running &&
                context->ExecuteControlledThread(thread, 1) == Reason::Busy &&
                !context->ResetStopRequest(thread);
            const auto start = std::chrono::steady_clock::now();
            const bool requested = context->RequestStop(thread);
            worker.join();
            result.stopLatencyUs[trial] =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count();
            result.abiMask &= mask;
            ++result.entries;
            FEXCore::Allocator::RegisterTLSData(thread);
            result.native[trial * 2] = progress && busy && requested &&
                                       call.reason == Reason::StopRequested &&
                                       cleanStop();
            const uint32_t value = heartbeat.load();
            uint32_t expectedFlags =
                0x603; // preserved CF, DF, IF and reserved bit1
            expectedFlags |= !(std::popcount(value & 0xff) & 1) ? 4 : 0;
            expectedFlags |= (value & 15) == 0 ? 0x10 : 0;
            expectedFlags |= value == 0 ? 0x40 : 0;
            expectedFlags |= (value >> 31) ? 0x80 : 0;
            expectedFlags |= value == 0x80000000U ? 0x800 : 0;
            result.native[trial * 2 + 1] =
                (state.rip == code || state.rip == code + 7) && value > 0 &&
                !std::memcmp(before.data(), state.gregs, sizeof before) &&
                context->ReconstructCompactedEFLAGS(thread, false, nullptr,
                                                    0) == expectedFlags &&
                context->ResetStopRequest(thread);
        }
#endif
    }
    FEXCore::Allocator::UninstallTLSData(thread);
    context->DestroyThread(thread);
    FEXCore::Allocator::munmap(shadow, ShadowSize);
    return result;
}
} // namespace armxbox
