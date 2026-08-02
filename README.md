# SkyRoads for macOS (Apple Silicon)

A native macOS port of **SkyRoads** (Bluemoon Interactive, 1993) — the classic
DOS space-racing game — rewritten in portable C with SDL2 and running natively
on Apple Silicon. No emulation, no DOSBox.

![status](https://img.shields.io/badge/status-playable-brightgreen)

## Quick start

**1. Get the game data** (not included here — it's Bluemoon's freeware):

Download SkyRoads from the official site, [bluemoon.ee](https://www.bluemoon.ee/history/skyroads/),
and unzip it anywhere, e.g. `~/Downloads/skyroads-data`. Uppercase DOS-style
filenames (`ROADS.LZS`) are fine.

**2. Install the build tools** (one-time):

```bash
brew install cmake sdl2
```

**3. Build the app** — from a clone of this repo:

```bash
git clone https://github.com/pedrocatalao/skyroads-mac.git
cd skyroads-mac
./native/make_app.sh ~/Downloads/skyroads-data
```

**4. Play:**

```bash
open native/build/SkyRoads.app
```

The app is self-contained (game data is copied into the bundle) — you can move
it to `/Applications`. Progress and settings are saved to
`~/Library/Application Support/`.

### Alternative: run from the terminal

```bash
cmake -S native -B native/build
cmake --build native/build --target skyroads
./native/build/skyroads ~/Downloads/skyroads-data
```

## Controls

| Key | Action |
|---|---|
| ← → | steer |
| ↑ ↓ | accelerate / brake |
| Space | jump |
| Enter | select (menus) |
| Esc | abort road / back |

## What's in this repo

- `native/src/` — the port:
  - `render.c` — C rewrite of the original 16-bit assembly 3D road renderer
  - `game_play.c` — the physics/collision/gameplay engine (bit-faithful
    fixed-point math)
  - `assets.c`, `menus.c` — data loaders, menu flow
  - `compat.c` — DOS runtime emulation (segment memory model, LZSS
    decompressor, file I/O)
  - `audio.c` — AdLib music driver on a software OPL2 (Nuked-OPL3, LGPL) +
    digitized sound effects
  - `platform.c` — SDL2 window/input/timing
- `native/docs/trek_blueprint.md` — reverse-engineering notes on the original
  renderer
- `native/make_app.sh` — builds the signed `SkyRoads.app` bundle

## Troubleshooting

- **"required data file … not found"** — the path you gave `make_app.sh` must
  contain the SkyRoads data files (`trekdat.lzs`, `roads.lzs`, `world*.lzs`,
  …). Point it at the folder where you unzipped the freeware download.
- **CMake can't find SDL2** — `brew install sdl2`, then delete
  `native/build` and rebuild.
- **Intel Macs** — should build and run fine too (plain C + SDL2); only
  tested on Apple Silicon.

## Credits & legal

- **SkyRoads** was created by **Bluemoon Interactive** (Ahti Heinla,
  Jaan Tallinn, and team). All game content, art, music and the original
  design are theirs. This is an unofficial fan port, not affiliated with or
  endorsed by Bluemoon; the game itself is distributed by Bluemoon as
  freeware.
- OPL2 FM synthesis via [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3)
  (Nuke.YKT), LGPL-2.1 — see `native/src/opl3.c` for its license header.
