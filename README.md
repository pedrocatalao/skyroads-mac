# SkyRoads for macOS (Apple Silicon)

A native macOS port of **SkyRoads** (Bluemoon Interactive, 1993) — the classic
DOS space-racing game — rewritten in portable C with SDL2 and running natively
on Apple Silicon. No emulation, no DOSBox.

![status](https://img.shields.io/badge/status-playable-brightgreen)

## What's here

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
- `native/make_app.sh` — builds a signed `SkyRoads.app` bundle

## What's NOT here

**No game data and none of the original code.** You need the original
SkyRoads data files (`*.lzs`, `*.snd`, `*.dat`), which Bluemoon released as
freeware in 2003 — get the game from the official site (bluemoon.ee) and
point the build at it.

## Building

Requires: Xcode command-line tools, Homebrew `cmake` and `sdl2`.

```bash
brew install cmake sdl2
./native/make_app.sh /path/to/skyroads-data
open native/build/SkyRoads.app
```

Or run windowed from a terminal:

```bash
cmake -S native -B native/build && cmake --build native/build --target skyroads
./native/build/skyroads /path/to/skyroads-data
```

Game progress/config is saved to `~/Library/Application Support/`.

## Controls

| Key | Action |
|---|---|
| ← → | steer |
| ↑ ↓ | accelerate / brake |
| Space | jump |
| Esc | abort road / back |

## Credits & legal

- **SkyRoads** was created by **Bluemoon Interactive** (Ahti Heinla,
  Jaan Tallinn, and team). All game content, art, music and the original
  design are theirs. This is an unofficial fan port, not affiliated with or
  endorsed by Bluemoon; the game itself is distributed by Bluemoon as
  freeware.
- OPL2 FM synthesis via [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3)
  (Nuke.YKT), LGPL-2.1 — see `native/src/opl3.c` for its license header.
