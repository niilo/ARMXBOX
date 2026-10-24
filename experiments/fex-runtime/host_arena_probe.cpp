#include "host_arena_probe.h"
#include <FEXCore/Utils/AllocatorHooks.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

namespace armxbox {
namespace {
constexpr size_t Page = 4096;
constexpr uintptr_t GuestEnd = 1ULL << 32;
bool outsideArena(uintptr_t p, size_t length, uintptr_t base, size_t size)
{
    if (!p || !length || p < GuestEnd || p > UINTPTR_MAX - length) {
        return false;
    }
    return p >= base + size || p + length <= base;
}
bool normalAllocation(uintptr_t base, size_t size)
{
    constexpr size_t Bytes = 1024 * 1024;
    auto *p = static_cast<unsigned char *>(malloc(Bytes));
    if (!p) {
        return false;
    }
    memset(p, 0x6d, Bytes);
    const bool good =
        outsideArena(reinterpret_cast<uintptr_t>(p), Bytes, base, size) &&
        static_cast<volatile unsigned char *>(p)[Bytes - 1] == 0x6d;
    free(p);
    return good;
}
bool normalMapping(uintptr_t base, size_t size)
{
    auto *p = static_cast<unsigned char *>(
        ::mmap(nullptr, Page, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (p == MAP_FAILED) {
        return false;
    }
    memset(p, 0x3c, Page);
    const bool good =
        outsideArena(reinterpret_cast<uintptr_t>(p), Page, base, size) &&
        static_cast<volatile unsigned char *>(p)[Page - 1] == 0x3c;
    return ::munmap(p, Page) == 0 && good;
}
bool contained(uintptr_t p, size_t length, uintptr_t base, size_t size)
{
    return p >= base && p < base + size && length <= base + size - p;
}
bool boundedMapping(uintptr_t base, size_t size)
{
    void *p = FEXCore::Allocator::mmap(nullptr, Page, PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        return false;
    }
    memset(p, 0x74, Page);
    const bool good =
        contained(reinterpret_cast<uintptr_t>(p), Page, base, size);
    return FEXCore::Allocator::munmap(p, Page) == 0 && good;
}
struct Churn {
    std::atomic<bool> ready{ false }, stop{ false }, good{ true };
    uintptr_t held = 0;
    unsigned rounds = 0;
};
void *churn(void *opaque)
{
    auto &c = *static_cast<Churn *>(opaque);
    auto *held = static_cast<unsigned char *>(
        ::mmap(nullptr, Page, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (held == MAP_FAILED) {
        c.good = false;
        c.ready = true;
        return nullptr;
    }
    memset(held, 0x27, Page);
    c.held = reinterpret_cast<uintptr_t>(held);
    c.ready.store(true, std::memory_order_release);
    do {
        const bool ok = normalAllocation(0, GuestEnd) &&
                        normalMapping(0, GuestEnd) && held[0] == 0x27 &&
                        held[Page - 1] == 0x27;
        if (!ok) {
            c.good = false;
        }
        ++c.rounds;
        sched_yield();
    } while (!c.stop.load(std::memory_order_acquire));
    if (::munmap(held, Page)) {
        c.good = false;
    }
    return nullptr;
}
struct Worker {
    uintptr_t base;
    size_t size;
    bool passed = false;
};
void *stress(void *opaque)
{
    auto &w = *static_cast<Worker *>(opaque);
    unsigned char stackMarker = 0;
    const uintptr_t stack = reinterpret_cast<uintptr_t>(&stackMarker);
    bool good = outsideArena(stack, sizeof stackMarker, w.base, w.size);
    for (unsigned i = 0; i < 64; ++i) {
        good &= normalAllocation(w.base, w.size) &&
                normalMapping(w.base, w.size) && boundedMapping(w.base, w.size);
    }
    w.passed = good;
    return nullptr;
}
bool hostWorkers(uintptr_t base, size_t size)
{
    Worker workers[]{ { base, size }, { base, size } };
    pthread_t ids[2];
    bool started[2]{};
    for (unsigned i = 0; i < 2; ++i) {
        started[i] = !pthread_create(&ids[i], nullptr, stress, &workers[i]);
    }
    bool good = true;
    for (unsigned i = 0; i < 2; ++i) {
        good &=
            started[i] && !pthread_join(ids[i], nullptr) && workers[i].passed;
    }
    return good;
}
bool freshLibrary(uintptr_t base, size_t size, char *detail, size_t capacity)
{
    const char *name = "libarmxbox_host_mapping_probe.so";
    void *prior = dlopen(name, RTLD_NOW | RTLD_NOLOAD);
    if (prior) {
        dlclose(prior);
        snprintf(detail, capacity, "host helper was already loaded");
        return false;
    }
    void *handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        snprintf(detail, capacity, "host dlopen: %s", dlerror());
        return false;
    }
    auto *symbol = dlsym(handle, "armxbox_host_mapping_probe");
    using Call = uint32_t (*)(uint32_t);
    const auto call = reinterpret_cast<Call>(symbol);
    const bool good =
        call &&
        outsideArena(reinterpret_cast<uintptr_t>(symbol), 4, base, size) &&
        call(0x1234abcd) == (0x1234abcdU ^ 0xa5c36f19U);
    const bool closed = dlclose(handle) == 0;
    if (!good || !closed) {
        snprintf(detail, capacity,
                 "host helper symbol/result/location or close failed");
    }
    return closed && good;
}
bool reuseMapping(uintptr_t base, size_t size)
{
    auto *p = static_cast<unsigned char *>(
        FEXCore::Allocator::mmap(nullptr, 2 * Page, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (p == MAP_FAILED) {
        return false;
    }
    memset(p, 0x7e, 2 * Page);
    bool good = contained(reinterpret_cast<uintptr_t>(p), 2 * Page, base, size);
    good &= !FEXCore::Allocator::munmap(p, Page);
    void *reused = FEXCore::Allocator::mmap(
        p, Page, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    good &= reused == p && p[Page] == 0x7e;
    if (reused == p) {
        good &= p[0] == 0;
    }
    good &= !FEXCore::Allocator::munmap(p, 2 * Page);
    return good;
}
bool rejectedSizes(size_t size)
{
    auto request = [](size_t length, int error) {
        errno = 0;
        void *p =
            FEXCore::Allocator::mmap(nullptr, length, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        const bool good = p == MAP_FAILED && errno == error;
        if (p != MAP_FAILED) {
            FEXCore::Allocator::munmap(p, length);
        }
        return good;
    };
    return request(0, EINVAL) && request(SIZE_MAX, EINVAL) &&
           request(size, ENOMEM);
}
bool fileAlias(uintptr_t base, size_t size)
{
    const int fd = syscall(SYS_memfd_create, "armxbox-arena-alias", 1U);
    if (fd < 0) {
        return false;
    }
    if (ftruncate(fd, Page)) {
        close(fd);
        return false;
    }
    void *a = FEXCore::Allocator::mmap(nullptr, Page, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, fd, 0);
    void *b = ::mmap(nullptr, Page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    bool good = a != MAP_FAILED && b != MAP_FAILED;
    if (good) {
        *static_cast<uint32_t *>(a) = 0xfedc9876;
        good &= *static_cast<uint32_t *>(b) == 0xfedc9876;
        *static_cast<uint32_t *>(b) = 0x12345678;
        good &= *static_cast<uint32_t *>(a) == 0x12345678;
        good &= contained(reinterpret_cast<uintptr_t>(a), Page, base, size) &&
                outsideArena(reinterpret_cast<uintptr_t>(b), Page, base, size);
    }
    if (a != MAP_FAILED) {
        good &= !FEXCore::Allocator::munmap(a, Page);
    }
    if (b != MAP_FAILED) {
        good &= !::munmap(b, Page);
    }
    close(fd);
    return good;
}
} // namespace
HostMemorySnapshot hostMemorySnapshot()
{
    HostMemorySnapshot result;
    FILE *file = fopen("/proc/self/status", "r");
    if (!file) {
        return result;
    }
    bool vm = false, rss = false;
    char line[256];
    while (fgets(line, sizeof line, file)) {
        if (sscanf(line, "VmSize: %zu kB", &result.virtualKiB) == 1) {
            vm = true;
        }
        if (sscanf(line, "VmRSS: %zu kB", &result.residentKiB) == 1) {
            rss = true;
        }
    }
    fclose(file);
    result.available = vm && rss;
    return result;
}
HostArenaResults prepareHostArena(uintptr_t guestCode)
{
    HostArenaResults result;
    const auto originalMap = FEXCore::Allocator::mmap;
    const auto originalUnmap = FEXCore::Allocator::munmap;
    auto invalid = [&](size_t page, size_t size) {
        uintptr_t out = 123;
        errno = 0;
        return !FEXCore::Allocator::SetupBoundedHooks(page, size, out) &&
               out == 0 && errno == EINVAL &&
               originalMap == FEXCore::Allocator::mmap &&
               originalUnmap == FEXCore::Allocator::munmap;
    };
    result.passed[0] = invalid(16384, result.size);
    result.passed[1] = invalid(Page, 64 * 1024 * 1024);
    result.passed[2] = invalid(Page, result.size + 1);
    result.passed[3] = invalid(Page, SIZE_MAX);
    auto *sentinel = static_cast<unsigned char *>(
        ::mmap(nullptr, Page, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (sentinel != MAP_FAILED) {
        memset(sentinel, 0x19, Page);
    }
    Churn concurrent;
    pthread_t id;
    const bool started = !pthread_create(&id, nullptr, churn, &concurrent);
    if (started) {
        while (!concurrent.ready.load(std::memory_order_acquire)) {
            sched_yield();
        }
    }
    result.ready =
        FEXCore::Allocator::SetupBoundedHooks(Page, result.size, result.base);
    const int setupError = errno;
    concurrent.stop.store(true, std::memory_order_release);
    const bool joined = started && !pthread_join(id, nullptr);
    result.passed[4] = result.ready && result.base >= GuestEnd &&
                       result.base <= UINTPTR_MAX - result.size;
    result.passed[7] =
        joined && concurrent.good && concurrent.rounds &&
        outsideArena(concurrent.held, Page, result.base, result.size);
    if (!result.ready) {
        snprintf(result.detail, sizeof result.detail,
                 "bounded arena initialization failed: errno=%d", setupError);
        if (sentinel != MAP_FAILED) {
            ::munmap(sentinel, Page);
        }
        return result;
    }
    uintptr_t again = 123;
    const auto installedMap = FEXCore::Allocator::mmap;
    const auto installedUnmap = FEXCore::Allocator::munmap;
    errno = 0;
    result.passed[5] =
        !FEXCore::Allocator::SetupBoundedHooks(Page, result.size, again) &&
        errno == EBUSY && again == 0 &&
        installedMap == FEXCore::Allocator::mmap &&
        installedUnmap == FEXCore::Allocator::munmap;
    result.passed[6] = sentinel != MAP_FAILED &&
                       outsideArena(reinterpret_cast<uintptr_t>(sentinel), Page,
                                    result.base, result.size) &&
                       sentinel[0] == 0x19 && sentinel[Page - 1] == 0x19;
    result.passed[8] = normalAllocation(result.base, result.size);
    result.passed[9] = normalMapping(result.base, result.size);
    result.passed[10] = hostWorkers(result.base, result.size);
    result.passed[11] = freshLibrary(result.base, result.size, result.detail,
                                     sizeof result.detail);
    result.passed[12] = reuseMapping(result.base, result.size);
    if (sentinel != MAP_FAILED) {
        errno = 0;
        const auto p = FEXCore::Allocator::mmap(
            sentinel, Page, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        bool rejected = p == MAP_FAILED && errno == EINVAL;
        errno = 0;
        rejected &=
            FEXCore::Allocator::munmap(sentinel, Page) == -1 && errno == EINVAL;
        result.passed[13] =
            rejected && sentinel[0] == 0x19 && sentinel[Page - 1] == 0x19;
        ::munmap(sentinel, Page);
    }
    auto *low = reinterpret_cast<unsigned char *>(guestCode);
    const auto saved = *low;
    *low = 0x58;
    errno = 0;
    const auto p = FEXCore::Allocator::mmap(
        low, Page, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    bool rejected = p == MAP_FAILED && errno == EINVAL;
    errno = 0;
    rejected &= FEXCore::Allocator::munmap(low, Page) == -1 && errno == EINVAL;
    result.passed[14] = rejected && *low == 0x58;
    *low = saved;
    result.passed[15] = rejectedSizes(result.size);
    result.passed[16] = fileAlias(result.base, result.size);
    return result;
}
std::array<bool, 2> finishHostArena(uintptr_t base, size_t size)
{
    return { normalAllocation(base, size) && normalMapping(base, size) &&
                 hostWorkers(base, size),
             rejectedSizes(size) && boundedMapping(base, size) };
}
} // namespace armxbox
