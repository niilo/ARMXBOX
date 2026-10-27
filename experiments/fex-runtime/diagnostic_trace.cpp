#include "diagnostic.h"
#include "diagnostic_trace.h"
#include <cerrno>
#include <cstring>
#include <mutex>
#include <unistd.h>

namespace {
std::mutex TraceMutex;
int TraceFD = -1;
size_t TraceBytes = 0;
constexpr size_t TraceLimit = 128 * 1024;
constexpr char Truncated[] =
    "TRACE LIMIT REACHED; further progress unavailable\n";
bool writeAll(const char *text, size_t size)
{
    while (size) {
        const auto count = write(TraceFD, text, size);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return false;
        text += count;
        size -= count;
    }
    return true;
}
} // namespace
extern "C" void armxbox_fex_set_trace_fd(int fd)
{
    std::lock_guard lock(TraceMutex);
    TraceFD = fd;
    TraceBytes = 0;
}
namespace armxbox {
void traceLine(const char *text)
{
    std::lock_guard lock(TraceMutex);
    if (TraceFD < 0 || TraceBytes >= TraceLimit)
        return;
    // Limit output even if a dependency emits an unexpectedly large message.
    const size_t size = strnlen(text, 1024);
    if (TraceBytes + size + 1 + sizeof Truncated > TraceLimit) {
        writeAll(Truncated, sizeof Truncated - 1);
        fsync(TraceFD);
        TraceBytes = TraceLimit;
        return;
    }
    if (writeAll(text, size) && writeAll("\n", 1)) {
        TraceBytes += size + 1;
        // The next instruction may fault. Preserve the last completed boundary
        // before returning; do not depend on a crash handler or C++ unwinding.
        fsync(TraceFD);
    } else {
        TraceBytes = TraceLimit;
    }
}
} // namespace armxbox
