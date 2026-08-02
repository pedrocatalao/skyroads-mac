#!/bin/bash
# make_app.sh — package the native SkyRoads port as a macOS .app bundle.
set -euo pipefail
cd "$(dirname "$0")"

DATA_DIR="${1:-..}"          # original game data location
OUT="build/SkyRoads.app"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build --target skyroads -j >/dev/null

rm -rf "$OUT"
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"

cat > "$OUT/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>      <string>skyroads</string>
    <key>CFBundleIdentifier</key>      <string>local.skyroads-native-port</string>
    <key>CFBundleName</key>            <string>SkyRoads</string>
    <key>CFBundleDisplayName</key>     <string>SkyRoads</string>
    <key>CFBundlePackageType</key>     <string>APPL</string>
    <key>CFBundleShortVersionString</key> <string>1.0</string>
    <key>NSHighResolutionCapable</key> <true/>
</dict>
</plist>
PLIST

cp build/skyroads "$OUT/Contents/MacOS/skyroads"

# game data: everything the English retail flow loads
for f in trekdat.lzs roads.lzs muzax.lzs cars.lzs dashbrd.lzs \
         mainmenu.lzs gomenu.lzs setmenu.lzs helpmenu.lzs intro.lzs anim.lzs \
         intro.snd sfx.snd speed.dat oxy_disp.dat ful_disp.dat demo.rec \
         world0.lzs world1.lzs world2.lzs world3.lzs world4.lzs \
         world5.lzs world6.lzs world7.lzs world8.lzs world9.lzs; do
    cp "$DATA_DIR/$f" "$OUT/Contents/Resources/"
done

codesign --force -s - "$OUT"
echo "built: $OUT"
