/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Compile the real SDL startup argument construction without SDL/JNI. */
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

#define LogInfo(...) ((void)0)
#define LogInfoFmt(...) ((void)0)
#define LogInfoInt(...) ((void)0)

static bool IsTcgTuningEnabled()
{
    return std::getenv("STARTUP_TEST_TUNING")[0] != '0';
}

static const char *GetTcgThreadFromEnv()
{
    return "multi";
}

static int GetTcgTbSizeFromEnv()
{
    return 256;
}

static void xemu_android_display_preinit()
{
}

#include "startup-launch-context.inc"

int main(int argc, char **argv)
{
    assert(argc == 2);
    int mask = std::atoi(argv[1]);
    int g_dvd_fd = mask & 1 ? open("/dev/null", O_RDONLY | O_CLOEXEC) : -1;
    int g_dvd_fd_rw = mask & 2 ? open("/dev/null", O_RDWR | O_CLOEXEC) : -1;

    assert(!(mask & 1) || g_dvd_fd >= 0);
    assert(!(mask & 2) || g_dvd_fd_rw >= 0);
#include "startup-android-args.inc"
    assert(launch_ctx.argv[launch_ctx.argc] == nullptr);
    for (int i = 0; i < launch_ctx.argc; i++) {
        const char *arg = launch_ctx.argv[i];

        std::fwrite(arg, 1, std::char_traits<char>::length(arg) + 1, stdout);
    }
    if (g_dvd_fd >= 0) {
        assert(!(fcntl(g_dvd_fd, F_GETFD) & FD_CLOEXEC));
        close(g_dvd_fd);
    }
    if (g_dvd_fd_rw >= 0) {
        assert(!(fcntl(g_dvd_fd_rw, F_GETFD) & FD_CLOEXEC));
        close(g_dvd_fd_rw);
    }
    return 0;
}
