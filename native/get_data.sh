#!/bin/bash
# get_data.sh — fetch the freeware SkyRoads game data from Bluemoon's
# official site into ./data/.  This repo never redistributes the game;
# this downloads it from the source, on your machine, for your use.
set -euo pipefail
cd "$(dirname "$0")/.."

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
echo "OK: game data in ./$DEST ($(ls "$DEST" | wc -l | tr -d ' ') files)"
echo "Next: ./native/make_app.sh $DEST"
