#!/bin/bash
# make_app.sh — build SkyRoads.app.  If the game data isn't present yet, it
# is fetched first from Bluemoon's official site (the game is their freeware;
# this repo never redistributes it — the download happens on your machine).
#
# Usage: ./make_app.sh [data-dir]     (default: ./data, fetched if missing)
set -euo pipefail

DATA_ARG="${1:-$(dirname "$0")/data}"

# ---- fetch the game data if it isn't there yet ----
have_data() { find "$1" -maxdepth 1 -iname "roads.lzs" 2>/dev/null | grep -q .; }

if ! have_data "$DATA_ARG"; then
    if [ "$#" -ge 1 ]; then
        echo "ERROR: no SkyRoads data in '$DATA_ARG'" >&2
        echo "Point make_app.sh at your game data, or run it with no argument" >&2
        echo "to download the freeware release into ./data automatically." >&2
        exit 1
    fi
    URL="http://www.bluemoon.ee/history/skyroads/skyroads.zip"
    mkdir -p "$DATA_ARG"
    echo "Game data not found — downloading SkyRoads (freeware) from $URL ..."
    curl -fL --progress-bar -o "$DATA_ARG/skyroads.zip" "$URL"
    unzip -o -q "$DATA_ARG/skyroads.zip" -d "$DATA_ARG"
    rm "$DATA_ARG/skyroads.zip"
    have_data "$DATA_ARG" || { echo "ERROR: download did not contain the expected game data" >&2; exit 1; }
fi

# TimGM6mb SoundFont (GPL, Tim Brechbill / MuseScore) for the wavetable
# music mode; the game falls back to AdLib FM without it.
if ! find "$DATA_ARG" -maxdepth 1 -iname "TimGM6mb.sf2" | grep -q .; then
    SF_URL="https://sourceforge.net/p/mscore/code/HEAD/tree/trunk/mscore/share/sound/TimGM6mb.sf2?format=raw"
    echo "Downloading TimGM6mb.sf2 (wavetable instruments, ~6 MB) ..."
    curl -fL --progress-bar -o "$DATA_ARG/TimGM6mb.sf2" "$SF_URL" \
        || echo "warning: soundfont fetch failed; music will use AdLib FM"
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
    <key>CFBundleIconFile</key>        <string>SkyRoads</string>
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
# not used by the port yet (demo mode)
for f in anim.lzs intro.snd demo.rec; do
    copy_data "$f" optional
done
# wavetable soundfont
copy_data "TimGM6mb.sf2" optional

# app icon
if [ -f icon.png ]; then
    python3 make_icon.py icon.png "$OUT/Contents/Resources/SkyRoads.icns" \
        && echo "icon: from icon.png" || echo "note: icon generation failed, skipping"
fi

codesign --force -s - "$OUT"
# nudge Finder/LaunchServices so the fresh bundle's icon shows immediately
touch "$OUT"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$OUT" 2>/dev/null || true
echo "built: $OUT"
