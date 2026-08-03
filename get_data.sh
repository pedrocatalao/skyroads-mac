#!/bin/bash
# get_data.sh — fetch the freeware SkyRoads game data from Bluemoon's
# official site into ./data/.  This repo never redistributes the game;
# this downloads it from the source, on your machine, for your use.
set -euo pipefail
cd "$(dirname "$0")"

URL="http://www.bluemoon.ee/history/skyroads/skyroads.zip"
DEST="data"

mkdir -p "$DEST"
echo "Downloading SkyRoads (freeware) from $URL ..."
curl -fL --progress-bar -o "$DEST/skyroads.zip" "$URL"
unzip -o -q "$DEST/skyroads.zip" -d "$DEST"
rm "$DEST/skyroads.zip"

if ! find "$DEST" -maxdepth 1 -iname "roads.lzs" | grep -q .; then
    echo "ERROR: download did not contain the expected game data" >&2
    exit 1
fi
# TimGM6mb SoundFont (GPL, Tim Brechbill / MuseScore) for the wavetable
# music mode; the game falls back to AdLib FM without it.
SF_URL="https://sourceforge.net/p/mscore/code/HEAD/tree/trunk/mscore/share/sound/TimGM6mb.sf2?format=raw"
if [ ! -f "$DEST/TimGM6mb.sf2" ]; then
    echo "Downloading TimGM6mb.sf2 (wavetable instruments, ~6 MB) ..."
    curl -fL --progress-bar -o "$DEST/TimGM6mb.sf2" "$SF_URL" || echo "warning: soundfont fetch failed; music will use AdLib FM"
fi
echo "OK: game data in ./$DEST ($(ls "$DEST" | wc -l | tr -d ' ') files)"
echo "Next: ./make_app.sh $DEST"
