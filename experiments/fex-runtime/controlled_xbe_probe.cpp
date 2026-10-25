// SPDX-License-Identifier: GPL-2.0-or-later
#include "controlled_xbe.h"
#include "execution_boundary.h"
#include "guest_reservation.h"
#include <FEXCore/Core/HostFeatures.h>
#include <FEXCore/Core/SignalDelegator.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/Utils/AllocatorHooks.h>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>

namespace armxbox {
namespace {
using Context = FEXCore::Context::Context;
using Thread = FEXCore::Core::InternalThreadState;
using Reason = FEXCore::Context::ExecutionResult;
constexpr uint32_t Page = 4096, StackVA = XbeBase + 0x10000;
constexpr uint32_t StackBegin = StackVA + Page, StackEnd = StackVA + 3 * Page;
constexpr uint32_t InitialSP = StackEnd - 16;
constexpr const char *Labels[] = {
    "generated XBE header and bounded image plan are admitted",
    "XBE truncation overflow and malformed headers fail before mapping",
    "XBE invalid section extents flags and overlap fail before mapping",
    "XBE unsupported TLS imports encodings and guest code are refused",
    "XBE low mapping conflict preserves existing host bytes",
    "XBE image and guarded stack reserve exact owned low addresses",
    "XBE image data stack and guards have expected host protections",
    "XBE raw sections load exactly and BSS starts zero",
    "XBE ordinal284 RtlFillMemory executes once with typed stdcall frame",
    "XBE guest continuation publishes fill result and exact completion state",
    "XBE warm repeat retains code and shared finite instruction allowance",
    "XBE invalid service frames and repeated dispatch have no host effects",
    "XBE zero allowance returns before image entry or service effects",
    "XBE finite allowance stops before kernel dispatch",
    "XBE latched stop returns before image entry or service effects",
};
static_assert(std::size(Labels) == XbeCheckCount);
void *reserve(uint32_t address, size_t size)
{
    return reserveGuestExact(address, size).address;
}
struct Loaded {
    void *image = nullptr, *stack = nullptr;
    GuestReservation imageResult, stackResult;
    const char *stage = "unstarted";
    int protectionError = 0;
    void (*log)(const char *) = nullptr;
    explicit Loaded(void (*logger)(const char *) = nullptr) : log(logger)
    {
    }
    ~Loaded()
    {
        if (stack)
            ::munmap(stack, 4 * Page);
        if (image)
            ::munmap(image, XbeImageSize);
    }
    void traceReservation(const char *name, uint32_t address, size_t size,
                          const GuestReservation &r)
    {
        char text[224];
        std::snprintf(text, sizeof text,
                      "XBE %s reserve requested=%x size=%zu ok=%u "
                      "noReplaceErr=%d hint=%u hintErr=%d alternate=%u",
                      name, address, size, unsigned(r.address != nullptr),
                      r.noReplaceError, r.usedHint, r.hintError, r.alternate);
        if (log)
            log(text);
    }
    bool protect(void *address, size_t size, int flags, const char *name)
    {
        stage = name;
        if (!::mprotect(address, size, flags))
            return true;
        protectionError = errno;
        char text[192];
        std::snprintf(text, sizeof text,
                      "XBE protection failed stage=%s address=%llx size=%zu "
                      "prot=%d errno=%d",
                      stage,
                      (unsigned long long)reinterpret_cast<uintptr_t>(address),
                      size, flags, protectionError);
        if (log)
            log(text);
        return false;
    }
    bool load(const GeneratedXbe &bytes, const XbePlan &plan)
    {
        stage = "image-reserve";
        imageResult = reserveGuestExact(XbeBase, XbeImageSize);
        image = imageResult.address;
        traceReservation("image", XbeBase, XbeImageSize, imageResult);
        if (!image)
            return false;
        stage = "stack-reserve";
        stackResult = reserveGuestExact(StackVA, 4 * Page);
        stack = stackResult.address;
        traceReservation("stack", StackVA, 4 * Page, stackResult);
        if (!stack)
            return false;
        if (!protect(image, XbeImageSize, PROT_READ | PROT_WRITE, "image-RW") ||
            !protect(reinterpret_cast<void *>(uintptr_t(StackBegin)), 2 * Page,
                     PROT_READ | PROT_WRITE, "stack-RW"))
            return false;
        std::memcpy(image, bytes.data(), Page);
        for (unsigned i = 0; i < 2; ++i)
            std::memcpy(static_cast<uint8_t *>(image) + plan.virtualOffset[i],
                        bytes.data() + plan.rawOffset[i], plan.rawSize[i]);
        // Guest instructions are ARM-host data: guest RX text is host RO.
        if (!protect(image, 2 * Page, PROT_READ, "header-text-RO"))
            return false;
        stage = "mapped";
        return true;
    }
    void detail(char *text, size_t capacity, bool oldOverlap, bool dalvik) const
    {
        std::snprintf(
            text, capacity,
            "stage=%s image=%x stack=%x nrErr=%d/%d hint=%u/%u hintErr=%d/%d "
            "protErr=%d old20000000Overlap=%u dalvik=%u",
            stage, XbeBase, StackVA, imageResult.noReplaceError,
            stackResult.noReplaceError, imageResult.usedHint,
            stackResult.usedHint, imageResult.hintError, stackResult.hintError,
            protectionError, oldOverlap, dalvik);
    }
};
// Report overlaps without changing protections or exposing mapped file paths.
bool describeOldRange(void (*log)(const char *), bool &dalvik)
{
    FILE *f = std::fopen("/proc/self/maps", "r");
    dalvik = false;
    if (!f) {
        if (log)
            log("XBE old-range map inspection unavailable");
        return false;
    }
    char line[512], flags[5];
    unsigned long long begin, end;
    unsigned overlaps = 0;
    while (std::fgets(line, sizeof line, f)) {
        if (std::sscanf(line, "%llx-%llx %4s", &begin, &end, flags) != 3 ||
            begin >= 0x20004000ULL || end <= 0x20000000ULL)
            continue;
        dalvik |= std::strstr(line, "dalvik") != nullptr;
        if (log && overlaps < 3) {
            char text[160];
            std::snprintf(text, sizeof text,
                          "XBE previous range overlap begin=%llx end=%llx "
                          "prot=%s dalvik=%u",
                          begin, end, flags,
                          unsigned(std::strstr(line, "dalvik") != nullptr));
            log(text);
        }
        ++overlaps;
    }
    std::fclose(f);
    if (log && !overlaps)
        log("XBE previous range has no observed host mapping");
    return overlaps != 0;
}
bool protection(uint32_t address, const char *wanted)
{
    FILE *f = std::fopen("/proc/self/maps", "r");
    if (!f)
        return false;
    char line[512], flags[5];
    unsigned long long begin, end;
    bool ok = false;
    while (std::fgets(line, sizeof line, f)) {
        if (std::sscanf(line, "%llx-%llx %4s", &begin, &end, flags) == 3 &&
            address >= begin && address < end) {
            ok = !std::memcmp(flags, wanted, 3);
            break;
        }
    }
    std::fclose(f);
    return ok;
}
uint32_t word(uint32_t address)
{
    uint32_t value;
    std::memcpy(&value, reinterpret_cast<void *>(uintptr_t(address)), 4);
    return value;
}
// Executable admission is local to this owned image/context. The general CPU
// diagnostic's handler only admits its original fixture address range.
class ImageCalls final : public FEXCore::HLE::SyscallHandler {
    FEXCore::HLE::SyscallHandler *parent;

public:
    explicit ImageCalls(FEXCore::HLE::SyscallHandler *p) : parent(p)
    {
    }
    void HandleSyscall(FEXCore::Core::CpuStateFrame *frame) override
    {
        parent->HandleSyscall(frame);
    }
    FEXCore::HLE::ExecutableRangeInfo
    QueryGuestExecutableRange(Thread *, uint64_t address) override
    {
        return address >= XbeEntry && address < XbeEntry + Page ?
                   FEXCore::HLE::ExecutableRangeInfo{ XbeEntry, Page, false } :
                   FEXCore::HLE::ExecutableRangeInfo{ 0, 0, false };
    }
    std::optional<FEXCore::ExecutableFileSectionInfo>
    LookupExecutableFileSection(Thread *, uint64_t) override
    {
        return std::nullopt;
    }
};
struct Service {
    Thread *thread;
    unsigned bodies = 0;
    bool dispatch(Reason reason)
    {
        auto &state = thread->CurrentFrame->State;
        const auto &fault = thread->CurrentFrame->SynchronousFaultData;
        if (bodies || reason != Reason::GuestExit ||
            state.rip != XbeImportStub ||
            !fault.FaultToTopAndGeneratedException || fault.Signal != SIGSEGV ||
            fault.TrapNo != 13 || fault.err_code || fault.si_code != 0x80)
            return false;
        const uint64_t esp = state.gregs[4];
        if (esp < StackBegin || esp > StackEnd - 16)
            return false;
        uint32_t args[4];
        std::memcpy(args, reinterpret_cast<void *>(esp), sizeof args);
        if (args[0] != XbeReturn || args[1] != XbeFill || args[2] != 32 ||
            args[3] != 0x5a)
            return false;
        // Verified ordinal 284: VOID NTAPI RtlFillMemory(PVOID,DWORD,BYTE).
        // Admission limits this host body to 32 owned writable bytes.
        std::memset(reinterpret_cast<void *>(uintptr_t(args[1])),
                    uint8_t(args[3]), args[2]);
        ++bodies;
        state.rip = args[0];
        state.gregs[4] = esp + sizeof args; // stdcall callee cleanup.
        return true;
    }
};
} // namespace
const char *controlledXbeCheckLabel(unsigned i)
{
    return Labels[i];
}
ControlledXbeResults
runControlledXbeProbe(const FEXCore::HostFeatures &features,
                      FEXCore::HLE::SyscallHandler *calls,
                      void (*log)(const char *))
{
    ControlledXbeResults result;
    if (log)
        log("XBE: validating generated image and malformed controls");
    const auto controls = controlledXbeAdmissionControls();
    std::copy(controls.begin(), controls.end(), result.passed.begin());
    const auto bytes = generateControlledXbe();
    XbePlan plan;
    if (!admitControlledXbe(bytes, plan))
        return result;
    // Try the same loader against a conflicting owned host mapping first.
    {
        void *held = reserve(XbeBase, XbeImageSize);
        if (held && !::mprotect(held, Page, PROT_READ | PROT_WRITE)) {
            // Force an actual read after the conflict attempt.
            auto *p = static_cast<volatile uint32_t *>(held);
            *p = 0x7c12ef49;
            Loaded conflict;
            result.passed[4] = !conflict.load(bytes, plan) && *p == 0x7c12ef49;
        }
        if (held)
            ::munmap(held, XbeImageSize);
    }
    if (log)
        log("XBE: reserving image and guarded stack");
    bool dalvik = false;
    const bool oldOverlap = describeOldRange(log, dalvik);
    Loaded loaded(log);
    result.passed[5] = loaded.load(bytes, plan);
    loaded.detail(result.detail, sizeof result.detail, oldOverlap, dalvik);
    if (!result.passed[5]) {
        if (log)
            log("XBE setup blocked; downstream execution checks are unmet "
                "prerequisites");
        return result;
    }
    result.passed[6] =
        protection(XbeBase, "r--") && protection(XbeEntry, "r--") &&
        protection(XbeThunk, "rw-") && protection(XbeFill, "rw-") &&
        protection(StackVA, "---") && protection(StackBegin, "rw-") &&
        protection(StackEnd, "---");
    result.passed[7] =
        !std::memcmp(reinterpret_cast<void *>(uintptr_t(XbeBase)), bytes.data(),
                     bytes.size()) &&
        std::all_of(
            reinterpret_cast<const uint8_t *>(uintptr_t(XbeFill)),
            reinterpret_cast<const uint8_t *>(uintptr_t(XbeFill + Page)),
            [](uint8_t b) { return b == 0; });
    // Resolve the single previously validated ordinal into our owned exit stub.
    uint32_t stub = XbeImportStub;
    std::memcpy(reinterpret_cast<void *>(uintptr_t(XbeThunk)), &stub, 4);
    ImageCalls imageCalls(calls);
    FEXCore::SignalDelegator signals;
    auto context = Context::CreateNewContext(
        features, FEXCore::CPUID::IdentityProfile::XboxReference, true,
        FEXCore::Context::InstructionPolicy::XboxReferenceSubset);
    if (!context)
        return result;
    context->SetSignalDelegator(&signals);
    context->SetSyscallHandler(&imageCalls);
    if (!context->InitCore())
        return result;
    Thread *thread = context->CreateThread(nullptr);
    FEXCore::Allocator::RegisterTLSData(thread);
    constexpr size_t Shadow = Thread::CALLRET_STACK_SIZE;
    void *shadow =
        FEXCore::Allocator::mmap(nullptr, Shadow + 2 * Page, PROT_NONE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (shadow == MAP_FAILED || ::mprotect(static_cast<char *>(shadow) + Page,
                                           Shadow, PROT_READ | PROT_WRITE)) {
        if (shadow != MAP_FAILED)
            FEXCore::Allocator::munmap(shadow, Shadow + 2 * Page);
        FEXCore::Allocator::UninstallTLSData(thread);
        context->DestroyThread(thread);
        return result;
    }
    thread->CallRetStackBase = static_cast<char *>(shadow) + Page;
    auto &frame = *thread->CurrentFrame;
    auto &state = frame.State;
    FEXCore::Core::CPUState::gdt_segment gdt[32]{};
    gdt[1].D = 1;
    state.segment_arrays[0] = gdt;
    state.cs_idx = 8;
    frame.Pointers.GuestSignal_SIGILL =
        frame.Pointers.ThreadStopHandlerSpillSRA;
    frame.Pointers.GuestSignal_SIGTRAP =
        frame.Pointers.ThreadStopHandlerSpillSRA;
    frame.Pointers.GuestSignal_SIGSEGV =
        frame.Pointers.ThreadStopHandlerSpillSRA;
    uint64_t allowance = 64;
    auto prepare = [&] {
        context->ResetStopRequest(thread);
        std::memset(thread->CallRetStackBase, 0, Shadow);
        state.callret_sp =
            reinterpret_cast<uintptr_t>(thread->CallRetStackBase) + Shadow / 4;
        std::memset(state.gregs, 0, sizeof state.gregs);
        state.rip = plan.entry;
        state.gregs[4] = InitialSP;
        state.mxcsr = 0x1f80;
        state.FCW = 0x37f;
        context->SetFlagsFromCompactedEFLAGS(thread, 0x202);
        std::memset(reinterpret_cast<void *>(uintptr_t(XbeFill)), 0, Page);
        std::memset(reinterpret_cast<void *>(uintptr_t(XbeThunk + 0x20)), 0, 4);
        allowance = 64;
    };
    auto enter = [&] {
        char stage[160];
        std::snprintf(stage, sizeof stage, "XBE entry%u pc=%llx budget=%llu",
                      result.entries, (unsigned long long)state.rip,
                      (unsigned long long)allowance);
        if (log)
            log(stage);
        ControlledInvocation invocation{ context.get(), allowance };
        result.abiMask &= executeControlledGuest(invocation, thread);
        ++result.entries;
        const auto left = frame.ExecutionInstructionsRemaining;
        if (left > allowance)
            return Reason::Unsupported;
        allowance = left;
        std::snprintf(
            stage, sizeof stage,
            "XBE return%u reason=%u pc=%llx sp=%llx trap=%u signal=%u "
            "remaining=%llu abi=%llx",
            result.entries, unsigned(invocation.reason),
            (unsigned long long)state.rip, (unsigned long long)state.gregs[4],
            frame.SynchronousFaultData.TrapNo,
            frame.SynchronousFaultData.Signal, (unsigned long long)allowance,
            (unsigned long long)result.abiMask);
        if (log)
            log(stage);
        return invocation.reason;
    };
    result.passed[8] = true;
    result.passed[9] = true;
    result.passed[11] = true;
    uint64_t coldLeft = 0;
    for (unsigned warm = 0; warm < 2; ++warm) {
        prepare();
        Service service{ thread };
        const auto first = enter();
        if (warm == 0) {
            // Every bad dispatch is checked while stopped, without reentering
            // guest code or accessing unowned host memory.
            uint32_t saved[4]{};
            const auto esp = state.gregs[4];
            const bool stopped = first == Reason::GuestExit &&
                                 state.rip == XbeImportStub &&
                                 esp == InitialSP - 16;
            if (stopped) {
                std::memcpy(saved, reinterpret_cast<void *>(esp), sizeof saved);
                auto bad = [&] {
                    return !service.dispatch(first) && !service.bodies &&
                           word(XbeFill) == 0;
                };
                auto *args = reinterpret_cast<uint32_t *>(esp);
                for (unsigned i = 0; i < 4; ++i) {
                    args[i] = 0xffffffff;
                    result.passed[11] &= bad();
                    std::memcpy(args, saved, sizeof saved);
                }
                state.gregs[4] = StackBegin - 4;
                result.passed[11] &= bad();
                state.gregs[4] = 1ULL << 32;
                result.passed[11] &= bad();
                state.gregs[4] = esp;
                ++state.rip;
                result.passed[11] &= bad();
                --state.rip;
                const auto trap = frame.SynchronousFaultData.TrapNo;
                frame.SynchronousFaultData.TrapNo = 6;
                result.passed[11] &= bad();
                frame.SynchronousFaultData.TrapNo = trap;
                result.passed[11] &=
                    !service.dispatch(Reason::BudgetExhausted) &&
                    !service.bodies;
            } else
                result.passed[11] = false;
        }
        const bool called = service.dispatch(first);
        result.passed[8] &= called && service.bodies == 1 &&
                            word(XbeFill) == 0x5a5a5a5a &&
                            state.gregs[4] == InitialSP;
        // A second dispatch must never repeat a host effect, even if a caller
        // forges the previously valid stopped PC and argument frame.
        if (called && warm == 0) {
            const auto rip = state.rip, sp = state.gregs[4];
            state.rip = XbeImportStub;
            state.gregs[4] = InitialSP - 16;
            result.passed[11] &=
                !service.dispatch(first) && service.bodies == 1;
            state.rip = rip;
            state.gregs[4] = sp;
        }
        if (!called) {
            result.passed[9] = false;
            break;
        }
        const auto final = enter();
        const auto &fault = frame.SynchronousFaultData;
        const bool complete =
            final == Reason::GuestExit && state.rip == XbeCompletion &&
            state.gregs[4] == InitialSP && state.gregs[0] == 0x5a5a5a5a &&
            fault.FaultToTopAndGeneratedException && fault.Signal == SIGSEGV &&
            fault.TrapNo == 13 && !fault.err_code &&
            word(XbeThunk + 0x20) == 0x5a5a5a5a && word(XbeFill + 32) == 0 &&
            std::all_of(
                reinterpret_cast<const uint8_t *>(uintptr_t(XbeFill)),
                reinterpret_cast<const uint8_t *>(uintptr_t(XbeFill + 32)),
                [](uint8_t b) { return b == 0x5a; });
        result.passed[9] &= complete;
        if (!warm)
            coldLeft = allowance;
        else
            result.passed[10] = complete && allowance == coldLeft &&
                                allowance < 64 && allowance > 0;
    }
    prepare();
    allowance = 0;
    Service zero{ thread };
    auto reason = enter();
    result.passed[12] = reason == Reason::BudgetExhausted &&
                        state.rip == XbeEntry && !zero.dispatch(reason) &&
                        !zero.bodies && word(XbeFill) == 0 &&
                        word(XbeThunk + 0x20) == 0;
    prepare();
    allowance = 3;
    Service finite{ thread };
    reason = enter();
    result.passed[13] =
        reason == Reason::BudgetExhausted && allowance == 0 &&
        state.rip == XbeEntry + 9 && state.gregs[4] == InitialSP - 12 &&
        !frame.SynchronousFaultData.FaultToTopAndGeneratedException &&
        !finite.dispatch(reason) && !finite.bodies && word(XbeFill) == 0;
    prepare();
    const bool requested = context->RequestStop(thread);
    Service stop{ thread };
    reason = enter();
    result.passed[14] =
        requested && reason == Reason::StopRequested && state.rip == XbeEntry &&
        allowance == 64 && state.gregs[4] == InitialSP &&
        !frame.SynchronousFaultData.FaultToTopAndGeneratedException &&
        !stop.dispatch(reason) && !stop.bodies && word(XbeFill) == 0;
    context->ResetStopRequest(thread);
    FEXCore::Allocator::UninstallTLSData(thread);
    context->DestroyThread(thread);
    FEXCore::Allocator::munmap(shadow, Shadow + 2 * Page);
    if (log)
        log("XBE: engine stopped; destroying context before owned image "
            "cleanup");
    return result;
}
} // namespace armxbox
