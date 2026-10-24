#include "pc_metadata_probe.h"
#include "fault_policy.h"
#ifdef VIXL_SIMULATOR
#include "Interface/Context/Context.h"
#include "Interface/Core/Dispatcher/Dispatcher.h"
#endif
#include "Interface/Core/CPUBackend.h"
#include "Utils/variable_length_integer.h"
#include <FEXCore/Core/Context.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <cstdio>
#include <cstring>

namespace armxbox {
#ifdef VIXL_SIMULATOR
// Obtain protected member pointers through a derived access declaration. The
// actual object remains the engine dispatcher; no layout copy or invalid cast.
struct SimulatorAccess : FEXCore::CPU::Arm64Emitter {
    static auto simulator()
    {
        return &SimulatorAccess::Simulator;
    }
    static auto decoder()
    {
        return &SimulatorAccess::SimDecoder;
    }
};
struct AccessObserver : vixl::aarch64::DecoderVisitor {
    FEXCore::Context::Context *context;
    FEXCore::Core::InternalThreadState *thread;
    vixl::aarch64::Simulator &sim;
    uint64_t address;
    unsigned hits = 0;
    uint32_t flags = 0;
    AccessObserver(FEXCore::Context::Context *c,
                   FEXCore::Core::InternalThreadState *t,
                   vixl::aarch64::Simulator &s, uint64_t a)
        : context(c), thread(t), sim(s), address(a)
    {
    }
    void Visit(vixl::aarch64::Metadata *,
               const vixl::aarch64::Instruction *instruction) override
    {
        std::array<uint64_t, 31> regs{};
        for (unsigned i = 0; i < 31; ++i) {
            regs[i] = sim.ReadXRegister(i);
        }
        const auto access = fault::decodeWord(
            instruction->GetInstructionBits(), regs,
            sim.ReadXRegister(31, vixl::aarch64::Reg31IsStackPointer));
        if (!access.valid || access.address != address) {
            return;
        }
        auto &state = thread->CurrentFrame->State;
        const auto pf = state.pf_raw, af = state.af_raw;
        flags = context->ReconstructCompactedEFLAGS(
            thread, true, regs.data(), sim.ReadNzcv().GetRawValue());
        state.pf_raw = pf;
        state.af_raw = af;
        ++hits;
    }
};
#endif

PCMetadataResults runPCMetadataProbe(FEXCore::Context::Context *context,
                                     FEXCore::Core::InternalThreadState *thread,
                                     uint64_t code, uint64_t data,
                                     void (*execute)(void *), void *opaque)
{
    PCMetadataResults result;
    auto &state = thread->CurrentFrame->State;
    for (unsigned which = 0; which < result.passed.size(); ++which) {
        context->ClearCodeCache(thread);
        unsigned char bytes[20] = {
            0xbb, 0xff, 0xff, 0xff, 0xff, 0x83, 0xc3, 1
        };
        const bool moffs = which < 2;
        bytes[8] = moffs      ? (which ? 0xa3 : 0xa1) :
                   which == 2 ? 0x8b :
                   which == 3 ? 0x89 :
                                0xf7;
        if (!moffs) {
            bytes[9] = which < 4 ? 0x05 : which == 4 ? 0x35 : 0x3d;
        }
        const uint32_t address = data;
        memcpy(bytes + (moffs ? 9 : 10), &address, sizeof address);
        const unsigned length = moffs ? 13 : 14;
        bytes[length] = 0x83;
        bytes[length + 1] = 0xc0;
        bytes[length + 2] = 1;
        bytes[length + 3] = 0xf4;
        memcpy(reinterpret_cast<void *>(code), bytes, length + 4);
        *reinterpret_cast<uint32_t *>(data) = 3;
        state.gregs[0] = 32;
        state.gregs[2] = 0;
        context->SetFlagsFromCompactedEFLAGS(thread, 2);
        state.rip = code;
#ifdef VIXL_SIMULATOR
        auto &dispatcher =
            *static_cast<FEXCore::Context::ContextImpl *>(context)->Dispatcher;
        auto &sim = dispatcher.*SimulatorAccess::simulator();
        auto &decoder = dispatcher.*SimulatorAccess::decoder();
        AccessObserver observer(context, thread, sim, data);
        decoder.InsertVisitorBefore(&observer, &sim);
#endif
        execute(opaque);
#ifdef VIXL_SIMULATOR
        decoder.RemoveVisitor(&observer);
        result.flags[which] =
            observer.hits == 1 && (observer.flags & 0x8d5) == 0x55;
        if (!result.flags[which]) {
            const auto used = strlen(result.detail);
            snprintf(result.detail + used, sizeof result.detail - used,
                     "case=%u pre-access flags=%x hits=%u; ", which,
                     observer.flags, observer.hits);
        }
#endif
        const uint64_t begin = state.InlineJITBlockHeader;
        auto *backend = thread->CPUBackend.get();
        if (!begin || !backend->IsAddressInCodeBuffer(begin)) {
            continue;
        }
        const auto *header =
            reinterpret_cast<const FEXCore::CPU::CPUBackend::JITCodeHeader *>(
                begin);
        const auto *tail =
            reinterpret_cast<const FEXCore::CPU::CPUBackend::JITCodeTail *>(
                begin + header->OffsetToBlockTail);
        const auto *entry =
            reinterpret_cast<const uint8_t *>(tail) + tail->OffsetToRIPEntries;
        uint64_t hostPC = begin, guestPC = tail->RIP;
        bool exact = false;
        for (unsigned n = 0; n < tail->NumberOfRIPEntries; ++n) {
            const auto delta = FEXCore::Utils::vl64pair::Decode(entry);
            entry += delta.Size;
            hostPC += delta.IntegerARMPC;
            guestPC += delta.IntegerX86RIP;
            if (guestPC == code + 8 && hostPC - begin < tail->Size &&
                context->RestoreRIPFromHostPC(thread, hostPC) == code + 8) {
                exact = true;
            }
        }
        result.passed[which] = exact;
        if (!exact) {
            const size_t used = strlen(result.detail);
            snprintf(result.detail + used, sizeof result.detail - used,
                     "case=%u missing exact memory-PC metadata (entries=%u); ",
                     which, tail->NumberOfRIPEntries);
        }
    }
    return result;
}
} // namespace armxbox
