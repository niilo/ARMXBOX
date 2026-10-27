#pragma once
#include <array>
#include <cstdint>

namespace armxbox::fault {
enum class Direction : uint8_t { Read, Write };
struct Access {
    bool valid = false;
    Direction direction{};
    uint64_t address = 0;
    uint32_t value = 0;
};
// Only the scalar word operations needed by the controlled probe are supported.
// No generic ARM decoder or arbitrary guest-instruction emulation is implied.
inline Access decodeWord(uint32_t op, const std::array<uint64_t, 31> &regs,
                         uint64_t sp)
{
    Direction direction{};
    int64_t offset = 0;
    if ((op & 0xfffffc00U) == 0x88dffc00U) {
        direction = Direction::Read; // LDAR Wt,[Xn]
    } else if ((op & 0xfffffc00U) == 0x889ffc00U) {
        direction = Direction::Write; // STLR
    } else if ((op & 0xffc00000U) == 0xb9400000U) {
        direction = Direction::Read;
        offset = ((op >> 10) & 4095) * 4;
    } else if ((op & 0xffc00000U) == 0xb9000000U) {
        direction = Direction::Write;
        offset = ((op >> 10) & 4095) * 4;
    } else if ((op & 0xffe00c00U) == 0xb8400000U ||
               (op & 0xffe00c00U) == 0xb8000000U) {
        direction = (op & 0x00400000U) ? Direction::Read : Direction::Write;
        offset = (op >> 12) & 511;
        if (offset & 256) {
            offset -= 512; // LDUR/STUR, no writeback
        }
    } else {
        return {};
    }
    const unsigned base = (op >> 5) & 31, target = op & 31;
    return { true, direction, (base == 31 ? sp : regs[base]) + offset,
             target == 31 ? 0U : static_cast<uint32_t>(regs[target]) };
}
struct Descriptor {
    uint64_t guestPC = 0, address = 0;
    Direction direction{};
};
struct Snapshot {
    int signal = 0, code = 0;
    uint64_t faultAddress = 0, hostPC = 0, hostSP = 0, frameRegister = 0;
    uint64_t frame = 0, returnSP = 0, stopPC = 0, blockBegin = 0, blockSize = 0,
             guestPC = 0;
    Access access{};
    uint64_t stackAnchor = 0;
};
enum class Decision { Decline, Unsupported, Read, Write };
inline Decision classify(const Descriptor &expected, const Snapshot &s)
{
    // Linux/Bionic SIGSEGV and positive MAPERR/ACCERR only; reject user
    // signals.
    if (s.signal != 11 || (s.code != 1 && s.code != 2) ||
        s.faultAddress < expected.address ||
        s.faultAddress - expected.address >= 4) {
        return Decision::Decline;
    }
    if (!s.frame || s.frameRegister != s.frame || !s.stopPC || (s.stopPC & 3) ||
        (s.hostPC & 3)) {
        return Decision::Decline;
    }
    if (!s.returnSP || (s.returnSP & 15) || (s.hostSP & 15) ||
        s.hostSP > s.returnSP || s.returnSP - s.hostSP > 1024 * 1024) {
        return Decision::Decline;
    }
    if (!s.stackAnchor || s.returnSP > s.stackAnchor ||
        s.hostSP > s.stackAnchor || s.stackAnchor - s.hostSP > 1024 * 1024) {
        return Decision::Decline;
    }
    if (!s.blockSize || s.hostPC < s.blockBegin ||
        s.hostPC - s.blockBegin >= s.blockSize) {
        return Decision::Decline;
    }
    if (s.guestPC != expected.guestPC || !s.access.valid ||
        s.access.address != expected.address ||
        s.access.direction != expected.direction) {
        return Decision::Unsupported;
    }
    return expected.direction == Direction::Read ? Decision::Read :
                                                   Decision::Write;
}
struct WordBus {
    void *opaque = nullptr;
    uint32_t (*read)(void *, uint64_t) = nullptr;
    void (*write)(void *, uint64_t, uint32_t) = nullptr;
};
struct Transaction {
    Descriptor expected{};
    Decision kind{};
    uint64_t guestPC = 0, address = 0;
    uint32_t value = 0;
    bool consumed = false;
};
inline bool service(Transaction &tx, const WordBus &bus, uint32_t &loaded)
{
    if (tx.consumed || tx.guestPC != tx.expected.guestPC ||
        tx.address != tx.expected.address) {
        return false;
    }
    if (tx.kind == Decision::Read && tx.expected.direction == Direction::Read &&
        bus.read) {
        tx.consumed = true;
        loaded = bus.read(bus.opaque, tx.address);
        return true;
    }
    if (tx.kind == Decision::Write &&
        tx.expected.direction == Direction::Write && bus.write) {
        tx.consumed = true;
        bus.write(bus.opaque, tx.address, tx.value);
        return true;
    }
    return false;
}

} // namespace armxbox::fault
