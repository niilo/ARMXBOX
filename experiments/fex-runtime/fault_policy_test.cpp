#include "fault_policy.h"
#include <cstdio>
#include <aarch64/decoder-aarch64.h>
#include <aarch64/simulator-aarch64.h>
using namespace armxbox::fault;
int main()
{
    unsigned failures = 0, checks = 0;
    auto check = [&](const char *name, bool ok) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("FAIL fault policy: %s\n", name);
        }
    };
    std::array<uint64_t, 31> regs{};
    regs[10] = 0x1000;
    regs[4] = 0xdeadbeef;
    auto load = decodeWord(0x88dffd44U, regs, 0x7000); // LDAR w4,[x10]
    auto store = decodeWord(0x889ffd44U, regs, 0x7000);
    check("LDAR word", load.valid && load.direction == Direction::Read &&
                           load.address == 0x1000);
    check("STLR word", store.valid && store.direction == Direction::Write &&
                           store.value == 0xdeadbeef);
    check("LDR scaled", decodeWord(0xb9400944U, regs, 0).address == 0x1008);
    check("STR scaled",
          decodeWord(0xb9000944U, regs, 0).direction == Direction::Write);
    check("LDUR negative", decodeWord(0xb85fc144U, regs, 0).address == 0xffc);
    check("STUR negative", decodeWord(0xb81fc144U, regs, 0).address == 0xffc);
    check("zero register store", decodeWord(0x889ffd5fU, regs, 0).value == 0);
    check("SP base", decodeWord(0xb94003e4U, regs, 0x7000).address == 0x7000);
    for (uint32_t op : { 0xd503201fU, 0xf9400144U, 0x39400144U, 0xb8401544U }) {
        check("reject unsupported width/writeback/opcode",
              !decodeWord(op, regs, 0).valid);
    }
    Descriptor expected{ 0x10000008, 0x1000, Direction::Read };
    Snapshot good{ 11,     2,      0x1000, 0x2004, 0x7000,     0x9000, 0x9000,
                   0x7100, 0x3000, 0x2000, 0x100,  0x10000008, load,   0x7200 };
    check("owned read", classify(expected, good) == Decision::Read);
    auto s = good;
    s.code = 1;
    check("unmapped read", classify(expected, s) == Decision::Read);
    auto declined = [&](Snapshot value) {
        check("decline unrelated/unsafe context",
              classify(expected, value) == Decision::Decline);
    };
    s = good;
    s.signal = 7;
    declined(s);
    s = good;
    s.code = 0;
    declined(s);
    s = good;
    s.code = -6;
    declined(s);
    s = good;
    s.faultAddress = 0xfff;
    declined(s);
    s = good;
    s.faultAddress = 0x1004;
    declined(s);
    s = good;
    s.frameRegister++;
    declined(s);
    s = good;
    s.frame = 0;
    declined(s);
    s = good;
    s.hostPC = 0x2100;
    declined(s);
    s = good;
    s.hostPC = 0x1ffc;
    declined(s);
    s = good;
    s.hostPC++;
    declined(s);
    s = good;
    s.hostSP++;
    declined(s);
    s = good;
    s.returnSP++;
    declined(s);
    s = good;
    s.hostSP = 0x7200;
    declined(s);
    s = good;
    s.returnSP = 0x200000;
    declined(s);
    s = good;
    s.stopPC = 0;
    declined(s);
    s = good;
    s.stackAnchor = 0;
    declined(s);
    s = good;
    s.stackAnchor = 0x7080;
    declined(s);
    s = good;
    s.stackAnchor = 0x200000;
    declined(s);
    s = good;
    s.stopPC++;
    declined(s);
    s = good;
    s.blockSize = 0;
    declined(s);
    s = good;
    s.guestPC++;
    check("unsupported exact PC",
          classify(expected, s) == Decision::Unsupported);
    s = good;
    s.access.valid = false;
    check("unsupported native instruction",
          classify(expected, s) == Decision::Unsupported);
    s = good;
    s.access.address++;
    check("unsupported decoded address",
          classify(expected, s) == Decision::Unsupported);
    s = good;
    s.access.direction = Direction::Write;
    check("unsupported direction",
          classify(expected, s) == Decision::Unsupported);
    expected.direction = Direction::Write;
    s = good;
    s.access = store;
    check("owned write", classify(expected, s) == Decision::Write);
    struct BusCount {
        unsigned reads = 0, writes = 0;
        uint64_t address = 0;
        uint32_t value = 0;
    } count;
    WordBus bus{ &count,
                 [](void *p, uint64_t address) -> uint32_t {
                     auto &c = *static_cast<BusCount *>(p);
                     ++c.reads;
                     c.address = address;
                     return 0x12345678;
                 },
                 [](void *p, uint64_t address, uint32_t value) {
                     auto &c = *static_cast<BusCount *>(p);
                     ++c.writes;
                     c.address = address;
                     c.value = value;
                 } };
    expected.direction = Direction::Read;
    Transaction tx{ expected, Decision::Read, expected.guestPC,
                    expected.address, 0 };
    uint32_t loaded = 0;
    check("service read once", service(tx, bus, loaded) &&
                                   loaded == 0x12345678 && count.reads == 1 &&
                                   count.address == expected.address);
    check("reject replayed read",
          !service(tx, bus, loaded) && count.reads == 1);
    expected.direction = Direction::Write;
    tx = { expected, Decision::Write, expected.guestPC, expected.address,
           0xdeadbeef };
    check("service write once", service(tx, bus, loaded) && count.writes == 1 &&
                                    count.value == 0xdeadbeef);
    check("reject replayed write",
          !service(tx, bus, loaded) && count.writes == 1);
    for (unsigned bad = 0; bad < 4; ++bad) {
        tx = { expected, Decision::Write, expected.guestPC, expected.address,
               0 };
        if (bad == 0) {
            ++tx.address;
        } else if (bad == 1) {
            ++tx.guestPC;
        } else if (bad == 2) {
            tx.kind = Decision::Unsupported;
        } else {
            tx.kind = Decision::Read;
        }
        check("reject invalid transfer without bus side effect",
              !service(tx, bus, loaded) && count.reads == 1 &&
                  count.writes == 1);
    }
    // Observe actual ARM memory effects under VIXL rather than validating the
    // decoder only against hand-written expected constants.
    for (uint32_t op : { 0x88dffd44U, 0x889ffd44U, 0xb9400944U, 0xb9000944U,
                         0xb85fc144U, 0xb81fc144U, 0x889ffd5fU }) {
        std::array<uint32_t, 8> cells{};
        for (unsigned i = 0; i < cells.size(); ++i) {
            cells[i] = 0x12340000 + i;
        }
        std::array<uint64_t, 31> native{};
        native[10] = reinterpret_cast<uintptr_t>(&cells[2]);
        native[4] = 0xdeadbeef;
        const auto decoded = decodeWord(op, native, 0);
        vixl::aarch64::Decoder decoder;
        vixl::aarch64::Simulator simulator(&decoder);
        simulator.WriteXRegister(10, native[10]);
        simulator.WriteXRegister(4, native[4]);
        const std::array<uint32_t, 2> instructions{ op, 0xd65f03c0 };
        simulator.RunFrom(reinterpret_cast<const vixl::aarch64::Instruction *>(
            instructions.data()));
        unsigned changed = 0, index = 0;
        for (unsigned i = 0; i < cells.size(); ++i) {
            if (cells[i] != 0x12340000 + i) {
                ++changed;
                index = i;
            }
        }
        if (changed) {
            check("decoded store matches simulated memory effect",
                  changed == 1 && decoded.valid &&
                      decoded.direction == Direction::Write &&
                      decoded.address ==
                          reinterpret_cast<uintptr_t>(&cells[index]) &&
                      decoded.value == cells[index]);
        } else {
            const uint32_t observed = simulator.ReadXRegister(4);
            bool matched = false;
            for (unsigned i = 0; i < cells.size(); ++i) {
                if (cells[i] == observed &&
                    decoded.address == reinterpret_cast<uintptr_t>(&cells[i])) {
                    matched = true;
                }
            }
            check("decoded load matches simulated memory effect",
                  decoded.valid && decoded.direction == Direction::Read &&
                      matched);
        }
    }
    std::printf("%s raw-fault policy: %u checks\n", failures ? "FAIL" : "PASS",
                checks);
    return failures ? 1 : 0;
}
