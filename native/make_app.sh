#!/bin/bash
# make_app.sh — package the native SkyRoads port as a macOS .app bundle.
set -euo pipefail

# Resolve the data dir relative to where the user runs the script,
# BEFORE changing directory.
DATA_ARG="${1:-$(dirname "$0")/..}"
if [ ! -d "$DATA_ARG" ]; then
    echo "ERROR: data directory '$DATA_ARG' not found" >&2
    echo "Usage: $0 <path-to-skyroads-game-data>   (e.g. ./native/make_app.sh data)" >&2
    exit 1
fi
DATA_DIR="$(cd "$DATA_ARG" && pwd)"

cd "$(dirname "$0")"
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

# Game data. Filenames in the freeware distribution may be UPPERCASE
# (DOS-style); copy case-insensitively and store as lowercase, which is
# what the engine opens.
copy_data() {  # $1 = filename (lowercase), $2 = "required" | "optional"
    local src
    src=$(find "$DATA_DIR" -maxdepth 1 -iname "$1" | head -1)
    if [ -z "$src" ]; then
        if [ "$2" = required ]; then
            echo "ERROR: required data file '$1' not found in $DATA_DIR" >&2
            echo "Point make_app.sh at your SkyRoads game data directory." >&2
            exit 1
        fi
        echo "note: optional '$1' not found, skipping"
        return
    fi
    cp "$src" "$OUT/Contents/Resources/$1"
}

for f in trekdat.lzs roads.lzs muzax.lzs cars.lzs dashbrd.lzs \
         mainmenu.lzs gomenu.lzs setmenu.lzs helpmenu.lzs intro.lzs \
         sfx.snd speed.dat oxy_disp.dat ful_disp.dat \
         world0.lzs world1.lzs world2.lzs world3.lzs world4.lzs \
         world5.lzs world6.lzs world7.lzs world8.lzs world9.lzs; do
    copy_data "$f" required
done
# not used by the port yet (intro sequence / demo mode)
for f in anim.lzs intro.snd demo.rec; do
    copy_data "$f" optional
done

codesign --force -s - "$OUT"
echo "built: $OUT"
