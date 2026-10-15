#!/usr/bin/env python3
"""Prepare temporary Gradle signing files and verify release artifacts in CI."""

import argparse
import base64
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
PROPERTIES = ROOT / "android/key.properties"


def signing_dir():
    return Path(os.environ["RUNNER_TEMP"]) / "x1box-android-signing"


def property_value(value):
    """Escape Java Properties values, including spaces and non-ASCII passwords."""
    result = []
    for char in value:
        if char in "\\ =:#!":
            result.append("\\" + char)
        elif not 32 <= ord(char) < 127:
            encoded = char.encode("utf-16-be")
            result.extend("\\u" + encoded[i:i + 2].hex()
                          for i in range(0, len(encoded), 2))
        else:
            result.append(char)
    return "".join(result)


def run(command):
    # Capture tool output: keystore errors must never dump credentials to CI logs.
    return subprocess.run(command, capture_output=True, check=False)


def prepare():
    names = ("ANDROID_KEYSTORE_BASE64", "ANDROID_KEYSTORE_PASSWORD",
             "ANDROID_KEY_ALIAS", "ANDROID_KEY_PASSWORD")
    missing = [name for name in names if not os.environ.get(name)]
    if missing:
        raise ValueError("Release signing requires secrets: " + ", ".join(missing))
    if PROPERTIES.exists():
        raise ValueError("Refusing to replace an existing android/key.properties")

    data = base64.b64decode("".join(os.environ[names[0]].split()), validate=True)
    if not data:
        raise ValueError("The release keystore is empty")
    directory = signing_dir()
    directory.mkdir(mode=0o700)
    keystore = directory / "release.jks"
    keystore.write_bytes(data)
    keystore.chmod(0o600)

    key_options = ["-keystore", str(keystore), "-alias", os.environ[names[2]],
                   "-storepass:env", names[1]]
    certificate = run(["keytool", "-exportcert", *key_options])
    private_key = run(["keytool", "-certreq", *key_options,
                       "-keypass:env", names[3]])
    if certificate.returncode or private_key.returncode:
        raise ValueError("Cannot open the signing key; check the keystore, alias and passwords")
    digest = hashlib.sha256(certificate.stdout).hexdigest()
    (directory / "certificate.sha256").write_text(digest + "\n")

    values = {"storeFile": str(keystore), "storePassword": os.environ[names[1]],
              "keyAlias": os.environ[names[2]], "keyPassword": os.environ[names[3]]}
    descriptor = os.open(PROPERTIES, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="ascii") as properties:
        properties.write("# Temporary GitHub Actions release signing\n")
        for name, value in values.items():
            properties.write(name + "=" + property_value(value) + "\n")
    print("Release signing certificate SHA-256: " + digest)


def verify(apk, aab):
    expected = (signing_dir() / "certificate.sha256").read_text().strip()
    apksigner = Path(os.environ["ANDROID_HOME"]) / "build-tools/36.1.0/apksigner"
    result = run([str(apksigner), "verify", "--print-certs", str(apk)])
    match = re.search(rb"Signer #1 certificate SHA-256 digest: ([0-9a-fA-F]+)",
                      result.stdout)
    if result.returncode or not match or match[1].decode().lower() != expected:
        raise ValueError("APK signature verification failed or the signing certificate differs")

    result = run(["jarsigner", "-verify", "-strict", str(aab)])
    # Android signing certificates are normally self-signed (strict exit code 4).
    # The exact certificate is pinned below; unsigned entries (16) still fail.
    if result.returncode not in (0, 4):
        raise ValueError("AAB signature verification failed")
    certificate = run(["keytool", "-printcert", "-jarfile", str(aab), "-rfc"])
    match = re.search(rb"-----BEGIN CERTIFICATE-----\s*(.*?)\s*-----END CERTIFICATE-----",
                      certificate.stdout, re.DOTALL)
    if certificate.returncode or not match:
        raise ValueError("AAB has no signing certificate")
    digest = hashlib.sha256(base64.b64decode(match[1])).hexdigest()
    if digest != expected:
        raise ValueError("AAB signing certificate differs from the release key")
    print("APK and AAB signatures verified; certificate SHA-256: " + expected)


def cleanup():
    if PROPERTIES.is_file() and PROPERTIES.read_bytes().startswith(
            b"# Temporary GitHub Actions release signing\n"):
        PROPERTIES.unlink()
    shutil.rmtree(signing_dir(), ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)
    subcommands.add_parser("prepare")
    subcommands.add_parser("cleanup")
    verify_parser = subcommands.add_parser("verify")
    verify_parser.add_argument("apk", type=Path)
    verify_parser.add_argument("aab", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            prepare()
        elif args.command == "verify":
            verify(args.apk, args.aab)
        else:
            cleanup()
    except (OSError, ValueError) as error:
        if args.command == "prepare":
            cleanup()
        sys.exit("Android signing: " + str(error))


if __name__ == "__main__":
    main()
