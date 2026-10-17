/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host fixture for the real QEMU option/config startup code. */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qobject/qdict.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "system/arch_init.h"

/* Compile the real reader, including fopen and its errno/error propagation. */
#include "util/qemu-config.c"
#include "startup-options.inc"

static unsigned default_attempts;
static unsigned parsed_groups;
static QDict *machine_opts_dict;

static char *startup_test_config_path(const char *compiled_dir)
{
    const char *path = getenv("STARTUP_TEST_DEFAULT_CONFIG");

    g_assert_nonnull(path);
    g_assert_true(g_str_has_suffix(compiled_dir, "/qemu.conf"));
    default_attempts++;
    fprintf(stderr, "DEFAULT_CONFIG_ATTEMPT: %s\n", path);
    return g_strdup(path);
}

/*
 * Machine configuration application is outside this fixture. Verify that
 * the real reader parsed the supplied section and value before invoking us.
 */
static void qemu_parse_config_group(const char *group, QDict *dict,
                                    void *opaque, Error **errp)
{
    g_assert_cmpstr(group, ==, "startup-test");
    g_assert_cmpstr(qdict_get_str(dict, "value"), ==, "parsed");
    parsed_groups++;
}

#define get_relocated_path startup_test_config_path
#include "startup-functions.inc"
#undef get_relocated_path

int main(int argc, char **argv)
{
    bool userconfig = true;
    int optind;
    const char *optarg;
    int fake_argc = 1;
    char **fake_argv = g_new0(char *, argc + 1);

    error_init(argv[0]);
    fake_argv[0] = argv[0];
    /* Exercise the actual argument-forwarding loop in qemu_init as well. */
#include "startup-forwarding.inc"
    argc = fake_argc;
    argv = fake_argv;

    /* Exact first-pass body, including the conditional default config read. */
#include "startup-first-pass.inc"

    /*
     * Limit the second pass to its actual -readconfig case. Other option
     * effects need the complete machine, but lookup_opt consumes them here.
     */
    optind = 1;
    while (optind < argc) {
        const QEMUOption *popt;

        if (argv[optind][0] != '-') {
            optind++;
            continue;
        }
        popt = lookup_opt(argc, argv, &optarg, &optind);

        switch (popt->index) {
#include "startup-readconfig.inc"
        default:
            break;
        }
    }
    printf("default_attempts=%u parsed_groups=%u\n",
           default_attempts, parsed_groups);
    qobject_unref(machine_opts_dict);
    g_free(fake_argv);
    return 0;
}
