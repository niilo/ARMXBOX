#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare real cached native-policy code with the released implementation."""
import argparse
import json
from pathlib import Path
import resource
import shlex
import subprocess


BASELINE = 'e643007d12d2f88ef1d1e8782d717ad999afc7ff'
ROOT = Path(__file__).resolve().parents[3]
SOURCE = 'target/i386/tcg/fpu_helper.c'
HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define unlikely(x) __builtin_expect(!!(x), 0)
static const char *setting;
static int reads;
static bool x87_native_enabled(void);
static void observe_read(void)
{
    reads++;
    /* The old cache publishes its default before querying external code. */
    assert(x87_native_enabled());
}
#ifdef __ANDROID__
#include <sys/system_properties.h>
int __system_property_get(const char *name, char *value)
{
    assert(!strcmp(name, "debug.xemu.tcg.native_x87"));
    observe_read();
    if (!setting) {
        *value = '\0';
        return 0;
    }
    strcpy(value, setting);
    return strlen(value);
}
#else
static char *policy_getenv(const char *name)
{
    assert(!strcmp(name, "XEMU_TCG_NATIVE_X87"));
    observe_read();
    return (char *)setting;
}
#define getenv policy_getenv
#endif
#include "policy.inc"
int main(int argc, char **argv)
{
    assert(argc == 3);
    setting = !strcmp(argv[1], "<unset>") ? NULL : argv[1];
    bool expected = atoi(argv[2]);
    assert(x87_native_enabled() == expected);
    assert(reads == 1);
    /* Changing the external setting must not change the cached policy. */
    setting = expected ? "0" : "1";
    for (int i = 0; i < 10000; i++) {
        assert(x87_native_enabled() == expected);
    }
    assert(reads == 1);
    puts("PASS cached policy and reentrant initialization");
}
'''


def policy(source):
    begin = source.index('/*\n * Whether x87 arithmetic')
    end = source.index('/* Precision control double', begin)
    return source[begin:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--output-dir', type=Path, default=Path('build/x87-policy'))
    args = parser.parse_args()
    out = args.output_dir.resolve()
    (out / 'sys').mkdir(parents=True, exist_ok=True)
    (out / 'sys/system_properties.h').write_text(
        '#define PROP_VALUE_MAX 92\n'
        'int __system_property_get(const char *, char *);\n')
    (out / 'test.c').write_text(HARNESS)
    original = policy(subprocess.check_output(
        ['git', 'show', BASELINE + ':' + SOURCE], cwd=ROOT, text=True))
    actual = policy((ROOT / SOURCE).read_text())
    flags = shlex.split(args.cc) + [
        '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
        '-UNDEBUG', '-I' + str(out)]
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                  '-fno-pie', '-no-pie']
    cases = [('<unset>', 1), ('', 1), ('0', 0), ('00', 0), ('0false', 0),
             ('1', 1), ('false', 1), ('true', 1), (' 0', 1), ('x' * 91, 1)]
    results = []

    def check(label, text, negative=False):
        (out / 'policy.inc').write_text(text)
        for android in (False, True):
            name = label + ('-android' if android else '-desktop')
            binary = out / name
            subprocess.run(flags + (['-D__ANDROID__'] if android else []) +
                           [str(out / 'test.c'), '-o', str(binary)], check=True)
            detected = False
            for setting, expected in cases:
                result = subprocess.run(
                    [str(binary), setting, str(expected)], capture_output=True,
                    text=True, preexec_fn=lambda: resource.setrlimit(
                        resource.RLIMIT_CORE, (0, 0)))
                if negative:
                    if result.returncode and 'Assertion' in result.stderr:
                        detected = True
                        break
                elif result.returncode:
                    raise RuntimeError(name + ': ' + repr(setting) + '\n' +
                                       result.stdout + result.stderr)
            if negative:
                assert detected, name + ' was not detected'
            results.append({'name': name, 'negative_detected': detected,
                            'cases': None if negative else len(cases)})
            print('PASS', name, 'negative detected' if negative else
                  str(len(cases)) + ' fresh-process settings', flush=True)

    check('release', original)
    check('current', actual)
    if args.negative_control:
        reread = actual.replace('if (unlikely(enabled < 0))', 'if (true)')
        # The assertion in observe_read detects both premature querying and
        # re-reading. A recursion guard lets this mutant terminate cleanly.
        harness = HARNESS.replace('    observe_read();',
            '    if (!reads) { observe_read(); } else { reads++; }')
        (out / 'test.c').write_text(harness)
        wrong = actual.replace("env[0] == '0'", "env[0] == '1'").replace(
            "value[0] == '0'", "value[0] == '1'")
        assert reread != actual and wrong != actual
        check('reread', reread, True)
        (out / 'test.c').write_text(HARNESS)
        check('wrong-setting', wrong, True)
    (out / 'policy.inc').write_text(actual)
    (out / 'test.c').write_text(HARNESS)
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
