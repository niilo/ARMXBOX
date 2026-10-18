#!/bin/bash
set -euo pipefail

# Resize/encode the original masters; requires ImageMagick.
cd "$(dirname "$0")/.."
for variant in release dev; do
  source="assets/branding/armxbox-${variant}.png"
  prefix=armxbox
  res=android/app/src/main/res
  if [ "$variant" = dev ]; then prefix=armxbox-dev; res=android/app/src/dev/res; fi
  mkdir -p "$res/drawable-nodpi" "$res/mipmap-xxxhdpi"
  convert "$source" -resize 512x512 "${res}/drawable-nodpi/armxbox_icon.png"
  convert "$source" -resize 192x192 "${res}/mipmap-xxxhdpi/ic_launcher.png"
  cp "${res}/mipmap-xxxhdpi/ic_launcher.png" "${res}/mipmap-xxxhdpi/ic_launcher_round.png"
  for size in 16 24 32 48 64 128 256 512; do
    convert "$source" -resize "${size}x${size}" "ui/icons/${prefix}_${size}x${size}.png"
  done
  convert "ui/icons/${prefix}_256x256.png" -define icon:auto-resize=256,128,64,48,32,16 "ui/icons/${prefix}-icon.ico"
done
convert assets/branding/armxbox-release.png -resize 512x512 android/app/src/main/ic_launcher-playstore.png
