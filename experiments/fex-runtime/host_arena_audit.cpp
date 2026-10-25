// Host-only audit of the real allocator's OS calls. Reject a cross-boundary
// discard to avoid corrupting another mapping, and record a failing check.
#include "controlled_xbe.h"
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <sys/types.h>
namespace {
std::atomic<uintptr_t> base{ 0 };
std::atomic<size_t> size{ 0 };
std::atomic<bool> violation{ false };
} // namespace
extern "C" void *__real_mmap(void *, size_t, int, int, int, off_t);
extern "C" int __real_madvise(void *, size_t, int);
extern "C" void *__wrap_mmap(void *address, size_t length, int protection,
                             int flags, int fd, off_t offset)
{
    // Host-only compatibility injection exercises the real hint fallback and
    // conflict preservation without changing the Android mapping policy.
    const uintptr_t requested = reinterpret_cast<uintptr_t>(address);
    if ((flags & MAP_FIXED_NOREPLACE) && requested >= armxbox::XbeBase &&
        requested < armxbox::XbeBase + 0x14000 &&
        std::getenv("ARMXBOX_PROBE_XBE_HINT_ONLY")) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    void *result = __real_mmap(address, length, protection, flags, fd, offset);
    if (result != MAP_FAILED && !address && length == (2ULL << 30) &&
        protection == PROT_NONE && (flags & MAP_ANONYMOUS)) {
        size.store(length);
        base.store(reinterpret_cast<uintptr_t>(result));
    }
    return result;
}
extern "C" int __wrap_madvise(void *address, size_t length, int advice)
{
    const uintptr_t start = reinterpret_cast<uintptr_t>(address);
    const uintptr_t begin = base.load();
    const size_t bytes = size.load();
    if (begin && advice == MADV_DONTNEED && start >= begin &&
        start - begin < bytes && length > bytes - (start - begin)) {
        violation.store(true);
        std::fprintf(stderr,
                     "FAIL arena OS-call audit: discard exceeds owned "
                     "reservation by %zu bytes\n",
                     length - (bytes - (start - begin)));
        errno = EINVAL;
        return -1;
    }
    return __real_madvise(address, length, advice);
}
namespace armxbox {
bool hostArenaAuditOK()
{
    return base.load() && !violation.load();
}
} // namespace armxbox
