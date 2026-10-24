#include "raw_fault_probe.h"
#include "fault_policy.h"
#include <FEXCore/Core/Context.h>
#include <FEXCore/Core/X86Enums.h>
#include <FEXCore/Debug/InternalThreadState.h>
// Audited layout at the immutable engine pin, not a duplicated ABI definition.
#include "Interface/Core/CPUBackend.h"
#include <android/log.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

namespace armxbox {
namespace {
struct Event {
    fault::Decision kind{};
    uint64_t pc = 0, address = 0;
    uint32_t value = 0;
    int code = 0;
};
struct ActiveFault {
    FEXCore::Context::Context *context;
    FEXCore::Core::InternalThreadState *thread;
    fault::Descriptor expected;
    uint64_t stackAnchor;
    std::atomic<bool> pending{ false };
    Event event{};
};
thread_local std::atomic<ActiveFault *> active{ nullptr };
static_assert(decltype(active)::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic_flag installed = ATOMIC_FLAG_INIT;
struct sigaction previous{};
void forward(int signal, siginfo_t *info, void *raw)
{
    if (previous.sa_handler == SIG_IGN) {
        return;
    }
    if (previous.sa_handler == SIG_DFL) {
        if (sigaction(signal, &previous, nullptr)) {
            _exit(128 + signal);
        }
        raise(signal); // Pending until return if blocked; preserve software
                       // signals too.
        return;
    }
    if (previous.sa_flags & SA_RESETHAND) {
        struct sigaction reset{};
        reset.sa_handler = SIG_DFL;
        sigemptyset(&reset.sa_mask);
        if (sigaction(signal, &reset, nullptr)) {
            _exit(128 + signal);
        }
    }
    if (previous.sa_flags & SA_SIGINFO) {
        previous.sa_sigaction(signal, info, raw);
    } else {
        previous.sa_handler(signal);
    }
}
void onFault(int signal, siginfo_t *info, void *raw)
{
    auto *owner = active.load(std::memory_order_acquire);
    if (!owner || owner->pending.load(std::memory_order_acquire) || !info ||
        !raw) {
        forward(signal, info, raw);
        return;
    }
    auto *machine = &static_cast<ucontext_t *>(raw)->uc_mcontext;
    auto *frame = owner->thread->CurrentFrame;
    const uint64_t address = reinterpret_cast<uintptr_t>(info->si_addr);
    // Cheap ownership checks before dereferencing any engine metadata.
    if (signal != SIGSEGV ||
        (info->si_code != SEGV_ACCERR && info->si_code != SEGV_MAPERR) ||
        address < owner->expected.address ||
        address - owner->expected.address >= 4 ||
        machine->regs[28] != reinterpret_cast<uintptr_t>(frame)) {
        forward(signal, info, raw);
        return;
    }
    auto *backend = owner->thread->CPUBackend.get();
    const uint64_t headerAddress = frame->State.InlineJITBlockHeader;
    if (!headerAddress || !backend->IsAddressInCodeBuffer(headerAddress) ||
        !backend->IsAddressInCodeBuffer(
            headerAddress + sizeof(FEXCore::CPU::CPUBackend::JITCodeHeader) -
            1)) {
        forward(signal, info, raw);
        return;
    }
    const auto *header =
        reinterpret_cast<const FEXCore::CPU::CPUBackend::JITCodeHeader *>(
            headerAddress);
    const uint64_t tailAddress = headerAddress + header->OffsetToBlockTail;
    if (header->OffsetToBlockTail > 16 * 1024 * 1024 ||
        tailAddress < headerAddress ||
        !backend->IsAddressInCodeBuffer(tailAddress) ||
        !backend->IsAddressInCodeBuffer(
            tailAddress + sizeof(FEXCore::CPU::CPUBackend::JITCodeTail) - 1)) {
        forward(signal, info, raw);
        return;
    }
    const auto *tail =
        reinterpret_cast<const FEXCore::CPU::CPUBackend::JITCodeTail *>(
            tailAddress);
    if (!tail->Size || machine->pc < headerAddress ||
        machine->pc - headerAddress >= tail->Size || (machine->pc & 3)) {
        forward(signal, info, raw);
        return;
    }
    std::array<uint64_t, 31> registers{};
    for (unsigned i = 0; i < 31; ++i) {
        registers[i] = machine->regs[i];
    }
    const auto access =
        fault::decodeWord(*reinterpret_cast<const uint32_t *>(machine->pc),
                          registers, machine->sp);
    // These pinned helper paths only read the live block's metadata: no
    // allocation, locks, logging, device callbacks/service, or syscalls on the
    // handled path.
    const uint64_t guestPC =
        owner->context->RestoreRIPFromHostPC(owner->thread, machine->pc);
    fault::Snapshot snapshot{ signal,
                              info->si_code,
                              address,
                              machine->pc,
                              machine->sp,
                              machine->regs[28],
                              reinterpret_cast<uintptr_t>(frame),
                              frame->ReturningStackLocation,
                              frame->Pointers.ThreadStopHandlerSpillSRA,
                              headerAddress,
                              tail->Size,
                              guestPC,
                              access,
                              owner->stackAnchor };
    const auto decision = fault::classify(owner->expected, snapshot);
    if (decision == fault::Decision::Decline) {
        forward(signal, info, raw);
        return;
    }
    owner->event = { decision, guestPC, access.address, access.value,
                     info->si_code };
    frame->State.rip = guestPC;
    frame->SynchronousFaultData = {
        .FaultToTopAndGeneratedException = false,
        .Signal = SIGSEGV,
        .TrapNo = FEXCore::X86State::X86_TRAPNO_PF,
        .si_code = static_cast<uint8_t>(info->si_code),
        .err_code = static_cast<uint16_t>(
            4 | (info->si_code == SEGV_ACCERR ? 1 : 0) |
            (access.direction == fault::Direction::Write ? 2 : 0))
    };
    // Kernel signal return restores captured guest SRA/PSTATE, then the
    // engine's stop path spills it and returns through the normal frontend
    // FP-state guard.
    machine->sp = frame->ReturningStackLocation;
    machine->pc = frame->Pointers.ThreadStopHandlerSpillSRA;
    owner->pending.store(true, std::memory_order_release);
}
bool sameAction(const struct sigaction &a, const struct sigaction &b)
{
    return a.sa_sigaction == b.sa_sigaction && a.sa_flags == b.sa_flags &&
           std::memcmp(&a.sa_mask, &b.sa_mask, sizeof a.sa_mask) == 0;
}
class SignalScope {
public:
    bool valid = false;
    SignalScope()
    {
        if (installed.test_and_set()) {
            return;
        }
        active.store(nullptr,
                     std::memory_order_release); // Resolve this thread's TLS
                                                 // before installation.
        if (sigaction(SIGSEGV, nullptr, &previous)) {
            installed.clear();
            return;
        }
        struct sigaction action{};
        action.sa_sigaction = onFault;
        action.sa_mask = previous.sa_mask;
        action.sa_flags = SA_SIGINFO | (previous.sa_flags &
                                        (SA_ONSTACK | SA_RESTART | SA_NODEFER));
        struct sigaction actual{};
        if (sigaction(SIGSEGV, &action, &actual)) {
            installed.clear();
            return;
        }
        if (!sameAction(actual, previous)) {
            if (!sigaction(SIGSEGV, &actual, nullptr)) {
                installed.clear();
            }
            return;
        }
        valid = true;
    }
    bool close()
    {
        active.store(nullptr, std::memory_order_release);
        if (!valid) {
            return false;
        }
        struct sigaction current{};
        bool ok = !sigaction(SIGSEGV, nullptr, &current) &&
                  current.sa_sigaction == onFault;
        if (ok) {
            ok = !sigaction(SIGSEGV, &previous, nullptr);
        }
        struct sigaction restored{};
        if (ok) {
            ok = !sigaction(SIGSEGV, nullptr, &restored) &&
                 sameAction(restored, previous);
        }
        valid = false;
        if (ok) {
            installed.clear();
        }
        return ok;
    }
    ~SignalScope()
    {
        if (valid) {
            close();
        }
    }
};
} // namespace
RawFaultResults runRawFaultProbe(FEXCore::Context::Context *context,
                                 FEXCore::Core::InternalThreadState *thread,
                                 uint64_t code, uint64_t guard,
                                 void (*execute)(void *), void *opaque)
{
    RawFaultResults result;
    uint64_t stackAnchor;
    asm volatile("mov %0, sp" : "=r"(stackAnchor));
    auto &state = thread->CurrentFrame->State;
    SignalScope signals;
    if (!signals.valid) {
        snprintf(result.detail, sizeof result.detail,
                 "signal registration unavailable or concurrently changed");
        return result;
    }
    if (mprotect(reinterpret_cast<void *>(guard), 4096, PROT_NONE)) {
        snprintf(result.detail, sizeof result.detail,
                 "owned guard protection failed");
        return result;
    }
    for (unsigned which = 0; which < 4; ++which) {
        const bool write = which & 1, unmapped = which >= 2;
        if (which == 2 && munmap(reinterpret_cast<void *>(guard), 4096)) {
            snprintf(result.detail, sizeof result.detail,
                     "owned guard unmap failed");
            return result;
        }
        context->ClearCodeCache(thread);
        const uint32_t address = guard + 64;
        // ADD makes guest flags observable; MOV must fault at a non-entry guest
        // PC.
        unsigned char bytes[] = {
            0xbb, 0xff, 0xff,
            0xff, 0xff, 0x83,
            0xc3, 1,    static_cast<unsigned char>(write ? 0xa3 : 0xa1),
            0,    0,    0,
            0,    0x83, 0xc0,
            1,    0xf4
        };
        memcpy(bytes + 9, &address, sizeof address);
        memcpy(reinterpret_cast<void *>(code), bytes, sizeof bytes);
        state.gregs[0] = write ? 0xdeadbeef : 0xa5a5a5a5;
        context->SetFlagsFromCompactedEFLAGS(thread, 2);
        state.rip = code;
        ActiveFault owner{ context,
                           thread,
                           { code + 8, address,
                             write ? fault::Direction::Write :
                                     fault::Direction::Read },
                           stackAnchor };
        active.store(&owner, std::memory_order_release);
        __android_log_write(ANDROID_LOG_INFO, "ARMXBOX-FEX",
                            write ? "raw owned store probe" :
                                    "raw owned load probe");
        execute(opaque);
        active.store(nullptr, std::memory_order_release);
        const bool seen = owner.pending.load(std::memory_order_acquire);
        const auto &exception = thread->CurrentFrame->SynchronousFaultData;
        bool trapped =
            seen &&
            owner.event.kind ==
                (write ? fault::Decision::Write : fault::Decision::Read) &&
            owner.event.pc == code + 8 && state.rip == code + 8 &&
            owner.event.code == (unmapped ? SEGV_MAPERR : SEGV_ACCERR) &&
            state.gregs[0] == (write ? 0xdeadbeefU : 0xa5a5a5a5U) &&
            state.gregs[3] == 0 &&
            (context->ReconstructCompactedEFLAGS(thread, false, nullptr, 0) &
             0x8d5) == 0x55 &&
            !exception.FaultToTopAndGeneratedException &&
            exception.TrapNo == 14 &&
            exception.err_code ==
                (4U | (unmapped ? 0U : 1U) | (write ? 2U : 0U));
        result.passed[which * 2] = trapped;
        if (!trapped) {
            const auto used = strlen(result.detail);
            snprintf(
                result.detail + used, sizeof result.detail - used,
                "raw case=%u seen=%u kind=%u statePC=%llx eventPC=%llx "
                "expected=%llx eax=%llx ebx=%llx flags=%x code=%d trap=%u "
                "err=%u; ",
                which, seen, unsigned(owner.event.kind),
                (unsigned long long)state.rip,
                (unsigned long long)owner.event.pc,
                (unsigned long long)(code + 8),
                (unsigned long long)state.gregs[0],
                (unsigned long long)state.gregs[3],
                context->ReconstructCompactedEFLAGS(thread, false, nullptr, 0),
                owner.event.code, unsigned(exception.TrapNo),
                unsigned(exception.err_code));
            continue;
        }
        // Typed mock bus service runs outside the signal handler. Only this
        // known scalar MOV is emulated; no claim of arbitrary instruction/MMIO
        // support.
        struct MockBus {
            unsigned calls = 0;
            uint64_t address = 0;
            uint32_t value = 0;
        } mock;
        const uint32_t readValue = 0x1234abcd;
        const fault::WordBus bus{ &mock,
                                  [](void *p, uint64_t address) -> uint32_t {
                                      auto &m = *static_cast<MockBus *>(p);
                                      ++m.calls;
                                      m.address = address;
                                      return 0x1234abcd;
                                  },
                                  [](void *p, uint64_t address,
                                     uint32_t value) {
                                      auto &m = *static_cast<MockBus *>(p);
                                      ++m.calls;
                                      m.address = address;
                                      m.value = value;
                                  } };
        fault::Transaction transaction{ owner.expected, owner.event.kind,
                                        owner.event.pc, owner.event.address,
                                        owner.event.value };
        uint32_t loaded = 0;
        bool valueOK =
            owner.pending.exchange(false, std::memory_order_acq_rel) &&
            fault::service(transaction, bus, loaded);
        valueOK &=
            mock.address == address && (!write || mock.value == 0xdeadbeef);
        if (!write) {
            state.gregs[0] = loaded;
        }
        if (!valueOK) {
            continue;
        }
        state.rip = code + 13;
        execute(opaque);
        result.passed[which * 2 + 1] =
            valueOK && mock.calls == 1 && state.rip == code + 16 &&
            state.gregs[0] == (write ? 0xdeadbef0U : readValue + 1) &&
            thread->CurrentFrame->SynchronousFaultData
                .FaultToTopAndGeneratedException;
    }
    // A memory operand must fault before divide-error checks consume it.
    void *remapped = mmap(reinterpret_cast<void *>(guard), 4096, PROT_NONE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (remapped != MAP_FAILED &&
        reinterpret_cast<uintptr_t>(remapped) == guard) {
        for (unsigned sign = 0; sign < 2; ++sign) {
            context->ClearCodeCache(thread);
            const uint32_t address = guard + 64;
            unsigned char bytes[] = {
                0xbb, 0xff,
                0xff, 0xff,
                0xff, 0x83,
                0xc3, 1,
                0xf7, static_cast<unsigned char>(sign ? 0x3d : 0x35),
                0,    0,
                0,    0,
                0xf4
            };
            memcpy(bytes + 10, &address, sizeof address);
            memcpy(reinterpret_cast<void *>(code), bytes, sizeof bytes);
            state.gregs[0] = 17;
            state.gregs[2] = 0;
            context->SetFlagsFromCompactedEFLAGS(thread, 2);
            state.rip = code;
            ActiveFault owner{ context,
                               thread,
                               { code + 8, address, fault::Direction::Read },
                               stackAnchor };
            active.store(&owner, std::memory_order_release);
            execute(opaque);
            active.store(nullptr, std::memory_order_release);
            const auto &exception = thread->CurrentFrame->SynchronousFaultData;
            result.passed[8 + sign] =
                owner.pending.load(std::memory_order_acquire) &&
                owner.event.kind == fault::Decision::Read &&
                state.rip == code + 8 && state.gregs[0] == 17 &&
                state.gregs[2] == 0 &&
                !exception.FaultToTopAndGeneratedException &&
                exception.TrapNo == 14 && exception.err_code == 5 &&
                (context->ReconstructCompactedEFLAGS(thread, false, nullptr,
                                                     0) &
                 0x8d5) == 0x55;
            if (!result.passed[8 + sign]) {
                const auto used = strlen(result.detail);
                snprintf(result.detail + used, sizeof result.detail - used,
                         "div sign=%u seen=%u kind=%u statePC=%llx "
                         "eventPC=%llx flags=%x trap=%u err=%u; ",
                         sign, owner.pending.load(), unsigned(owner.event.kind),
                         (unsigned long long)state.rip,
                         (unsigned long long)owner.event.pc,
                         context->ReconstructCompactedEFLAGS(thread, false,
                                                             nullptr, 0),
                         unsigned(exception.TrapNo),
                         unsigned(exception.err_code));
            }
        }
    } else if (remapped != MAP_FAILED) {
        munmap(remapped, 4096);
    }
    result.passed[10] = signals.close();
    return result;
}
} // namespace armxbox
