#include "instruction_policy_probe.h"
#include "execution_boundary.h"
#include "reference/xbox_instruction_cases.h"
#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/HostFeatures.h>
#include <FEXCore/Core/SignalDelegator.h>
#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/Utils/AllocatorHooks.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>

namespace armxbox {
namespace {
using Context = FEXCore::Context::Context;
using Policy = FEXCore::Context::InstructionPolicy;
using Identity = FEXCore::CPUID::IdentityProfile;
using Reason = FEXCore::Context::ExecutionResult;
using Thread = FEXCore::Core::InternalThreadState;
constexpr const char *Labels[] = {
    "instruction policy rejects unknown policy value",
    "instruction policy rejects 64-bit guest mode independently of identity",
    "instruction policy rejects shared disk cache independently of identity",
    "instruction policy rejects experimental cache independently of identity",
    "instruction policy rejects cache validation independently of identity",
    "instruction policy rechecks incompatible cache at core initialization",
    "reference policy retains SSE1 XORPS",
    "reference policy retains MMX PXOR",
    "reference policy retains x87 FLD1",
    "reference policy retains observed SSE2 PXOR compatibility ambiguity",
    "reference policy rejects captured SSE3 HADDPS with exact state",
    "reference policy rejects captured SSSE3 PSHUFB with exact state",
    "reference policy rejects captured SSE4.1 PTEST with exact state",
    "reference policy rejects captured SSE4.2 PCMPGTQ with exact state",
    "reference policy rejects captured AVX VZEROUPPER with exact state",
    "reference policy rejects captured RDTSCP with exact state",
    "reference policy rejects captured XGETBV with exact state",
    "reference policy retains precise explicit UD2 rejection",
    "policy SIMD rejection precedes inaccessible memory operand",
    "default contexts retain modern operations and unsupported AVX behavior",
    "instruction policy and CPUID identity remain independent",
    "policy code invalidation observes replacement with allowed instruction",
    "admission budget boundary precedes policy rejection",
    "latched stop boundary precedes policy rejection",
    "policy rejection preserves SIMD MMX x87 status and floating controls",
    "reference policy rejects precisely without instruction checkpoints",
    "AVX128 VZERO dispatch isolates rejection policy from default contexts",
};
static_assert(std::size(Labels) == InstructionPolicyCheckCount);
static_assert(std::size(XboxInstructionCases) == 12);
} // namespace
const char *instructionPolicyCheckLabel(unsigned i)
{
    return Labels[i];
}
InstructionPolicyResults runInstructionPolicyProbe(
    const FEXCore::HostFeatures &features, FEXCore::HLE::SyscallHandler *calls,
    uintptr_t code, uintptr_t data, uintptr_t stack, void (*log)(const char *))
{
    InstructionPolicyResults result;
    result.passed[0] = !Context::CreateNewContext(features, Identity::Default,
                                                  false, Policy(0xffffffff));
    unsigned optionIndex = 1;
    for (auto option : { FEXCore::Config::CONFIG_IS64BIT_MODE,
                         FEXCore::Config::CONFIG_DISKCACHE,
                         FEXCore::Config::CONFIG_ENABLECODECACHINGWIP,
                         FEXCore::Config::CONFIG_ENABLECODECACHEVALIDATION }) {
        FEXCore::Config::Set(option, "1");
        result.passed[optionIndex++] = !Context::CreateNewContext(
            features, Identity::Default, false, Policy::XboxReferenceSubset);
        FEXCore::Config::Set(option, "0");
    }
    {
        auto context = Context::CreateNewContext(
            features, Identity::Default, false, Policy::XboxReferenceSubset);
        result.passed[20] =
            context && (context->RunCPUIDFunction(1, 0).edx & (1U << 26)) &&
            context->RunCPUIDFunction(0, 0).eax > 2;
        FEXCore::Config::Set(FEXCore::Config::CONFIG_DISKCACHE, "1");
        result.passed[5] = context && !context->InitCore();
        FEXCore::Config::Set(FEXCore::Config::CONFIG_DISKCACHE, "0");
    }
    const uintptr_t guard = code + 0xe000;
    if (mprotect(reinterpret_cast<void *>(guard), 4096, PROT_NONE))
        return result;
    result.passed[19] = true;
    result.passed[24] = true;
    for (unsigned pass = 0; pass < 6; ++pass) {
        const bool restricted = pass == 1 || pass == 3 || pass == 5;
        auto contextFeatures = features;
        if (pass >= 4) {
            // FEX supports AVX128 through baseline NEON, without SVE256.
            contextFeatures.SupportsAVX = true;
            contextFeatures.SupportsSVE256 = false;
        }
        const bool checkpoints = pass != 3;
        FEXCore::SignalDelegator signals;
        auto context = Context::CreateNewContext(
            contextFeatures, Identity::XboxReference, checkpoints,
            restricted ? Policy::XboxReferenceSubset : Policy::Default);
        if (!context) {
            result.passed[19] = false;
            break;
        }
        context->SetSignalDelegator(&signals);
        context->SetSyscallHandler(calls);
        if (!context->InitCore()) {
            result.passed[19] = false;
            break;
        }
        result.passed[20] &= context->RunCPUIDFunction(1, 0).eax == 0x68a;
        auto *thread = context->CreateThread(nullptr);
        FEXCore::Allocator::RegisterTLSData(thread);
        auto &state = thread->CurrentFrame->State;
        FEXCore::Core::CPUState::gdt_segment gdt[32]{};
        gdt[1].D = 1;
        state.segment_arrays[0] = gdt;
        state.cs_idx = 8;
        constexpr size_t ShadowSize = Thread::CALLRET_STACK_SIZE;
        void *shadow = FEXCore::Allocator::mmap(
            nullptr, ShadowSize, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (shadow == MAP_FAILED) {
            result.passed[19] = false;
            FEXCore::Allocator::UninstallTLSData(thread);
            context->DestroyThread(thread);
            break;
        }
        thread->CallRetStackBase = shadow;
        state.callret_sp = reinterpret_cast<uintptr_t>(shadow) + ShadowSize / 4;
        auto &frame = *thread->CurrentFrame;
        frame.Pointers.GuestSignal_SIGILL =
            frame.Pointers.ThreadStopHandlerSpillSRA;
        frame.Pointers.GuestSignal_SIGTRAP =
            frame.Pointers.ThreadStopHandlerSpillSRA;
        frame.Pointers.GuestSignal_SIGSEGV =
            frame.Pointers.ThreadStopHandlerSpillSRA;
        auto enter = [&](uint64_t budget) {
            char stage[128];
            std::snprintf(
                stage, sizeof stage,
                "instruction policy entry%u policy%u pc=%llx budget=%llu",
                result.entries, restricted, (unsigned long long)state.rip,
                (unsigned long long)budget);
            if (log)
                log(stage);
            ControlledInvocation call{ context.get(), budget };
            if (checkpoints) {
                result.abiMask &= executeControlledGuest(call, thread);
            } else {
                result.abiMask &= executeGuest(context.get(), thread);
                call.reason = Reason::GuestExit;
            }
            ++result.entries;
            return call.reason;
        };
        auto fpu = [&]() {
            std::array<uint8_t, sizeof state.xmm + sizeof state.mm + 16 + 7>
                bytes{};
            auto *next = bytes.data();
            auto append = [&](const void *p, size_t size) {
                std::memcpy(next, p, size);
                next += size;
            };
            append(&state.xmm, sizeof state.xmm);
            append(state.mm, sizeof state.mm);
            append(state.flags + 32, 16);
            append(&state.mxcsr, 4);
            append(&state.FCW, 2);
            append(&state.AbridgedFTW, 1);
            return bytes;
        };
        auto runCase = [&](const InstructionCase &test, unsigned scenario,
                           bool warm, bool modern) {
            auto *memory = reinterpret_cast<uint8_t *>(code);
            unsigned size = 0;
            auto emit = [&](uint8_t byte) { memory[size++] = byte; };
            auto imm = [&](uint32_t value) {
                std::memcpy(memory + size, &value, 4);
                size += 4;
            };
            if (!warm) {
                context->ClearCodeCache(thread);
                emit(0xb8);
                imm(scenario == 0 ? 0xffffffffU :
                    scenario == 1 ? 0x7fffffffU :
                                    1U);
                emit(0x83);
                emit(scenario == 2 ? 0xe8 : 0xc0);
                emit(1);
                std::memcpy(memory + size, test.bytes.data(), test.size);
                size += test.size;
                emit(0xc7);
                emit(0x05);
                imm(data + 0x2000);
                imm(0xfeedface);
                emit(0xf4);
            } else
                size = 8 + test.size + 10 + 1;
            const auto faultPC = code + 8;
            const auto haltPC = code + 8 + test.size + 10;
            std::array<uint64_t, 8> expected{ 0x12345678, 0,     0x55667788,
                                              0x22334455, stack, 0x66666666,
                                              guard,      guard };
            std::memcpy(state.gregs, expected.data(), sizeof expected);
            state.rip = code;
            state.mxcsr = 0x1f80;
            state.FCW = 0x77f;
            state.AbridgedFTW = 0;
            std::memset(state.flags + 32, 0, 16);
            state.flags[40] = 1;
            state.flags[43] = 3;
            std::memset(&state.xmm, 0xa5, sizeof state.xmm);
            std::memset(state.mm, 0, sizeof state.mm);
            context->SetFlagsFromCompactedEFLAGS(thread, 0xed7);
            auto *sentinel = reinterpret_cast<uint32_t *>(data + 0x2000);
            *sentinel = 0;
            const auto before = fpu();
            const auto reason = enter(64);
            const bool rejected = !modern && test.trap == 6;
            // The conservative host-feature fixture has no VEX dispatch table.
            // Default mode must retain its original SIGILL/trap0 decode result.
            const bool decodeOnly =
                modern && !contextFeatures.SupportsAVX &&
                std::strcmp(test.name, "avx_vzeroupper") == 0;
            const bool noSentinel = rejected || decodeOnly;
            expected[0] = scenario == 1 ? 0x80000000U : 0;
            const uint32_t flags = 0x602 | (scenario == 0 ? 0x55 :
                                            scenario == 1 ? 0x894 :
                                                            0x44);
            const auto &fault = frame.SynchronousFaultData;
            bool ok =
                reason == Reason::GuestExit &&
                fault.FaultToTopAndGeneratedException &&
                fault.Signal == (noSentinel ? SIGILL : SIGSEGV) &&
                fault.TrapNo == (decodeOnly ? 0 :
                                 rejected   ? 6 :
                                              13) &&
                fault.err_code == 0 &&
                fault.si_code == (decodeOnly ? 0 :
                                  rejected   ? 2 :
                                               0x80) &&
                state.rip == (noSentinel ? faultPC : haltPC) &&
                *sentinel == (noSentinel ? 0 : 0xfeedface) &&
                !std::memcmp(state.gregs, expected.data(), sizeof expected) &&
                (modern || context->ReconstructCompactedEFLAGS(
                               thread, false, nullptr, 0) == flags);
            if (rejected)
                result.passed[24] &= fpu() == before;
            if (!ok && !result.detail[0]) {
                std::snprintf(
                    result.detail, sizeof result.detail,
                    "case=%s policy=%u scenario=%u warm=%u pc=%llx trap=%u "
                    "signal=%u flags=%x sentinel=%x entries=%u",
                    test.name, restricted, scenario, warm,
                    (unsigned long long)state.rip, fault.TrapNo, fault.Signal,
                    context->ReconstructCompactedEFLAGS(thread, false, nullptr,
                                                        0),
                    *sentinel, result.entries);
            }
            return ok;
        };
        if (pass >= 4) {
            if (pass == 4)
                result.passed[26] = true;
            for (bool warm : { false, true })
                result.passed[26] &=
                    runCase(XboxInstructionCases[8], 0, warm, !restricted);
        }
        if (!checkpoints) {
            result.passed[25] = true;
            for (unsigned index : { 4U, 8U })
                for (bool warm : { false, true })
                    result.passed[25] &=
                        runCase(XboxInstructionCases[index], 0, warm, false);
        }
        for (unsigned i = 0; pass < 3 && i < std::size(XboxInstructionCases);
             ++i) {
            const auto &test = XboxInstructionCases[i];
            if (!restricted && (i < 4 || i > 8))
                continue;
            if (restricted)
                result.passed[6 + i] = true;
            for (unsigned scenario = 0; scenario < (restricted ? 3U : 1U);
                 ++scenario) {
                for (bool warm : { false, true }) {
                    const bool ok = runCase(test, scenario, warm, !restricted);
                    if (restricted)
                        result.passed[6 + i] &= ok;
                    else
                        result.passed[19] &= ok;
                }
            }
        }
        if (pass == 1) {
            const bool guardAdmitted = result.passed[10] && result.passed[11] &&
                                       result.passed[12] && result.passed[13];
            result.passed[18] = guardAdmitted;
            const InstructionCase memoryCases[] = {
                { "HADDPS guard", { 0xf2, 0x0f, 0x7c, 0x06 }, 4, 6 },
                { "PSHUFB guard", { 0x66, 0x0f, 0x38, 0x00, 0x06 }, 5, 6 },
                { "PTEST guard", { 0x66, 0x0f, 0x38, 0x17, 0x06 }, 5, 6 },
                { "PCMPGTQ guard", { 0x66, 0x0f, 0x38, 0x37, 0x06 }, 5, 6 }
            };
            // Never execute inaccessible operands if the finite register-form
            // rejection controls fail (including deliberate negative controls).
            if (guardAdmitted) {
                for (const auto &test : memoryCases)
                    for (bool warm : { false, true })
                        result.passed[18] &= runCase(test, 0, warm, false);
            }
            // Compile rejection then replace the same address and invalidate
            // it.
            const InstructionCase allowed{
                "replacement XORPS", { 0x0f, 0x57, 0xc0 }, 3, 0
            };
            result.passed[21] = runCase(allowed, 0, false, false);
            auto boundary = [&](bool stop) {
                context->ClearCodeCache(thread);
                const auto &test = XboxInstructionCases[4];
                std::memcpy(reinterpret_cast<void *>(code), test.bytes.data(),
                            test.size);
                state.rip = code;
                context->SetFlagsFromCompactedEFLAGS(thread, 0xed7);
                const auto before = fpu();
                const bool request = !stop || context->RequestStop(thread);
                const auto reason = enter(stop ? 10 : 0);
                const bool noFault =
                    !frame.SynchronousFaultData.FaultToTopAndGeneratedException;
                const bool first = request &&
                                   reason == (stop ? Reason::StopRequested :
                                                     Reason::BudgetExhausted) &&
                                   state.rip == code && noFault &&
                                   before == fpu();
                const bool reset = !stop || context->ResetStopRequest(thread);
                const auto rejected = enter(1);
                return first && reset && rejected == Reason::GuestExit &&
                       state.rip == code &&
                       frame.SynchronousFaultData.TrapNo == 6 &&
                       frame.SynchronousFaultData.Signal == SIGILL &&
                       before == fpu();
            };
            result.passed[22] = boundary(false);
            result.passed[23] = boundary(true);
        }
        FEXCore::Allocator::UninstallTLSData(thread);
        context->DestroyThread(thread);
        FEXCore::Allocator::munmap(shadow, ShadowSize);
    }
    if (mprotect(reinterpret_cast<void *>(guard), 4096, PROT_READ | PROT_WRITE))
        result.passed[18] = false;
    return result;
}
} // namespace armxbox
