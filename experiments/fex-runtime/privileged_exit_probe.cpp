#include "privileged_exit_probe.h"
#include "execution_boundary.h"
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>

namespace armxbox {
namespace {
struct ExitCase {
    const char *label;
    std::array<uint8_t, 3> bytes;
    unsigned size;
    uint8_t signal, trap;
};
// These are controlled userspace rejections, not Xbox ring-0 virtualization.
constexpr ExitCase Cases[] = {
    { "CLI controlled GP preserves state and stops before sentinel",
      { 0xfa },
      1,
      SIGSEGV,
      13 },
    { "STI controlled GP preserves state and stops before sentinel",
      { 0xfb },
      1,
      SIGSEGV,
      13 },
    { "MOV from CR0 controlled GP preserves state",
      { 0x0f, 0x20, 0xc0 },
      3,
      SIGSEGV,
      13 },
    { "MOV to CR0 controlled GP preserves state",
      { 0x0f, 0x22, 0xc0 },
      3,
      SIGSEGV,
      13 },
    { "MOV from DR0 controlled GP preserves state",
      { 0x0f, 0x21, 0xc0 },
      3,
      SIGSEGV,
      13 },
    { "MOV to DR0 controlled GP preserves state",
      { 0x0f, 0x23, 0xc0 },
      3,
      SIGSEGV,
      13 },
    { "RDMSR controlled GP preserves state", { 0x0f, 0x32 }, 2, SIGSEGV, 13 },
    { "WRMSR controlled GP preserves state", { 0x0f, 0x30 }, 2, SIGSEGV, 13 },
    { "RDPMC controlled GP preserves state", { 0x0f, 0x33 }, 2, SIGSEGV, 13 },
    { "IN immediate controlled GP preserves state",
      { 0xe4, 0x80 },
      2,
      SIGSEGV,
      13 },
    { "OUT immediate controlled GP preserves state",
      { 0xe6, 0x80 },
      2,
      SIGSEGV,
      13 },
    { "IN DX controlled GP preserves state", { 0xec }, 1, SIGSEGV, 13 },
    { "OUT DX controlled GP preserves state", { 0xee }, 1, SIGSEGV, 13 },
    { "INSB controlled GP precedes inaccessible destination",
      { 0x6c },
      1,
      SIGSEGV,
      13 },
    { "OUTSB controlled GP precedes inaccessible source",
      { 0x6e },
      1,
      SIGSEGV,
      13 },
    { "SYSENTER controlled UD preserves state", { 0x0f, 0x34 }, 2, SIGILL, 6 },
    { "SYSEXIT controlled UD preserves state", { 0x0f, 0x35 }, 2, SIGILL, 6 },
    { "UD2 preserves arithmetic-produced flags and stops before sentinel",
      { 0x0f, 0x0b },
      2,
      SIGILL,
      6 },
    { "HLT preserves arithmetic-produced flags and stops before sentinel",
      { 0xf4 },
      1,
      SIGSEGV,
      13 },
};
static_assert(std::size(Cases) == PrivilegedExitCount);
constexpr uint32_t ArithmeticFlags = 0x8d5;
} // namespace
const char *privilegedExitLabel(unsigned index)
{
    return Cases[index].label;
}
PrivilegedExitResults
runPrivilegedExitProbe(FEXCore::Context::Context *context,
                       FEXCore::Core::InternalThreadState *thread,
                       uintptr_t code, uintptr_t data, uintptr_t stack,
                       uintptr_t guard)
{
    PrivilegedExitResults result;
    result.passed.fill(true);
    if (mprotect(reinterpret_cast<void *>(guard), 4096, PROT_NONE)) {
        std::snprintf(result.detail, sizeof result.detail,
                      "guard protection failed");
        result.passed.fill(false);
        return result;
    }
    auto &state = thread->CurrentFrame->State;
    auto &pointers = thread->CurrentFrame->Pointers;
    const auto oldILL = pointers.GuestSignal_SIGILL;
    const auto oldSEGV = pointers.GuestSignal_SIGSEGV;
    pointers.GuestSignal_SIGILL = pointers.ThreadStopHandlerSpillSRA;
    pointers.GuestSignal_SIGSEGV = pointers.ThreadStopHandlerSpillSRA;
    // All arithmetic/DF combinations must round-trip as architectural EFLAGS,
    // even if callers supply reserved high bits. This also covers all NZCV
    // patterns without deriving expected values from the API under test.
    constexpr unsigned FlagBits[] = { 0, 2, 4, 6, 7, 10, 11 };
    for (unsigned seed = 0; seed < 128; ++seed) {
        // Also alternate TF/IOPL/NT/RF/VM/AC/VIF/VIP/ID without executing
        // with those control bits set; the subsequent fixtures reset them.
        uint32_t flags = 0x202 | ((seed & 1U) ? 0x3f7100 : 0);
        for (unsigned bit = 0; bit < std::size(FlagBits); ++bit) {
            flags |= ((seed >> bit) & 1U) << FlagBits[bit];
        }
        for (uint32_t high : { 0U, 0xffc00000U }) {
            context->SetFlagsFromCompactedEFLAGS(thread, flags | high);
            result.canonicalFlags &= context->ReconstructCompactedEFLAGS(
                                         thread, false, nullptr, 0) == flags;
        }
    }
    unsigned failures = 0;
    for (unsigned i = 0; i < std::size(Cases); ++i) {
        const auto &test = Cases[i];
        for (unsigned scenario = 0; scenario < 4; ++scenario) {
            context->ClearCodeCache(thread);
            auto *memory = reinterpret_cast<uint8_t *>(code);
            unsigned size = 0;
            auto emit = [&](uint8_t byte) { memory[size++] = byte; };
            auto imm = [&](uint32_t word) {
                std::memcpy(memory + size, &word, sizeof word);
                size += sizeof word;
            };
            if (scenario < 3) {
                emit(0xb8);
                imm(scenario == 0 ? 0xffffffffU :
                    scenario == 1 ? 0x7fffffffU :
                                    1U);
                emit(0x83);
                emit(scenario == 2 ? 0xe8 : 0xc0);
                emit(1);
            }
            const auto faultPC = code + size;
            std::memcpy(memory + size, test.bytes.data(), test.size);
            size += test.size;
            // Neither following memory nor GPR sentinel may execute.
            emit(0xc7);
            emit(0x05);
            imm(data + 0x2000);
            imm(0xbad0bad0);
            emit(0xbb);
            imm(0xbad1bad1);
            emit(0xf4);
            for (unsigned warm = 0; warm < 2; ++warm) {
                std::array<uint64_t, 8> expected{ 0x31415926, 0x10,  0x80,
                                                  0x27182818, stack, 0x12345678,
                                                  guard,      guard };
                for (unsigned reg = 0; reg < expected.size(); ++reg) {
                    state.gregs[reg] = expected[reg];
                }
                auto *sentinel = reinterpret_cast<uint32_t *>(data + 0x2000);
                *sentinel = 0xc001cafe;
                context->SetFlagsFromCompactedEFLAGS(thread, 0xed7);
                uint32_t expectedFlags = context->ReconstructCompactedEFLAGS(
                    thread, false, nullptr, 0);
                result.canonicalFlags &= (expectedFlags & 0xffc00000U) == 0;
                if (scenario < 3) {
                    expected[0] = scenario == 1 ? 0x80000000U : 0;
                    const uint32_t arithmetic = scenario == 0 ? 0x55 :
                                                scenario == 1 ? 0x894 :
                                                                0x44;
                    expectedFlags =
                        (expectedFlags & ~ArithmeticFlags) | arithmetic;
                }
                thread->CurrentFrame->SynchronousFaultData = {};
                state.rip = code;
                result.abiMask &= executeGuest(context, thread);
                ++result.entries;
                const auto &fault = thread->CurrentFrame->SynchronousFaultData;
                const auto flags = context->ReconstructCompactedEFLAGS(
                    thread, false, nullptr, 0);
                result.canonicalFlags &= (flags & 0xffc00000U) == 0;
                bool ok = fault.FaultToTopAndGeneratedException &&
                          fault.Signal == test.signal &&
                          fault.TrapNo == test.trap && fault.err_code == 0 &&
                          fault.si_code == (test.signal == SIGILL ? 2 : 0x80) &&
                          state.rip == faultPC && flags == expectedFlags &&
                          *sentinel == 0xc001cafe;
                int badReg = -1;
                for (unsigned reg = 0; reg < expected.size(); ++reg) {
                    if (state.gregs[reg] != expected[reg]) {
                        ok = false;
                        if (badReg < 0)
                            badReg = reg;
                    }
                }
                result.passed[i] &= ok;
                if (!ok && failures++ < 3) {
                    const auto used = std::strlen(result.detail);
                    std::snprintf(
                        result.detail + used, sizeof result.detail - used,
                        "case%u scenario%u warm%u pc=%llx expected=%llx "
                        "flags=%x expected=%x signal=%u trap=%u; ",
                        i, scenario, warm, (unsigned long long)state.rip,
                        (unsigned long long)faultPC, flags, expectedFlags,
                        fault.Signal, fault.TrapNo);
                    const auto extra = std::strlen(result.detail);
                    std::snprintf(
                        result.detail + extra, sizeof result.detail - extra,
                        "si=%u reg=%d actual=%llx expected=%llx sentinel=%x; ",
                        fault.si_code, badReg,
                        (unsigned long long)(badReg < 0 ? 0 :
                                                          state.gregs[badReg]),
                        (unsigned long long)(badReg < 0 ? 0 : expected[badReg]),
                        *sentinel);
                }
            }
        }
    }
    pointers.GuestSignal_SIGILL = oldILL;
    pointers.GuestSignal_SIGSEGV = oldSEGV;
    if (mprotect(reinterpret_cast<void *>(guard), 4096,
                 PROT_READ | PROT_WRITE)) {
        result.passed.fill(false);
        std::snprintf(result.detail, sizeof result.detail,
                      "guard restoration failed");
    }
    return result;
}
} // namespace armxbox
