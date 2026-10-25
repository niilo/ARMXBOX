#include "diagnostic.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    int trace = -1;
    if (argc == 3 && !std::strcmp(argv[1], "--trace")) {
        trace = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (trace < 0)
            return 3;
        armxbox_fex_set_trace_fd(trace);
    } else if (argc != 1) {
        std::fputs("usage: armxbox_fex_selftest [--trace FILE]\n", stderr);
        return 3;
    }
    char report[16384];
    int status = armxbox_fex_selftest(report, sizeof report);
    std::fputs(report, stdout);
    armxbox_fex_set_trace_fd(-1);
    if (trace >= 0)
        close(trace);
    return status;
}
