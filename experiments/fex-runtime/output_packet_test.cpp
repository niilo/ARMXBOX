// SPDX-License-Identifier: GPL-2.0-or-later
#include "output_packet.h"
#include <algorithm>
#include <cstdio>
using namespace armxbox;
namespace {
constexpr uint32_t Base = 0x13000;
unsigned checks = 0, failed = 0;
void check(bool ok, const char *name)
{
    ++checks;
    failed += !ok;
    std::printf("%s output contract %s\n", ok ? "PASS" : "FAIL", name);
}
void put(OutputFixture &bytes, unsigned at, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = value >> (8 * i);
}
StoppedOutputView view(const OutputFixture &data, uint64_t gen = 1)
{
    return { Base, data, gen, true, false };
}
} // namespace
int main()
{
    auto data = outputFixture(Base);
    OutputQueue q;
    check(q.submit(view(data), Base) == OutputError::None, "valid publication");
    auto first = q.acquire();
    auto *packet = q.get(first);
    check(packet && outputMatches(*packet, 1), "all immutable payload bytes");
    data.fill(0);
    check(packet && outputMatches(*packet, 1),
          "source mutation cannot alter snapshot");
    check(!q.acquire().generation, "single in-flight consumer");
    check(!q.reset(2), "reset blocked while in flight");
    auto secondData = outputFixture(Base, 2);
    check(q.submit(view(secondData), Base) == OutputError::None,
          "second slot publication");
    check(q.submit({ 0, {}, 0, false, true }, 0xffffffff) == OutputError::Full,
          "full queue rejects before guest read");
    auto bad = first;
    ++bad.sequence;
    check(!q.retire(bad), "wrong sequence cannot retire");
    bad = first;
    ++bad.generation;
    check(!q.retire(bad), "wrong generation cannot retire");
    bad = first;
    ++bad.slot;
    check(!q.retire(bad), "wrong slot cannot retire");
    OutputQueue other;
    auto otherData = outputFixture(Base);
    other.submit(view(otherData), Base);
    auto foreign = other.acquire();
    check(!q.get(foreign) && !q.retire(foreign),
          "foreign queue lease rejected");
    check(q.retire(first) && !q.get(first), "retirement invalidates lease");
    auto second = q.acquire();
    check(q.get(second) && outputMatches(*q.get(second), 2),
          "FIFO payload order");
    check(q.retire(second) && q.size() == 0 && q.reset(2),
          "drained generation reset");
    data = outputFixture(Base);
    check(q.submit(view(data), Base) == OutputError::Generation,
          "stale generation rejected");
    check(q.submit(view(data, 2), Base) == OutputError::None,
          "new generation restarts sequence");
    auto fresh = q.acquire();
    check(!q.retire(first) && q.retire(fresh),
          "old lease cannot retire reused slot");
    check(q.submit(view(data, 2), Base) == OutputError::Sequence,
          "replayed sequence rejected");
    check(!q.reset(1) && !q.reset(2), "generation cannot rewind");
    OutputQueue zero(0);
    check(zero.submit(view(data, 0), Base) == OutputError::Generation,
          "zero generation invalid");
    auto reject = [&](OutputFixture bytes, OutputError expected,
                      const char *name) {
        OutputQueue fresh;
        const bool rejected = fresh.submit(view(bytes), Base) == expected &&
                              !fresh.size() && !fresh.acquire().generation;
        const auto valid = outputFixture(Base);
        check(rejected && fresh.submit(view(valid), Base) == OutputError::None,
              name);
    };
    for (unsigned offset :
         { 0U, 4U, 16U, 24U, 28U, 32U, 40U, 48U, 52U, 56U, 60U }) {
        data = outputFixture(Base);
        put(data, offset, 0xffffffff);
        reject(data, OutputError::Format,
               "unsupported metadata rejects transactionally");
    }
    data = outputFixture(Base);
    put(data, 44, 0);
    reject(data, OutputError::Incomplete, "uncommitted packet rejected");
    data = outputFixture(Base);
    put(data, 8, 0);
    reject(data, OutputError::Sequence, "zero sequence rejected");
    for (unsigned offset : { 12U, 20U, 36U }) {
        data = outputFixture(Base);
        put(data, offset, 0xfffffff0);
        reject(data, OutputError::Bounds,
               "overflowing payload address rejected");
    }
    data = outputFixture(Base);
    put(data, 12, Base);
    reject(data, OutputError::Layout, "header overlap rejected");
    data = outputFixture(Base);
    put(data, 20, Base + 0x80);
    reject(data, OutputError::Layout, "payload overlap rejected");
    data = outputFixture(Base);
    put(data, 36, Base + 0xff);
    reject(data, OutputError::Layout, "unaligned sample address rejected");
    for (uint32_t bits :
         { 0x7fc00000U, 0x7f800000U, 0xff800000U, 0x40000000U, 0xc0000000U }) {
        data = outputFixture(Base);
        put(data, 0x80, bits);
        reject(data, OutputError::Vertex,
               "nonfinite or out-of-range vertex rejected");
    }
    data = outputFixture(Base);
    OutputQueue gate;
    check(gate.submit({ Base, {}, 1, false, false }, Base) ==
              OutputError::NotStopped,
          "running guest rejected before read");
    check(gate.submit({ Base, {}, 1, true, true }, Base) ==
              OutputError::Cancelled,
          "cancelled publication rejected before read");
    check(gate.submit({ Base, std::span(data).first(63), 1, true, false },
                      Base) == OutputError::Bounds,
          "truncated header rejected");
    check(gate.submit({ Base, std::span(data).first(0x17f), 1, true, false },
                      Base) == OutputError::Bounds,
          "cross-boundary samples rejected");
    check(gate.submit({ 0xfffffff0, data, 1, true, false }, 0xfffffff0) ==
              OutputError::Bounds,
          "overflowing owned view rejected");
    {
        auto temporary = outputFixture(Base);
        check(gate.submit(view(temporary), Base) == OutputError::None,
              "scoped source captured");
    }
    auto lease = gate.acquire();
    check(gate.get(lease) && outputMatches(*gate.get(lease), 1),
          "snapshot outlives source scope");
    uint32_t seed = 0x120a;
    for (unsigned i = 0; i < 4000; ++i) {
        data = outputFixture(Base);
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        data[seed % data.size()] ^= uint8_t((seed >> 16) | 1);
        OutputQueue fuzz;
        const auto error = fuzz.submit(view(data), Base);
        if (error == OutputError::None) {
            auto l = fuzz.acquire();
            const auto *p = fuzz.get(l);
            if (!p ||
                !std::equal(p->texture().begin(), p->texture().end(),
                            data.begin() + 0xa0) ||
                !fuzz.retire(l))
                return 1;
        } else if (fuzz.size())
            return 1;
    }
    check(true, "4000 seeded malformed/payload mutations");
    std::printf("%s output contract: %u controls\n", failed ? "FAIL" : "PASS",
                checks);
    return failed ? 1 : 0;
}
