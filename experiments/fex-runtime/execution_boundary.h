#pragma once
#include <FEXCore/Core/Context.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <cstdint>

#ifdef __aarch64__
extern "C" void armxbox_probe_execute_abi(void *, void *,
                                          void (*)(void *, void *), uint64_t *);
#endif
namespace armxbox {
// Each native entry owns host FP state and records all six ABI witnesses.
// A simulator return is not evidence about the Android ABI.
inline uint64_t executeGuest(FEXCore::Context::Context *context,
                             FEXCore::Core::InternalThreadState *thread)
{
#ifdef __aarch64__
    uint64_t checks[6]{};
    armxbox_probe_execute_abi(
        context, thread,
        [](void *ctx, void *cpu) {
            uint64_t fpcr, fpsr;
            asm volatile("mrs %0, fpcr\n mrs %1, fpsr"
                         : "=r"(fpcr), "=r"(fpsr)::"memory");
            static_cast<FEXCore::Context::Context *>(ctx)->ExecuteThread(
                static_cast<FEXCore::Core::InternalThreadState *>(cpu));
            asm volatile("msr fpcr, %0\n msr fpsr, %1" ::"r"(fpcr), "r"(fpsr)
                         : "memory");
        },
        checks);
    uint64_t mask = 0;
    for (unsigned i = 0; i < 6; ++i) {
        mask |= uint64_t(checks[i] != 0) << i;
    }
    return mask;
#else
    context->ExecuteThread(thread);
    return 0;
#endif
}
struct ControlledInvocation {
    FEXCore::Context::Context *context;
    uint64_t budget;
    FEXCore::Context::ExecutionResult reason =
        FEXCore::Context::ExecutionResult::Unsupported;
};
inline uint64_t
executeControlledGuest(ControlledInvocation &call,
                       FEXCore::Core::InternalThreadState *thread)
{
#ifdef __aarch64__
    uint64_t checks[6]{};
    armxbox_probe_execute_abi(
        &call, thread,
        [](void *opaque, void *cpu) {
            auto &call = *static_cast<ControlledInvocation *>(opaque);
            uint64_t fpcr, fpsr;
            asm volatile("mrs %0, fpcr\n mrs %1, fpsr"
                         : "=r"(fpcr), "=r"(fpsr)::"memory");
            call.reason = call.context->ExecuteControlledThread(
                static_cast<FEXCore::Core::InternalThreadState *>(cpu),
                call.budget);
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
} // namespace armxbox
