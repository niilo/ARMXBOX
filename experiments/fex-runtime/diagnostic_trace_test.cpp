#include "diagnostic.h"
#include "diagnostic_trace.h"
#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int main()
{
    char path[] = "/tmp/armxbox-trace-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
        return 1;
    unlink(path);
    pid_t child = fork();
    if (child < 0)
        return 1;
    if (child == 0) {
        armxbox_fex_set_trace_fd(fd);
        armxbox::traceLine("last boundary before abrupt process death");
        kill(getpid(), SIGKILL);
        _exit(2);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child || !WIFSIGNALED(status) ||
        WTERMSIG(status) != SIGKILL)
        return 1;
    char text[128]{};
    ssize_t size = pread(fd, text, sizeof text - 1, 0);
    if (size <= 0 ||
        std::strcmp(text, "last boundary before abrupt process death\n"))
        return 1;
    std::puts(
        "PASS progress survives abrupt child death without a crash handler");
    armxbox_fex_set_trace_fd(-1);
    armxbox::traceLine("must not use an uninstalled borrowed descriptor");
    struct stat file{};
    if (fstat(fd, &file) || file.st_size != size)
        return 1;
    std::puts(
        "PASS disabling journal stops writes before caller closes descriptor");
    if (ftruncate(fd, 0) || lseek(fd, 0, SEEK_SET) < 0)
        return 1;
    armxbox_fex_set_trace_fd(fd);
    std::array<char, 1025> large{};
    large.fill('x');
    large.back() = 0;
    for (unsigned i = 0; i < 160; ++i)
        armxbox::traceLine(large.data());
    if (fstat(fd, &file) || file.st_size > 128 * 1024 ||
        file.st_size < 100 * 1024)
        return 1;
    constexpr char marker[] =
        "TRACE LIMIT REACHED; further progress unavailable\n";
    char end[sizeof marker]{};
    if (pread(fd, end, sizeof marker - 1, file.st_size - sizeof marker + 1) !=
            sizeof marker - 1 ||
        std::strcmp(end, marker))
        return 1;
    std::puts("PASS native progress journal is capped with an explicit "
              "truncation marker");
    armxbox_fex_set_trace_fd(-1);
    close(fd);
    return 0;
}
