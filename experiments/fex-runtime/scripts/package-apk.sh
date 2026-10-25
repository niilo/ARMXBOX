#!/usr/bin/env bash
set -euo pipefail
probe_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
ndk_build=${1:?usage: package-apk.sh NATIVE_BUILD OUTPUT_DIR}
probe_out=${2:?usage: package-apk.sh NATIVE_BUILD OUTPUT_DIR}
: "${ANDROID_HOME:?Set ANDROID_HOME to the Android SDK}"
build_tools="$ANDROID_HOME/build-tools/36.1.0"
platform="$ANDROID_HOME/platforms/android-36/android.jar"
mkdir -p "$probe_out/classes" "$probe_out/staging/lib/arm64-v8a" "$probe_out/assets"
probe_out=$(cd "$probe_out" && pwd)
javac --release 8 -classpath "$platform" -d "$probe_out/classes" "$probe_root/android/org/armxbox/fexprobe/MainActivity.java"
"$build_tools/d8" --min-api 29 --lib "$platform" --output "$probe_out/staging" "$probe_out"/classes/org/armxbox/fexprobe/*.class
cp "$ndk_build/libarmxbox_host_mapping_probe.so" "$probe_out/staging/lib/arm64-v8a/"
cp "$ndk_build/libarmxbox_fex_diagnostic.so" "$probe_out/staging/lib/arm64-v8a/"
# Check the dependency closure before packaging: only Android system libraries
# may remain external. This catches accidental shared fmt/libc++ dependencies.
python3 - "$probe_out/staging/lib/arm64-v8a" "$ANDROID_HOME" <<'PYELF'
from pathlib import Path
import re, subprocess, sys
readelf = Path(sys.argv[2]) / 'ndk/30.0.15729638/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf'
for source in Path(sys.argv[1]).glob('*.so'):
    dynamic = subprocess.check_output([str(readelf), '-d', str(source)], text=True)
    needed = set(re.findall(r'\(NEEDED\).*?\[([^]]+)\]', dynamic))
    unpackaged = needed - {'libc.so', 'libm.so', 'libdl.so', 'liblog.so'}
    if unpackaged:
        raise SystemExit(f'{source.name}: unpackaged native dependencies: {sorted(unpackaged)}')
PYELF
python3 "$probe_root/scripts/notices.py" "${FEX_SOURCE_DIR:?Set FEX_SOURCE_DIR to the prepared pinned checkout}" "$probe_out/assets/THIRD_PARTY_NOTICES"
"$build_tools/aapt2" link -I "$platform" --manifest "$probe_root/android/AndroidManifest.xml" -A "$probe_out/assets" -o "$probe_out/unaligned.apk"
python3 - "$probe_out" <<'PYZIP'
from pathlib import Path
import sys, zipfile
out = Path(sys.argv[1])
with zipfile.ZipFile(out / 'unaligned.apk', 'a', compression=zipfile.ZIP_DEFLATED) as archive:
    for source in sorted((out / 'staging').rglob('*')):
        if source.is_file():
            archive.write(source, source.relative_to(out / 'staging'))
PYZIP
"$build_tools/zipalign" -P 16 -f 4 "$probe_out/unaligned.apk" "$probe_out/aligned.apk"
if [[ ! -f "$probe_out/probe-debug.keystore" ]]; then
  keytool -genkeypair -keystore "$probe_out/probe-debug.keystore" -storepass android -keypass android -alias probe -keyalg RSA -keysize 2048 -validity 3650 -dname 'CN=ARMXBOX Experimental Debug'
fi
"$build_tools/apksigner" sign --ks "$probe_out/probe-debug.keystore" --ks-pass pass:android --out "$probe_out/armxbox-fex-phase0.10.1-debug.apk" "$probe_out/aligned.apk"
"$build_tools/apksigner" verify "$probe_out/armxbox-fex-phase0.10.1-debug.apk"
"$build_tools/zipalign" -c -P 16 4 "$probe_out/armxbox-fex-phase0.10.1-debug.apk"
