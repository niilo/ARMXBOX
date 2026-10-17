#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute Android-generated arguments through real QEMU startup fragments."""
import argparse
import errno
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def between(source, start, end):
    if source.count(start) != 1 or source.count(end) != 1:
        raise AssertionError(
            f"startup source boundary changed: {start!r}, {end!r}")
    return source.split(start, 1)[1].split(end, 1)[0]


def function(source, signature):
    if source.count(signature) != 1:
        raise AssertionError(
            f"startup function boundary changed: {signature!r}")
    # These C functions close at column zero; nested blocks are indented.
    body = source.split(signature, 1)[1].split("\n}\n", 1)[0]
    return signature + body + "\n}\n"


def run(command, **kwargs):
    return subprocess.run(command, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--android-source", type=Path)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--cxx", default="c++")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    build = (args.build_dir or repo / "build").resolve()
    output = (args.output_dir or build / "tests/xbox/android-startup").resolve()
    output.mkdir(parents=True, exist_ok=True)
    if os.geteuid() == 0:
        raise SystemExit(
            "Run as an unprivileged user to verify real EACCES behavior")

    vl = (repo / "system/vl.c").read_text()
    android_path = args.android_source or (
        repo / "android/app/src/main/cpp/xemu_android.cpp")
    android = android_path.read_text()
    forwarding_start = ("    for (int i = 1; i < argc; i++) {\n"
                        "        if (argv[i] != NULL)")
    fragments = {
        "startup-options.inc": "enum {\n\n#define DEF(option" + between(
            vl, "enum {\n\n#define DEF(option",
            "typedef struct VGAInterfaceInfo"),
        "startup-functions.inc": function(
            vl, "static const QEMUOption *lookup_opt(") + function(
            vl, "static void qemu_read_default_config_file("),
        "startup-first-pass.inc": between(
            vl, "/* first pass of option parsing */",
            "/* second pass of option parsing */"),
        "startup-readconfig.inc": "case QEMU_OPTION_readconfig:" + between(
            vl, "case QEMU_OPTION_readconfig:",
            "#ifdef CONFIG_SPICE\n            case QEMU_OPTION_spice:"),
        "startup-forwarding.inc": forwarding_start + between(
            vl, forwarding_start,
            '    printf("Created QEMU launch parameters: ")'),
        "startup-launch-context.inc": "struct QemuLaunchContext {" + between(
            android, "struct QemuLaunchContext {",
            "static int SDLCALL QemuThreadMain"),
        "startup-android-args.inc": "std::vector<std::string> arg_storage;" +
        between(
            android, "std::vector<std::string> arg_storage;",
            "SDL_Thread* qemu_thread = SDL_CreateThread"),
    }
    for name, source in fragments.items():
        (output / name).write_text(
            "/* Generated from current production source. */\n" + source)

    pkg_cflags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "glib-2.0"], text=True))
    pkg_libs = shlex.split(subprocess.check_output(
        ["pkg-config", "--libs", "glib-2.0"], text=True))
    common = ["-O2", "-g", "-UNDEBUG", "-ffunction-sections", "-fdata-sections",
              "-D_GNU_SOURCE", "-DCOMPILING_SYSTEM_VS_USER",
              f"-I{output}", f"-I{build}", f"-I{repo}", f"-I{repo / 'include'}"]
    reader = output / "startup-config-test"
    builder = output / "android-args-test"
    run(shlex.split(args.cc) + common + pkg_cflags + ["-std=gnu11",
        str(here / "startup-config-test.c"), str(build / "libqemuutil.a"),
        "-Wl,--gc-sections"] + pkg_libs + [
            "-pthread", "-lm", "-o", str(reader)])
    run(shlex.split(args.cxx) + common + ["-std=c++17",
        str(here / "android-args-test.cpp"), "-o", str(builder)])

    cases = 0
    with tempfile.TemporaryDirectory(prefix="xemu-startup-") as temp:
        temp = Path(temp)
        denied = temp / "denied-qemu.conf"
        denied.write_text('[startup-test]\nvalue = "parsed"\n')
        denied.chmod(0)
        try:
            with denied.open():
                raise AssertionError(
                    "permission-denied fixture remained readable")
        except OSError as error:
            assert error.errno == errno.EACCES
        missing = temp / "missing-qemu.conf"
        valid = temp / "explicit.conf"
        valid.write_text('[startup-test]\nvalue = "parsed"\n')
        malformed = temp / "malformed.conf"
        malformed.write_text("invalid configuration\n")

        def check(argv, default, *, success, attempts, groups=0, error=None):
            nonlocal cases
            result = subprocess.run([str(reader)] + argv,
                env={**os.environ, "STARTUP_TEST_DEFAULT_CONFIG": str(default)},
                capture_output=True, text=True)
            assert (result.returncode == 0) == success, result
            assert result.stderr.count(
                "DEFAULT_CONFIG_ATTEMPT:") == attempts, result
            if success:
                assert f"parsed_groups={groups}" in result.stdout, result
            if error:
                assert error in result.stderr, result
            cases += 1

        # Unmodified native/Desktop semantics: denied is fatal, missing ignored,
        # and readable defaults are parsed. An option value is not a switch.
        check([], denied, success=False, attempts=1, error="Permission denied")
        check([], missing, success=True, attempts=1)
        check([], valid, success=True, attempts=1, groups=1)
        check([], malformed, success=False, attempts=1,
              error="no group defined")
        check(["-name", "-no-user-config"], denied,
              success=False, attempts=1, error="Permission denied")

        for tuning in ("0", "1"):
            for mask in range(4):
                result = run([str(builder), str(mask)], capture_output=True,
                             env={**os.environ, "STARTUP_TEST_TUNING": tuning})
                generated = result.stdout.decode().split("\0")
                assert generated[-1] == "" and generated[0] == "xemu"
                generated = generated[1:-1]
                check(generated, denied, success=True, attempts=0)
                check(generated, malformed, success=True, attempts=0)
                check(generated + ["-readconfig", str(valid)], denied,
                      success=True, attempts=0, groups=1)
                check(generated + ["-readconfig", str(denied)], denied,
                      success=False, attempts=0, error="Permission denied")
                check(generated + ["-readconfig", str(missing)], denied,
                      success=False, attempts=0, error="No such file")
        denied.chmod(0o600)
    print(f"Startup option/config cases passed: {cases}; "
          "Android tuning/DVD variants: 8")


if __name__ == "__main__":
    main()
