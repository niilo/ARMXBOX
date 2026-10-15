#!/usr/bin/env python3
"""Keep Android release versions and their Git tags on the same commit."""

import argparse
from pathlib import Path
import re
import subprocess
import sys


GRADLE_FILE = "android/app/build.gradle.kts"
VERSION = r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
MAX_VERSION_CODE = 2100000000


def git(root, *args, check=True):
    result = subprocess.run(["git", *args], cwd=root, text=True,
                            capture_output=True, check=False)
    if check and result.returncode:
        raise ValueError(f"git {args[0]} failed: {result.stderr.strip()}")
    return result


def read_versions(root):
    text = (root / GRADLE_FILE).read_text(encoding="utf-8")
    codes = list(re.finditer(
        r"(?m)^([ \t]*versionCode[ \t]*=[ \t]*)([0-9]+)"
        r"([ \t]*(?://[^\r\n]*)?)$", text))
    names = list(re.finditer(
        r'(?m)^([ \t]*versionName[ \t]*=[ \t]*")([^"\r\n]+)'
        r'("[ \t]*(?://[^\r\n]*)?)$', text))
    if len(codes) != 1 or len(names) != 1:
        raise ValueError("Expected one literal versionCode and versionName assignment")
    code, name = int(codes[0][2]), names[0][2]
    if not 1 <= code <= MAX_VERSION_CODE or not re.fullmatch(VERSION, name):
        raise ValueError("Expected a positive Android versionCode and an X.Y.Z versionName")
    return text, code, name, codes[0], names[0]


def ensure_tag_available(root, tag):
    if git(root, "show-ref", "--verify", "--quiet", "refs/tags/" + tag,
           check=False).returncode == 0:
        raise ValueError(f"Tag {tag} already exists; choose a different versionName")


def emit(values, output):
    for name, value in values.items():
        print(f"{name}={value}")
    if output:
        with output.open("a", encoding="utf-8") as target:
            for name, value in values.items():
                target.write(f"{name}={value}\n")


def prepare(root, version_name, version_code, dry_run, output):
    if git(root, "status", "--porcelain").stdout.strip():
        raise ValueError("Release preparation requires a clean Git working tree")
    text, old_code, old_name, code_match, name_match = read_versions(root)
    old_parts = tuple(map(int, old_name.split(".")))
    name = version_name or f"{old_parts[0]}.{old_parts[1]}.{old_parts[2] + 1}"
    if not re.fullmatch(VERSION, name):
        raise ValueError("versionName must use the X.Y.Z format, for example 1.2.10")
    if tuple(map(int, name.split("."))) <= old_parts:
        raise ValueError("versionName must be greater than the current Android version")
    if version_code and not re.fullmatch(r"[1-9][0-9]*", version_code):
        raise ValueError("versionCode must be a positive integer")
    code = int(version_code) if version_code else old_code + 1
    if not old_code < code <= MAX_VERSION_CODE:
        raise ValueError("versionCode must increase and cannot exceed 2100000000")
    tag = "v" + name
    ensure_tag_available(root, tag)
    if not dry_run:
        # Replace just the two values, retaining comments and surrounding style.
        for match, value in sorted(((code_match, str(code)), (name_match, name)),
                                   key=lambda item: item[0].start(), reverse=True):
            text = text[:match.start(2)] + value + text[match.end(2):]
        (root / GRADLE_FILE).write_text(text, encoding="utf-8")
    emit({"version_code": code, "version_name": name, "tag": tag}, output)


def check_tag(root, tag):
    _, code, name, _, _ = read_versions(root)
    if tag != "v" + name:
        raise ValueError(f"Release tag must be v{name}, matching Android versionName")
    return code, name


def publish(root, branch, tag, output):
    if not branch.startswith("refs/heads/"):
        raise ValueError("Release commits must target a branch")
    git(root, "check-ref-format", branch)
    code, name = check_tag(root, tag)
    ensure_tag_available(root, tag)
    changed = set(git(root, "diff", "--name-only").stdout.splitlines())
    changed.update(git(root, "diff", "--cached", "--name-only").stdout.splitlines())
    if changed != {GRADLE_FILE}:
        raise ValueError("Only the Android version file may be changed for the release commit")
    git(root, "add", "--", GRADLE_FILE)
    message = f"release: Android {name} (versionCode {code})"
    git(root, "commit", "-m", message)
    git(root, "tag", "-a", tag, "-m", message)
    # A rejected branch/tag update must not leave the other ref published.
    git(root, "push", "--atomic", "origin", "HEAD:" + branch, "refs/tags/" + tag)
    emit({"commit": git(root, "rev-parse", "HEAD").stdout.strip()}, output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    subcommands = parser.add_subparsers(dest="command", required=True)
    plan = subcommands.add_parser("prepare")
    plan.add_argument("--version-name", default="")
    plan.add_argument("--version-code", default="")
    plan.add_argument("--dry-run", action="store_true")
    plan.add_argument("--github-output", type=Path)
    check = subcommands.add_parser("check-tag")
    check.add_argument("tag")
    push = subcommands.add_parser("publish")
    push.add_argument("--branch", required=True)
    push.add_argument("--tag", required=True)
    push.add_argument("--github-output", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            prepare(args.root, args.version_name, args.version_code,
                    args.dry_run, args.github_output)
        elif args.command == "publish":
            publish(args.root, args.branch, args.tag, args.github_output)
        else:
            code, name = check_tag(args.root, args.tag)
            print(f"Validated {args.tag}: versionName={name}, versionCode={code}")
    except (OSError, ValueError) as error:
        sys.exit("Android release: " + str(error))


if __name__ == "__main__":
    main()
