// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <sys/mman.h>
#include <sys/types.h>
namespace armxbox {
struct GuestMappingOps {
    void *(*map)(void *, size_t, int, int, int, off_t) = ::mmap;
    int (*unmap)(void *, size_t) = ::munmap;
};
struct GuestReservation {
    void *address = nullptr;
    int noReplaceError = 0, hintError = 0;
    bool usedHint = false, alternate = false;
};
// Never uses MAP_FIXED. A hint fallback may only return the exact requested
// address; alternate mappings are released without touching the requested
// range.
GuestReservation reserveGuestExact(uint32_t address, size_t size,
                                   GuestMappingOps ops = {});
} // namespace armxbox
