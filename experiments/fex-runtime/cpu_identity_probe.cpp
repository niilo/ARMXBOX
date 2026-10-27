#include "cpu_identity_probe.h"
#include "execution_boundary.h"
#include "reference/xbox_cpuid_cases.h"
#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/HostFeatures.h>
#include <FEXCore/Core/SignalDelegator.h>
#include <FEXCore/Utils/AllocatorHooks.h>
#include <FEXCore/Utils/Allocator.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>

namespace armxbox {
CPUIdentityResults runCPUIdentityProbe(const FEXCore::HostFeatures &features,
                                       FEXCore::HLE::SyscallHandler *calls,
                                       uintptr_t code, uintptr_t data,
                                       uintptr_t stack)
{
    using Context = FEXCore::Context::Context;
    using Profile = FEXCore::CPUID::IdentityProfile;
    CPUIdentityResults result;
    result.passed.fill(true);
    result.passed[0] =
        !Context::CreateNewContext(features, static_cast<Profile>(255));
    const FEXCore::Config::ConfigOption invalidOptions[] = {
        FEXCore::Config::CONFIG_IS64BIT_MODE,
        FEXCore::Config::CONFIG_DISKCACHE,
        FEXCore::Config::CONFIG_ENABLECODECACHINGWIP,
        FEXCore::Config::CONFIG_ENABLECODECACHEVALIDATION,
    };
    for (unsigned i = 0; i < std::size(invalidOptions); ++i) {
        FEXCore::Config::Set(invalidOptions[i], "1");
        result.passed[i + 1] =
            !Context::CreateNewContext(features, Profile::XboxReference);
        FEXCore::Config::Set(invalidOptions[i], "0");
    }
    // Run identical guest addresses across default -> Xbox -> default contexts.
    // No mutable profile or shared disk-cache key is admitted.
    for (unsigned phase = 0; phase < 3; ++phase) {
        const bool xbox = phase == 1;
        FEXCore::SignalDelegator signals;
        auto context = Context::CreateNewContext(
            features, xbox ? Profile::XboxReference : Profile::Default);
        if (!context) {
            result.passed.fill(false);
            std::snprintf(result.detail, sizeof result.detail,
                          "context creation failed at phase %u", phase);
            return result;
        }
        context->SetSignalDelegator(&signals);
        context->SetSyscallHandler(calls);
        if (!context->InitCore()) {
            result.passed.fill(false);
            std::snprintf(result.detail, sizeof result.detail,
                          "InitCore failed at phase %u", phase);
            return result;
        }
        auto *thread = context->CreateThread(nullptr);
        FEXCore::Allocator::RegisterTLSData(thread);
        auto &state = thread->CurrentFrame->State;
        FEXCore::Core::CPUState::gdt_segment gdt[32]{};
        gdt[1].D = 1;
        state.segment_arrays[0] = gdt;
        state.cs_idx = 8;
        constexpr size_t shadowSize =
            FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE;
        void *shadow = FEXCore::Allocator::mmap(
            nullptr, shadowSize, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (shadow == MAP_FAILED) {
            FEXCore::Allocator::UninstallTLSData(thread);
            context->DestroyThread(thread);
            result.passed.fill(false);
            std::snprintf(result.detail, sizeof result.detail,
                          "shadow stack allocation failed");
            return result;
        }
        thread->CallRetStackBase = shadow;
        state.callret_sp = reinterpret_cast<uintptr_t>(shadow) + shadowSize / 4;
        state.mxcsr = 0x1f80;
        thread->CurrentFrame->Pointers.GuestSignal_SIGSEGV =
            thread->CurrentFrame->Pointers.ThreadStopHandlerSpillSRA;
        auto words = [](const FEXCore::CPUID::FunctionResults &value) {
            return std::array<uint32_t, 4>{ value.eax, value.ebx, value.ecx,
                                            value.edx };
        };
        auto run = [&](const CPUIDCase &test, bool dynamic) {
            auto *memory = reinterpret_cast<uint8_t *>(code);
            unsigned size = 0;
            auto emit = [&](uint8_t byte) { memory[size++] = byte; };
            auto imm = [&](uint32_t word) {
                std::memcpy(memory + size, &word, sizeof word);
                size += sizeof word;
            };
            if (dynamic) {
                // Runtime loads ensure the CPUID handler is exercised too.
                emit(0xa1);
                imm(data);
                emit(0x8b);
                emit(0x0d);
                imm(data + 4);
                std::memcpy(reinterpret_cast<void *>(data), &test.eax, 4);
                std::memcpy(reinterpret_cast<void *>(data + 4), &test.ecx, 4);
            } else {
                // Put EAX last so post-RA constant folding can see it.
                emit(0xb9);
                imm(test.ecx);
                emit(0xb8);
                imm(test.eax);
            }
            emit(0x0f);
            emit(0xa2);
            const unsigned stopOffset = size;
            emit(0xf4);
            context->ClearCodeCache(thread);
            state.gregs[4] = stack;
            state.gregs[5] = 0x12345678;
            state.gregs[6] = 0x23456789;
            state.gregs[7] = 0x3456789a;
            context->SetFlagsFromCompactedEFLAGS(thread, 0xed7);
            const auto flags =
                context->ReconstructCompactedEFLAGS(thread, false, nullptr, 0);
            thread->CurrentFrame->SynchronousFaultData = {};
            state.rip = code;
            result.abiMask &= executeGuest(context.get(), thread);
            ++result.nativeEntries;
            const auto &fault = thread->CurrentFrame->SynchronousFaultData;
            const bool clean =
                state.gregs[4] == stack && state.gregs[5] == 0x12345678 &&
                state.gregs[6] == 0x23456789 && state.gregs[7] == 0x3456789a &&
                context->ReconstructCompactedEFLAGS(thread, false, nullptr,
                                                    0) == flags &&
                fault.FaultToTopAndGeneratedException &&
                fault.Signal == SIGSEGV && fault.TrapNo == 13 &&
                fault.err_code == 0 && state.rip == code + stopOffset;
            result.passed[9] &= clean;
            return std::array<uint32_t, 4>{ uint32_t(state.gregs[0]),
                                            uint32_t(state.gregs[3]),
                                            uint32_t(state.gregs[1]),
                                            uint32_t(state.gregs[2]) };
        };
        if (xbox) {
            for (const auto &test : XboxCPUIDCases) {
                for (unsigned repeat = 0; repeat < 2; ++repeat) {
                    result.passed[5] &=
                        words(context->RunCPUIDFunction(test.eax, test.ecx)) ==
                        test.result;
                }
                for (uint32_t cpu : { 0U, 1U, 0xffffffffU }) {
                    result.passed[6] &=
                        words(context->RunCPUIDFunctionName(
                            test.eax, test.ecx, cpu)) == test.result;
                }
                result.passed[7] &= run(test, false) == test.result;
                result.passed[8] &= run(test, true) == test.result;
            }
        }
        if (xbox) {
            result.privileged = runPrivilegedExitProbe(context.get(), thread, code,
                                                      data, stack, code + 0xe000);
        }
        const auto expected = words(context->RunCPUIDFunction(0, 0));
        const CPUIDCase first{ 0, 0, expected };
        result.passed[10] &= run(first, false) == expected;
        if (!xbox) {
            const auto identity = context->RunCPUIDFunction(1, 0);
            result.passed[11] &= (identity.edx & (1U << 26)) != 0 &&
                                 context->RunCPUIDFunction(0, 0).eax > 2;
        }
        FEXCore::Allocator::UninstallTLSData(thread);
        context->DestroyThread(thread);
        FEXCore::Allocator::munmap(shadow, shadowSize);
    }
    return result;
}
} // namespace armxbox
