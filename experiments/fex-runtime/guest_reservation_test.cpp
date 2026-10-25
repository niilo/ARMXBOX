// SPDX-License-Identifier: GPL-2.0-or-later
#include "guest_reservation.h"
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <utility>
namespace {
constexpr uintptr_t Target = 0x10000, Other = 0x90000000;
unsigned calls, releases, scenario;
std::array<int, 2> flags;
void *fakeMap(void *p, size_t size, int prot, int mode, int fd, off_t offset)
{
    if (calls >= 2 || reinterpret_cast<uintptr_t>(p) != Target ||
        size != 16384 || prot != PROT_NONE || fd != -1 || offset ||
        (mode & MAP_FIXED))
        std::abort();
    flags[calls++] = mode;
    if (calls == 1) {
        if (!(mode & MAP_FIXED_NOREPLACE))
            std::abort();
        if (scenario == 0)
            return p;
        if (scenario == 5)
            return reinterpret_cast<void *>(Other);
        errno = scenario == 2 ? EPERM :
                scenario == 3 ? EEXIST :
                scenario == 6 ? ENOMEM :
                                EINVAL;
        return MAP_FAILED;
    }
    if (mode & MAP_FIXED_NOREPLACE)
        std::abort();
    if (scenario == 4)
        return reinterpret_cast<void *>(Other);
    if (scenario == 7) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return p;
}
int fakeUnmap(void *p, size_t size)
{
    if (reinterpret_cast<uintptr_t>(p) != Other || size != 16384)
        std::abort();
    ++releases;
    return 0;
}
} // namespace
int main()
{
    for (scenario = 0; scenario < 8; ++scenario) {
        calls = releases = 0;
        auto r =
            armxbox::reserveGuestExact(Target, 16384, { fakeMap, fakeUnmap });
        bool ok = false;
        switch (scenario) {
        case 0:
            ok = r.address && calls == 1 && !r.usedHint && !releases;
            break;
        case 1:
            ok = r.address && calls == 2 && r.usedHint &&
                 r.noReplaceError == EINVAL;
            break;
        case 2:
            ok = r.address && calls == 2 && r.usedHint &&
                 r.noReplaceError == EPERM;
            break;
        case 3:
            ok = !r.address && calls == 1 && !r.usedHint &&
                 r.noReplaceError == EEXIST;
            break;
        case 4:
            ok = !r.address && calls == 2 && releases == 1 &&
                 r.hintError == EADDRNOTAVAIL;
            break;
        case 5:
            ok = r.address && calls == 2 && releases == 1 && r.alternate &&
                 r.usedHint;
            break;
        case 6:
            ok = !r.address && calls == 1 && !r.usedHint &&
                 r.noReplaceError == ENOMEM;
            break;
        case 7:
            ok =
                !r.address && calls == 2 && r.usedHint && r.hintError == ENOMEM;
            break;
        }
        if (!ok)
            return 1;
        std::printf("PASS guest reservation syscall control %u\n", scenario);
    }
    calls = 0;
    for (auto [address, size] : std::array<std::pair<uint32_t, size_t>, 4>{
             { { 0, 16384 },
               { 0x10001, 16384 },
               { 0x10000, 0 },
               { 0xfffff000, 8192 } } }) {
        const auto r =
            armxbox::reserveGuestExact(address, size, { fakeMap, fakeUnmap });
        if (r.address || r.noReplaceError != EINVAL || calls)
            return 1;
    }
    std::puts("PASS guest reservation rejects invalid or overflowing ranges "
              "before syscalls");
}
