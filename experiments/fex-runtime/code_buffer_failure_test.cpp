// Exercise the real JIT buffer constructor in isolated children. No native
// crash handler is involved; the expected fatal diagnostic terminates the
// child.
#include "Interface/Core/SharedCodeBufferManager.h"
#include <FEXCore/Utils/AllocatorHooks.h>
#include <FEXCore/Utils/LogManager.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
int mode;
void fatal(const char *message)
{
    const char *expected = mode == 0 ? "Couldn't allocate code buffer:" :
                                       "Failed to protect code-buffer guard:";
    _exit(std::strstr(message, expected) && std::strstr(message, "errno=12") ?
              73 :
              2);
}
void *failedMap(void *, size_t, int, int, int, off_t)
{
    errno = ENOMEM;
    return MAP_FAILED;
}
} // namespace
extern "C" int __real_mprotect(void *, size_t, int);
extern "C" int __wrap_mprotect(void *address, size_t size, int protection)
{
    if (mode == 0)
        _exit(3); // MAP_FAILED must be rejected before attempting a guard.
    if (mode == 1) {
        errno = ENOMEM;
        return -1;
    }
    return __real_mprotect(address, size, protection);
}
int main()
{
    for (mode = 0; mode < 3; ++mode) {
        const pid_t child = fork();
        if (child < 0)
            return 1;
        if (!child) {
            LogMan::Throw::InstallHandler(fatal);
            if (mode == 0)
                FEXCore::Allocator::mmap = failedMap;
            {
                FEXCore::CPU::CodeBuffer buffer(16384, false);
                if (mode != 2 || buffer.UsableSize() != 12288)
                    _exit(4);
                buffer.GetBufferBase()[0] = 0x74;
            }
            _exit(mode == 2 ? 73 : 5);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 73)
            return 1;
        std::printf("PASS code buffer %s\n",
                    mode == 0 ? "rejects MAP_FAILED before guard access" :
                    mode == 1 ? "refuses failed guard protection" :
                                "retains successful allocation and release");
    }
}
