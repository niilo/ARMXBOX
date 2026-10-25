// SPDX-License-Identifier: GPL-2.0-or-later
#include "guest_reservation.h"
#include <cerrno>
namespace armxbox {
GuestReservation reserveGuestExact(uint32_t address, size_t size,
                                   GuestMappingOps ops)
{
    GuestReservation result;
    if (!address || (address & 4095) || !size || (size & 4095) ||
        size > (1ULL << 32) - address) {
        result.noReplaceError = EINVAL;
        return result;
    }
    void *target = reinterpret_cast<void *>(uintptr_t(address));
    constexpr int Flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void *p =
        ops.map(target, size, PROT_NONE, Flags | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == target) {
        result.address = p;
        return result;
    }
    if (p != MAP_FAILED) {
        result.alternate = true; // Kernel ignored NOREPLACE and used a hint.
        ops.unmap(p, size);
    } else {
        result.noReplaceError = errno;
        // An occupied range must fail. Retry only errors consistent with an
        // unsupported or restricted mapping flag; never force an allocation.
        if (errno != EINVAL && errno != EOPNOTSUPP && errno != ENOSYS &&
            errno != EPERM)
            return result;
    }
    result.usedHint = true;
    p = ops.map(target, size, PROT_NONE, Flags, -1, 0);
    if (p == target) {
        result.address = p;
    } else if (p == MAP_FAILED) {
        result.hintError = errno;
    } else {
        result.alternate = true;
        ops.unmap(p, size);
        result.hintError = EADDRNOTAVAIL;
    }
    return result;
}
} // namespace armxbox
