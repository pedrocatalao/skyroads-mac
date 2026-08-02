# Blueprint: C port of `trek.asm` — the SkyRoads 3D road renderer

Sources: `/Users/pedro/Downloads/skyroads/trek.asm`, `/Users/pedro/Downloads/skyroads/makeroad/makeroad.c`, `/Users/pedro/Downloads/skyroads/trek1.c`. VGA (`Vga=1`) path only; all EGA code (`egacopy/egadrwl/egadrwr/egacar/egashd/convega`, `Mirror`, `LeftEndMask`/`RightEndMask`, the `ega*` fields of `ColInfo`) is dead for the port and noted only in passing.

---

## 0. Coordinate systems and constants (trek.asm:37-69)

| Constant | Value | Meaning |
|---|---|---|
| `LINE0` | 32 | Road spans are drawn into `page + LINE0*320` (trek.asm:365-371): road-generator y `0..125` maps to absolute screen y `32..157` (in practice ≤137, see `xlim`). Absolute lines 0..31 are sky. |
| `MINX` / `MAXX` | 110 / 429 | Horizontal *world* coordinate range of the 320-px viewport: `screen_x = world_x − MINX`. Road center = world 270 = screen 160. |
| `CARW` / `CARH` | 29 / 24 | Car sprite is **29 wide × 24 tall** (stored column-major, see §2.7). |
| `GROUNDZ` | 80 | `z` passed when the ship rests on the road surface. |
| `MINZ` / `MAXZ` | 20 / 157 | `MINZ = GROUNDZ−CARH−36`, `MAXZ = MINZ−1+138`. Car top absolute screen y = `MAXZ − z` ∈ [0,137]. The viewport is exactly 138 lines (abs y 0..137); the dashboard owns y ≥ 138 and is never touched by `video()`. |
| `ROOFZ` | 100 | `GROUNDZ+20`: z of wall/tunnel roofs (matches makeroad's 20-unit wall height `H_`, makeroad.c:22). Used by game physics, not by the renderer itself. |
| `PLATEDEPTH` | 7 | Plate front-face depth in world units (matches `D_`, makeroad.c:23). |
| `ROWS` / `VIRTUAL_ROWS` | 11 / 13 | 11 visible road rows; index rows 11 and 12 are the split halves of the ground row (§1.2). |
| `GROUNDROWS` | 4 | When the row counter hits 4, the row being drawn is the one containing the ground line / the car. |
| `COLS` | 7 | Road columns per row. |
| `PHASES` / `PHAS` | 8 / 3 | 8 scroll sub-steps per road row; `phase = Y & 7`, `row = Y >> 3`. |
| `ELEMENTS` | 20 | Span-list records per (virtual row, half-column). |
| `SHADOWS`/`SHDCONST`/`SHDH`/`SHDZDIF` | 5 / 5 / 9 / 8 | 5 shadow shapes, each 9×29; shape index = `ShadowH/5` (≥5 ⇒ no shadow); shadow top = car bottom − 8 + `ShadowH` lines. |
| `INDEXSIZE` | 624 | `13*4*sizeof(table_t = 6 u16)` — the per-phase index table. |
| `PLATEW` | 46 | Slab width in px at ground depth (`W_`, makeroad.c:21). |
| Types | | `TUNNEL=1, WALL=2, DWALL=4` (arch=3, darch=5 have no named equ). |
| Colors | | `WALLROOFCOL=61, WALLRIGHTCOL=64, PLATESIDECOL0=31, PLATEFRONTCOL0=16, ARCHINSIDECOL=65, TUNNELINSIDECOL=67` (trek.asm:64-69). |

**Palette layout (72 stage colors, per skyproj.txt):** 1..15 plate-top colors `C`; 16..30 plate fronts (`15+C`); 31..45 plate left-side faces (`30+C`); 46..60 plate right-side faces *and* shadow-darkened variants of 1..15 (`+45`); 61 wall roof (default); 62 wall/arch fronts; 63 wall left side, 64 wall right side; 65 arch inside; 66 tunnel front; 67 tunnel inside; 68..73 the six tunnel-roof shading bands.

**`Road_Dat`**: `u16 Road_Dat[500][7]`, each word `0x0ABC`: `A` = type (0 plate, 1 tunnel, 2 wall, 3 arch, 4 dwall, 5 darch), `B` = roof color (0 ⇒ default 61), `C` = plate color (0 ⇒ no plate/nothing at all for type 0 cells' slab) (trek1.c:40-49).

**Parameter meanings** (`video(x,y,z,carptr,allscreen,shadowh,videopageseg)`, trek.asm:339):

- `x` — car sprite **left edge in world pixels**, `[MINX..MAXX−CARW]`. The road never pans; only car/shadow/mask use `x`. game.c keeps x as 16.16 fixed in slab units (`BEG_X = 3<<16` = middle slab of 7); conversion is `x_pix ≈ MINX + ((x_fx * PLATEW) >> 16)` plus a small constant centering the 29-px sprite in the 46-px slab (one game unit 65536 = one slab = 46 px; column k occupies world x `[MINX + k*46, MINX + (k+1)*46)`).
- `y` — road progress in 1/8-row steps: **base road row = `y>>3`**, **phase = `y&7`** selecting one of the 8 pre-rendered phase blocks (trek.asm:374-384). Drawn road window: rows `(y>>3)−3 … (y>>3)+7` (far row first). Element code also reads the "front" (nearer) neighbor `cell − COLS`, so `Road_Dat` needs 4 rows of margin below the lowest index used.
- `z` — car altitude; **car top absolute screen y = `MAXZ − z`** (trek.asm:1039-1043). `GROUNDZ`=80 ⇒ car bottom lands on the ground line (abs y ≈ 101; makeroad `GROUNDY = yy[57] ≈ 69.9`, + `LINE0`). `MINZ`/`MAXZ` bound the visible z range (car top y 137..0).
- `carptr` — far pointer to 29×24 sprite, **column-major** (29 columns × 24 bytes each), 0 = transparent (see `Cardat`, trek1.c:8-38, written as 29 source lines of 24 = one column per line).
- `allscreen` — "force full redraw" flag; the game sets it on the first frame and **while the ship is inside a tunnel** (the dirty-fragment tracker can't handle tunnel interiors). trek1.c:269 passes `(c=='q' || !i)`.
- `shadowh` — ship's height above the road surface directly beneath (surface-relative z); drives shadow shape and position only.
- `videopageseg` — **off-screen work page** (64000 bytes; trek1.c:256 allocates it). VGA pipeline: pristine `Background_Seg` → work page → VRAM `0xA000`.

---

## 1. `initvid` and the phase data

### 1.1 What `initvid` does (trek.asm:304-334)

For VGA it does exactly two things: `call expand`, then set the function pointers `CopyBackround=vgacopy, DrawLeft=vgadrwl, DrawRight=vgadrwr, Car=vgacar` (trek.asm:309-314). **It computes no tables at runtime** — everything else is assembled static data (§1.3). (EGA additionally runs `convega`, trek.asm:1913-1961, which rewrites every expanded line triplet into `(xbyte, beginmask, restlen)` planar form and shifts bases by 3 — irrelevant to the port.)

### 1.2 Phase block layout and `expand` (trek.asm:1966-2001) — byte-level

`trekdat.lzs` (built by makeroad.c) holds 8 phase blocks. Per block the loader (trek1.c:238-247) reads `memlen` (expanded size) and `disklen` (packed size), allocates `memlen` bytes, stores the u16 value `memlen−disklen` at offset 0, and LZSS-decompresses the packed block to offset `memlen−disklen`. `PicDatSegments[8]` (trek.asm:121) holds the 8 segments.

Packed block layout (makeroad.c:254-271): `Index[624 bytes]` then 1040 packed records.

- **Packed record (input to expand)**: `color:1, base:u16, (x2:1, width:1)*, 0xff`.
- **Expanded record (output)**: `color:1, base:u16, (x2:1, width:1, 0x00:1)*, 0xff`.

`base = y*320 + 160` for the record's first line (`y` = makeroad screen y, 0..125). Each subsequent pair/triplet is the next line down (`y+1`, i.e. +320). `x2` = span right extent and `width = x2 − x1`, both measured **leftward from screen center** in the canonical (left-half) orientation. Zero-width lines occur mid-record as `(x2, 0)`; trailing zero-width lines were trimmed at build time (`LastElPtr`, makeroad.c:239-248). An element with no visible lines is just `color, base, 0xff`.

`expand` converts each block **in place** (dest cursor from offset 0 trails the src cursor from `memlen−disklen`):

1. copy the 624-byte index to offset 0 (`INDEXSIZE/2` movsw, trek.asm:1977-1978);
2. for each of `13*4*20 = 1040` records: copy the 3 header bytes; then per line copy `x2`; if `x2 == 0xff` the record ends; otherwise copy `width` and **append a 0x00 filler** (trek.asm:1980-1989).

The Index values written by makeroad (`ElPtr + sizeof(Index)`, makeroad.c:226) already count the fillers, so they are valid offsets into the *expanded* block. The filler makes every line entry exactly 3 bytes (uniform stepping for `skip`/`copyfrag`, and room for the in-place EGA conversion).

**Index table**: `u16 Index[13][4][6]`, filled Phase→Row(0..12)→Col(0..3)→the 6 records flagged `index=1` in `El[]` (makeroad.c:82-103, 224-226). The 6 entries map 1:1 to `table_t` (trek.asm:18-25):

| `table_t` field | first record | chained records after it (drawn/skipped sequentially) |
|---|---|---|
| `slab` | #0 plateroof | #1 plateside, #2 platefront |
| `inside` | #3 inside | — |
| `outside` | #4 wallroof | #5 wallside |
| `front` | #6 wallfront (62) | #7 archfront1, #8 archfront2 (62) |
| `tunnel` | #9 tunnelroof1 (68) | #10-14 tunnelroof2..6 (69..73), #15-16 tunnelfront1/2 (66) |
| `dwall` | #17 dwallroof (61) | #18 dwallside (63), #19 dwallfront (62) |

Baked record colors are the `El[].color` values (makeroad.c:82-103); the renderer **pokes** record byte 0 before drawing the colorable faces (§2.4).

**Geometry that produced the spans** (needed only to regenerate the data): row boundary table `yy[n] = YMAX − (YMAX−YMIN)·(atan(LEN0+EDGEPOINT−(n−1)/8)−atan(LEN0)) / (atan(LEN0+EDGEPOINT)−atan(LEN0))`, `YMIN=2, YMAX=126, LEN0=1.25, EDGEPOINT=8` (makeroad.c:137-145). Row far/near edges `y02/y12` per phase (makeroad.c:162-175: `y02 = Row==0 ? YMIN : yy[(Row−1)*8+1+Phase]`, `y12 = Row==ROWS−1 ? yy[87] : yy[Row*8+1+Phase]`); column x extents linearly interpolated between horizon width `W0=1` and ground width `W_ = 46·(YMAX−YMIN)/(GROUNDY−YMIN)` (makeroad.c:177-197); wall top `y01 = y02 − dif0/W·H`, dwall top `y00 = y02 − 2·dif0/W·H`, plate bottom `y03 = y02 + dif0/W·D` (makeroad.c:198-203). `GROUNDY = yy[57] ≈ 69.9`. Col 3 is the **center** column (its shape is symmetric about x=0 and drawn by both halves, makeroad.c:177-185); col 0 is outermost. Virtual rows 11/12 are the `[UpperY, GROUNDY]` / `[GROUNDY, LowerY]` halves of real row 7 — the row that straddles `GROUNDY` for every phase — with front faces suppressed on row 11 when `Phase≠0` (`NoFront`, makeroad.c:171-175). Spans are clipped per line by `xlim[y]` (`x1 = max(x1, xlim[y])`, `x2 = min(x2, 160)`, makeroad.c:147-156): `xlim` is 0 for y < 97, `{89,111,123,133,138,143,148,153,158}` for y = 97..105, and 160 (full cut) for y ≥ 106 (makeroad.c:48-57) — i.e. the road never draws below abs screen y 137, and abs lines 129..137 are carved around the dashboard's raised center hump.

### 1.3 Static tables (assembled data — become `const` arrays in C)

- **`ColInfo[74]`** (trek.asm:130-203): per record-color `c`, the actual VGA color for the left half (`vgaleft`) and mirrored right half (`vgaright`). Formulas: `0..30 → (c, c)`; `31..45 → (c, c+15)` (plate left side face vs. differently-lit right side face); `61→(61,61)`, `62→(62,62)`, **`63→(63,64)`** (wall left/right side), `65→(65,65)`, `66→(66,66)`, `67→(67,67)`; tunnel-roof bands `68..73 → left {71,70,69,68,69,70}, right {70,69,68,69,70,71}` (keeps the light direction consistent when mirrored).
- **`XLimits[138]`** (trek.asm:205-206): indexed by absolute screen y (`MAXZ − z`); 0 for y ≤ 128, `{89,111,123,133,138,143,148,153,158}` for y = 129..137. Nonzero entries define the dashboard hump: on those lines the car/shadow mask is invisible inside world x `[270−XLimits[y], 270+XLimits[y])` (mirrors makeroad's `xlim`).
- **`ShdShapes[5][9][29]`** (trek.asm:241-289): five 1-bit shadow silhouettes, largest (on ground) to smallest (highest).
- **`HeightTab[8] = {1,2,3,3,4,4,1,1}`** (trek.asm:291): road type → stack height (plate 1, tunnel 2, wall/arch 3, dwall/darch 4). Used only by the dirty-fragment logic.
- **`ElmDraw[16]`** (trek.asm:293-295): type → constructor: `{plate, tunneld, wall, arch, dblwall, darch, nilfn×10}`.

---

## 2. `video()` control flow (trek.asm:339-463)

### 2.1 Setup

Copy the 7 parameters to globals (trek.asm:346-361). Call `CopyBackround` = `vgacopy` → `copydif(pass=0)` (§2.8): restores background into the work page (differentially, or fully). Then `es = page + LINE0*320` for road drawing (trek.asm:365-371); `bp = &Road_Dat[((Y>>3)+ROWS−4)*COLS]` — the **farthest** drawn road row (trek.asm:373-379); `ds = PicDatSegments[Y & 7]` — the phase block; `di = 0` (index cursor); `Half = 0` (trek.asm:381-386).

### 2.2 Row / side / column loop (trek.asm:388-438) — painter's order

Rows are drawn **far → near** (row counter `Rows = 11 … 1`; index-table virtual row = `11 − Rows`; road row = `(Y>>3)+7 − (11−Rows)`, i.e. one row of `Road_Dat` *lower* per screen row, trek.asm:435). Per row:

- **Ground-row substitution**: when `Rows == GROUNDROWS(4)` (index row 7 — the row containing the ground line), `di` is saved and replaced by virtual row **11** (`Half=0`, above-ground half) or **12** (`Half=1`, below-ground half) (trek.asm:393-399). After both sides of the `Half=0` pass, the road pointer is rewound, **the car (and its shadow) is drawn** (`call Car` → `vgacar`, trek.asm:424-432), then the same road row is redrawn with `Half=1`. Paint order overall: rows 11..5, ground-row upper half, **car+shadow**, ground-row lower half, rows 3..1.
- **Two side passes** per row (`Side=0` left with `Draw=vgadrwl`, then `Side=1` right with `Draw=vgadrwr`): the index cursor covers 4 half-columns `c = 0..3` (0 = outermost, 3 = center; `di += sizeof(table_t)` per column, reset −4 tables between sides, trek.asm:401-422). Road words visited: left pass columns **0,1,2,3**; right pass columns **6,5,4,3** (`bp` +2 per column, net −2 on the right pass; trek.asm:408-417). Center column 3 is drawn by *both* passes.
- Per cell: `type = (Road_Dat_word >> 8) & 0xF`; `call ElmDraw[type]` (trek.asm:403-406).

**Mirroring**: every span list is stored once (left-half orientation, x measured leftward from center). `vgadrwl` draws it left of x=160, `vgadrwr` reflects it to the right and uses `ColInfo[].vgaright` instead of `.vgaleft`, so one data set serves both halves.

### 2.3 Neighbor conventions used by the constructors

- **Front cell** = `cell − COLS` (one road row nearer; drawn *later*). A face is drawn only if the front cell doesn't hide it.
- **Side neighbor** = the cell **toward screen center**: `cell+1` on the left pass, `cell−1` on the right pass (computed as `bp + (2 − 4*Side)` bytes; trek.asm:480-487, 527-535, etc.). A side face is hidden if that neighbor is tall enough.

### 2.4 Element constructors (trek.asm:470-723)

All operate on the record chains of §1.2; `Draw` consumes a record (advancing `si` past its 0xFF), `skip` (trek.asm:942-951) advances past one record without drawing. Colorable faces are poked into the record's color byte before drawing. With `C = word&0xF` (plate color), `B = (word>>4)&0xF` (roof color), `ft` = front cell's type, `st` = side neighbor's type:

- **plate (type 0)** (trek.asm:470-506): if `C==0` do nothing at all. Draw `slab` chain: roof with color `C`; side face with `30+C` iff side neighbor's `C==0` (else skip record); front face with `15+C` iff front cell's `C==0`.
- **tunneld (type 1)** (trek.asm:700-723): `plate()`; if `ft < TUNNEL`: draw `inside` with **67**; draw the 6 `tunnel` roof records (baked 68..73); if `ft < TUNNEL`: draw the two tunnel fronts (baked 66).
- **wall (type 2)** (trek.asm:510-540): `plate()`; if `ft < WALL`: draw `front` (wallfront, baked 62); draw `outside` roof with `B ? B : 61`; if `st < WALL`: draw wallside (baked 63 → 63 left / 64 right).
- **arch (type 3)** (trek.asm:544-581): `plate()`; if `ft < WALL`: draw `inside` with **65**; roof `B?B:61`; wallside if `st < WALL`; if `ft < WALL`: from `front`, **skip** wallfront then draw archfront1 + archfront2 (62).
- **dblwall (type 4)** (trek.asm:585-635): `plate()`; wallfront if `ft < WALL`; from `outside` **skip** wallroof, draw wallside (lower side) if `st < WALL`; draw `dwall` roof with `B?B:61`; dwallside if `st < DWALL` else skip; dwallfront if `ft < DWALL`.
- **darch (type 5)** (trek.asm:639-696): `plate()`; inside (65) if `ft < WALL`; from `outside` skip wallroof, wallside if `st < WALL`; if `ft < WALL`: from `front` skip wallfront, draw archfront1+2; `dwall` roof `B?B:61`; dwallside if `st < DWALL` else skip; dwallfront if `ft < DWALL`.

### 2.5 Span blitting — `vgadrwl` / `vgadrwr` (trek.asm:846-938)

Common: read record color `c`, map through `ColInfo`, read `base` (= `y*320+160`), then per 3-byte line entry `(x2, w, 0)` until 0xFF, advancing `base += 320` per line. Destination is `page + LINE0*320`.

- **Left** (trek.asm:846-888): fill `w` bytes at `[base − x2, base − x2 + w)` → screen pixels `[160−x2, 160−x1)` on abs line `y+32`.
- **Right** (trek.asm:892-938): fill `w` bytes ending at `base − 1 + x2` (backward `std` fill) → screen pixels `[160+x1, 160+x2)`. Exact mirror.

### 2.6 Car mask — `carmask` (trek.asm:1080-1165)

`CarMask[(CARH+SHDH)*CARW]` = 33 rows × 29 cols, row-major; 0 = clipped, 1 = visible, 2 = pixel actually drawn this frame. Cleared, then per row `r` with `z = Z − r` (after the 24 car rows, `z` drops an extra `ShadowH − SHDZDIF` and the XLimits index advances equally, trek.asm:1148-1157 — so shadow row 0 sits at `z = Z − 16 − ShadowH`):

- rows with `z > MAXZ` or `z < MINZ` stay 0 (off-viewport → also guarantees nothing is drawn below abs y 137 into the dashboard);
- otherwise the visible world-x interval starts as `[MINX, MAXX+1)`; if `XLimits[MAXZ−z] = xl ≠ 0` (bottom 9 lines): if `X ≥ 270−xl` the interval becomes `[270+xl, MAXX+1)`, else `[MINX, 270−xl)` — the car disappears behind the dashboard hump (trek.asm:1102-1122);
- the interval is intersected with `[X, X+CARW)` and those mask cells set to 1 (trek.asm:1124-1143).

### 2.7 Car and shadow — `vgacar` / `vgashd` (trek.asm:1027-1076, 1244-1302)

`vgacar` (note: `es = VideoPageSeg`, **no** LINE0 shift): `CarOfs = (MAXZ−Z)*320 + X − MINX`. Walk the sprite **column-major** (outer loop 29 columns, inner 24 rows, trek.asm:1050-1066): for each nonzero sprite byte whose mask cell is 1, set mask = 2 and store the byte. Then `vgashd`:

- shape = `ShdShapes[ShadowH / SHDCONST]`; if index ≥ `SHADOWS` (5) → no shadow (trek.asm:1249-1254);
- `ShdOfs = (MAXZ−Z + CARH − SHDZDIF + ShadowH)*320 + X − MINX` (shadow top = car top + 16 + ShadowH, trek.asm:1260-1269);
- for each shape bit set whose shadow-mask cell (`CarMask[CARH*CARW + …]`) is nonzero: set it to 2 and **darken the framebuffer pixel in place**: `61 → 64`; `1..15 → +45` (into the 46..60 dark range); 0 and ≥16 unchanged (trek.asm:1275-1290). No sprite data — the shadow is a palette remap of what's under it.

### 2.8 Background restore and present — `copydif` (trek.asm:1334-1500)

`video` runs `copydif` twice: **pass 0** at entry (`vgacopy`, trek.asm:1334-1340; src=`Background_Seg`, dst=work page, fragment finder `findfrg0`) restores background over everything last frame dirtied; **pass 1** at exit (trek.asm:440-441; src=work page, dst=`0xA000`, finder `findfrg1`) pushes the changes to VRAM. Dispatcher logic (trek.asm:1369-1398):

- `AllScreen ≠ 0` → `copyall`: bulk copy abs lines 0..137 (with AllScreen); the non-AllScreen `copyall` variant copies 32..137 only (the sky is static) — then `copycar` (trek.asm:1487-1498).
- `Y == LastY` → only `copycar`. `Y − LastY ≥ PHASES` → `copyall`.
- else differential: `IndexOfsDif = (Y>>3 == LastY>>3) ? one virtual row (48 bytes) : 0` (trek.asm:1381-1388); `LastIndexSeg/IndexSeg` = phase blocks of `LastY&7` / `Y&7`; then for rows 11..2, columns center→outward, both sides, gather from `Road_Dat`: `Color(C), Color1(B), Height=HeightTab[type]` of the cell, the same for the front cell (`Front*`), side neighbor (`SideColor/SideHeight`), and the diagonal front-side cell (`DiagHeight`) (trek.asm:1412-1464), and call the finder (conditions transcribed verbatim in §3.1).

`copyfrag(code)` (trek.asm:1728-1852): `code = f<<8 | n` (+`0x8000`): field `f` selects the `table_t` entry (0 slab, 2 outside, 3 front, 4 tunnel, 5 dwall), `n` = how many records to step past within its chain. It looks the record up in **both** the last phase block (slot + `IndexOfsDif`) and the current one; without bit 15 it skips the new record's leading lines while `new_base < old_base` (copying only the sliver exposed by scroll motion); then copies each remaining line's span `(±x2, w)` at `LINE0*320 + base` from src to dst, word-aligned (the alignment may copy 1 extra src pixel per end — harmless since src is authoritative).

`copycar` (trek.asm:1856-1908): using `copysprite` (copy bytes where mask == 2, stride 320), pass 0 erases the previous car+shadow (regions `LastCarOfs`/`LastShdOfs` masked by `LastCarMask`); pass 1 does those *and* the current `CarOfs`/`ShdOfs` with `CarMask`.

Epilogue (trek.asm:443-455): `LastY=Y; LastCarOfs=CarOfs; LastShdOfs=ShdOfs; LastCarMask=CarMask`.

**Port note**: with a modern linear framebuffer, the entire §2.8 machinery is an optimization. A faithful, simpler port = always take the `AllScreen` path: `memcpy` background lines 0..137, draw everything, present the whole page.

---

## 3. C pseudocode

```c
/* ==== constants: see §0 ==== */
enum { SLAB=0, INSIDE=1, OUTSIDE=2, FRONT=3, TUNNEL_T=4, DWALL_T=5 };  /* table_t */

typedef struct { uint8_t vgaleft, vgaright; } colinfo_t;
extern const colinfo_t ColInfo[74];          /* trek.asm:130-203, formulas §1.3 */
extern const uint8_t   XLimits[138];         /* trek.asm:205-206 */
extern const uint8_t   ShdShapes[5][SHDH][CARW];  /* trek.asm:241-289 */
static const uint8_t   HeightTab[8] = {1,2,3,3,4,4,1,1};   /* trek.asm:291 */

uint8_t *PicDat[PHASES];      /* expanded phase blocks; PicDatSegments trek.asm:121 */
uint8_t  Background[320*200]; /* Background_Seg */
uint8_t *Page;                /* VideoPageSeg: off-screen work page */
uint8_t  Vram[320*200];       /* 0xA000 */
uint16_t Road_Dat[500][7];    /* 0x0ABC words */

int X, Y, Z, AllScreen, ShadowH;  uint8_t *CarPtr;
int Side, Half;
uint8_t CarMask[(CARH+SHDH)*CARW], LastCarMask[(CARH+SHDH)*CARW];
int CarOfs, ShdOfs, LastCarOfs, LastShdOfs, LastY;

/* ---- initvid (trek.asm:304-334): expand only ---- */
void initvid(void) { for (int p = 0; p < PHASES; p++) expand(PicDat[p]); }

void expand(uint8_t *blk) {                       /* trek.asm:1966-2001 */
    uint8_t *src = blk + *(uint16_t *)blk;        /* = memlen-disklen (trek1.c:244) */
    uint8_t *dst = blk;
    memmove(dst, src, INDEXSIZE); dst += INDEXSIZE; src += INDEXSIZE;
    for (int n = 0; n < VIRTUAL_ROWS*(COLS/2+1)*ELEMENTS; n++) {  /* 1040 records */
        *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;        /* color + base */
        for (;;) {
            uint8_t x2 = *src++; *dst++ = x2;
            if (x2 == 0xff) break;
            *dst++ = *src++;  *dst++ = 0;         /* width + inserted filler */
        }
    }
}

/* ---- span drawing (vgadrwl/vgadrwr, trek.asm:846-938) ----
   rec: expanded record. side: 0=left,1=right. color: -1 = use rec[0].
   returns pointer past the record (like the asm leaving si after 0xff). */
uint8_t *drawelm(const uint8_t *rec, int side, int color) {
    uint8_t *es = Page + LINE0*320;
    int c = (color >= 0) ? color : rec[0];
    c = side ? ColInfo[c].vgaright : ColInfo[c].vgaleft;
    int base = rec[1] | rec[2] << 8;              /* = y*320 + 160 */
    rec += 3;
    for (;;) {
        uint8_t x2 = *rec++;
        if (x2 == 0xff) return (uint8_t *)rec;
        uint8_t w = *rec++;  rec++;               /* skip filler */
        if (w) {
            if (!side) memset(es + base - x2, c, w);        /* [160-x2,160-x1) */
            else       memset(es + base + x2 - w, c, w);    /* [160+x1,160+x2) */
        }
        base += 320;
    }
}
uint8_t *skiprec(const uint8_t *rec) {            /* trek.asm:942-951 */
    rec += 3; while (*rec != 0xff) rec += 3; return (uint8_t *)(rec + 1);
}

/* ---- element constructors (trek.asm:470-723) ----
   cell: &Road_Dat[row][col]; tab: the 6 u16 index entries; ph: phase block.
   front cell = cell-COLS (nearer row); side neighbor = toward screen center. */
#define TYPE(wrd)  (((wrd) >> 8) & 0xf)
#define REC(f)     (ph + tab[f])
static uint8_t *ph; static uint16_t *cell; static const uint16_t *tab;
#define FT   TYPE(cell[-COLS])                       /* front type */
#define ST   TYPE(cell[Side ? -1 : +1])              /* side-neighbor type */

void el_plate(void) {                                /* trek.asm:470-506 */
    int C = *cell & 0xf;
    if (!C) return;
    uint8_t *si = drawelm(REC(SLAB), Side, C);                       /* top */
    if ((cell[Side ? -1 : +1] & 0xf) == 0)
         si = drawelm(si, Side, PLATESIDECOL0-1 + C);                /* side: 30+C */
    else si = skiprec(si);
    if ((cell[-COLS] & 0xf) == 0)
         drawelm(si, Side, PLATEFRONTCOL0-1 + C);                    /* front: 15+C */
}
void el_tunnel(void) {                               /* trek.asm:700-723 */
    el_plate();
    if (FT < TUNNEL) drawelm(REC(INSIDE), Side, TUNNELINSIDECOL);    /* 67 */
    uint8_t *si = REC(TUNNEL_T);
    for (int i = 0; i < 6; i++) si = drawelm(si, Side, -1);          /* roofs 68..73 */
    if (FT < TUNNEL) { si = drawelm(si, Side, -1); drawelm(si, Side, -1); } /* fronts 66 */
}
void el_wall(void) {                                 /* trek.asm:510-540 */
    el_plate();
    if (FT < WALL) drawelm(REC(FRONT), Side, -1);                    /* wallfront 62 */
    int B = (*cell >> 4) & 0xf;
    uint8_t *si = drawelm(REC(OUTSIDE), Side, B ? B : WALLROOFCOL);  /* roof */
    if (ST < WALL) drawelm(si, Side, -1);                            /* wallside 63/64 */
}
void el_arch(void) {                                 /* trek.asm:544-581 */
    el_plate();
    if (FT < WALL) drawelm(REC(INSIDE), Side, ARCHINSIDECOL);        /* 65 */
    int B = (*cell >> 4) & 0xf;
    uint8_t *si = drawelm(REC(OUTSIDE), Side, B ? B : WALLROOFCOL);
    if (ST < WALL) drawelm(si, Side, -1);
    if (FT < WALL) {
        uint8_t *s2 = skiprec(REC(FRONT));                           /* skip wallfront */
        s2 = drawelm(s2, Side, -1); drawelm(s2, Side, -1);           /* archfront1,2 */
    }
}
void el_dwall(void) {                                /* trek.asm:585-635 */
    el_plate();
    if (FT < WALL) drawelm(REC(FRONT), Side, -1);                    /* lower front */
    uint8_t *si = skiprec(REC(OUTSIDE));                             /* skip wallroof */
    if (ST < WALL) drawelm(si, Side, -1);                            /* lower side */
    int B = (*cell >> 4) & 0xf;
    si = drawelm(REC(DWALL_T), Side, B ? B : WALLROOFCOL);           /* high roof */
    if (ST < DWALL) si = drawelm(si, Side, -1); else si = skiprec(si);  /* high side */
    if (FT < DWALL) drawelm(si, Side, -1);                           /* high front */
}
void el_darch(void) {                                /* trek.asm:639-696 */
    el_plate();
    if (FT < WALL) drawelm(REC(INSIDE), Side, ARCHINSIDECOL);
    uint8_t *si = skiprec(REC(OUTSIDE));
    if (ST < WALL) drawelm(si, Side, -1);
    if (FT < WALL) {
        uint8_t *s2 = skiprec(REC(FRONT));
        s2 = drawelm(s2, Side, -1); drawelm(s2, Side, -1);
    }
    int B = (*cell >> 4) & 0xf;
    si = drawelm(REC(DWALL_T), Side, B ? B : WALLROOFCOL);
    if (ST < DWALL) si = drawelm(si, Side, -1); else si = skiprec(si);
    if (FT < DWALL) drawelm(si, Side, -1);
}
void (*const ElmDraw[6])(void) =
    { el_plate, el_tunnel, el_wall, el_arch, el_dwall, el_darch };   /* trek.asm:293 */

/* ---- carmask (trek.asm:1080-1165) ---- */
void carmask(void) {
    memset(CarMask, 0, sizeof CarMask);
    int z = Z, k = MAXZ - Z;                       /* k = abs screen y = XLimits index */
    for (int r = 0; r < CARH + SHDH; r++) {
        if (z >= MINZ && z <= MAXZ) {
            int beg = MINX, end = MAXX + 1, xl = XLimits[k];
            if (xl) {                              /* dashboard hump, abs y 129..137 */
                int a = MINX + 160 - xl;           /* = 270-xl */
                if (X >= a) beg = MINX + 160 + xl; else end = a;
            }
            int lo = beg - X;  if (lo < 0) lo = 0;
            if (lo < CARW) {
                int cnt = CARW - lo, over = X + CARW - end;
                if (over > 0) cnt -= over;
                if (cnt > 0) memset(&CarMask[r*CARW + lo], 1, cnt);
            }
        }
        z--; k++;
        if (r == CARH - 1) { z -= ShadowH - SHDZDIF; k += ShadowH - SHDZDIF; } /* shadow gap */
    }
}

/* ---- car + shadow (vgacar trek.asm:1027-1076, vgashd 1244-1302) ---- */
void vgacar(void) {
    carmask();
    CarOfs = (MAXZ - Z)*320 + X - MINX;            /* car top-left, page-absolute */
    for (int c = 0; c < CARW; c++)                 /* sprite is column-major! */
        for (int r = 0; r < CARH; r++) {
            uint8_t p = CarPtr[c*CARH + r];
            if (p && CarMask[r*CARW + c]) {
                CarMask[r*CARW + c] = 2;
                Page[CarOfs + r*320 + c] = p;
            }
        }
    /* shadow */
    unsigned n = (unsigned)ShadowH / SHDCONST;
    if (n >= SHADOWS) return;
    const uint8_t *shape = &ShdShapes[n][0][0];
    ShdOfs = (MAXZ - Z + CARH - SHDZDIF + ShadowH)*320 + X - MINX;
    for (int c = 0; c < CARW; c++)
        for (int r = 0; r < SHDH; r++) {
            int m = CARH*CARW + r*CARW + c;
            if (shape[r*CARW + c] && CarMask[m]) {
                CarMask[m] = 2;
                uint8_t p = Page[ShdOfs + r*320 + c];
                if (p == WALLROOFCOL)      p = WALLRIGHTCOL;   /* 61 -> 64 */
                else if (p >= 1 && p < 16) p += 45;            /* 1..15 -> 46..60 */
                Page[ShdOfs + r*320 + c] = p;
            }
        }
}

/* ==== video() (trek.asm:339-463) ==== */
void video(int x, int y, int z, uint8_t *carptr,
           int allscreen /* car-inside-tunnel / first frame */,
           int shadowh   /* surface-relative height */, uint8_t *page) {
    X=x; Y=y; Z=z; CarPtr=carptr; AllScreen=allscreen; ShadowH=shadowh; Page=page;

    copydif(0);                                   /* restore background into Page */

    ph = PicDat[Y & (PHASES-1)];                  /* trek.asm:381-384 */
    const uint16_t *index = (const uint16_t *)ph; /* Index[13][4][6] */
    int roadrow = (Y >> PHAS) + (ROWS - GROUNDROWS);   /* farthest row, trek.asm:373-379 */
    Half = 0;

    for (int Rows = ROWS; Rows >= 1; Rows--, roadrow--) {
      again:;
        int vrow = ROWS - Rows;                        /* 0..10 far->near */
        if (Rows == GROUNDROWS) vrow = ROWS + Half;    /* 11 or 12, trek.asm:393-399 */
        for (Side = 0; Side < 2; Side++)
            for (int c = 0; c < COLS/2 + 1; c++) {     /* 0 outermost .. 3 center */
                int col = Side ? COLS-1-c : c;         /* L:0,1,2,3  R:6,5,4,3 */
                cell = &Road_Dat[roadrow][col];
                tab  = &index[(vrow*(COLS/2+1) + c) * 6];
                ElmDraw[TYPE(*cell)]();
            }
        if (Rows == GROUNDROWS && Half == 0) {         /* trek.asm:424-432 */
            Half = 1;
            vgacar();                                  /* car between the halves */
            goto again;                                /* redraw same road row, lower half */
        }
    }

    copydif(1);                                   /* present Page -> Vram, trek.asm:440 */
    LastY = Y; LastCarOfs = CarOfs; LastShdOfs = ShdOfs;
    memcpy(LastCarMask, CarMask, sizeof CarMask); /* trek.asm:443-455 */
}
```

### 3.1 `copydif` / fragment machinery (trek.asm:1347-1908) — faithful transcription

*(A port may replace all of this with: pass 0 = `memcpy(Page, Background, 138*320)`, pass 1 = `memcpy(Vram, Page, 138*320)`.)*

```c
static uint8_t *Src, *Dst; static const uint16_t *IndexT, *LastIndexT;
static uint8_t *PhBlk, *LastPhBlk;
static int IndexOfsDifSlots, dRows, dColumns;    /* Rows, Columns of the diff loop */
static int dColor, dColor1, dHeight, dFrontColor, dFrontColor1,
           dFrontHeight, dSideColor, dSideHeight, dDiagHeight;

void copydif(int pass) {                          /* trek.asm:1347-1500 */
    Src = pass ? Page : Background;   Dst = pass ? Vram : Page;
    if (AllScreen) { memcpy(Dst, Src, 320*(MAXZ-MINZ+1)); copycar(pass); return; }
    int dy = Y - LastY;
    if (dy == 0)        { copycar(pass); return; }
    if (dy >= PHASES)   { memcpy(Dst + LINE0*320, Src + LINE0*320,
                                 320*(MAXZ-MINZ+1-LINE0));           /* lines 32..137 */
                          copycar(pass); return; }

    IndexOfsDifSlots = ((Y>>PHAS) == (LastY>>PHAS)) ? (COLS/2+1) : 0; /* trek.asm:1381-1388:
                          same base row -> old lookup one virtual row further (+48 bytes) */
    LastPhBlk = PicDat[LastY & (PHASES-1)];  LastIndexT = (uint16_t *)LastPhBlk;
    PhBlk     = PicDat[Y     & (PHASES-1)];  IndexT     = (uint16_t *)PhBlk;

    int roadrow = (Y >> PHAS) + (ROWS - GROUNDROWS);   /* trek.asm:1400-1406 */
    for (dRows = ROWS; dRows >= 2; dRows--, roadrow--)          /* rows 11..2 */
        for (dColumns = 1; dColumns <= COLS/2+1; dColumns++)    /* center -> outward */
            for (Side = 0; Side < 2; Side++) {                  /* trek.asm:1409-1479 */
                int colidx = (COLS/2+1) - dColumns;             /* 3..0 */
                int col    = Side ? COLS-1-colidx : colidx;
                uint16_t *cw = &Road_Dat[roadrow][col];
                int so = Side ? -1 : +1;                        /* toward center */
                dColor       =  *cw        & 0xf;               /* trek.asm:1422-1427 */
                dColor1      = (*cw >> 4)  & 0xf;
                dHeight      = HeightTab[TYPE(*cw) & 7];
                dFrontColor  =  cw[-COLS]       & 0xf;          /* trek.asm:1435-1445 */
                dFrontColor1 = (cw[-COLS] >> 4) & 0xf;
                dFrontHeight = HeightTab[TYPE(cw[-COLS]) & 7];
                dSideColor   =  cw[so] & 0xf;                   /* trek.asm:1447-1458 */
                dSideHeight  = HeightTab[TYPE(cw[so]) & 7];
                dDiagHeight  = HeightTab[TYPE(cw[so-COLS]) & 7];/* trek.asm:1460-1464 */
                if (pass) findfrg1(); else findfrg0();
            }
    copycar(pass);
}
```

**`findfrg0`** — background-restore pass conditions (trek.asm:1504-1562), verbatim:

```c
void findfrg0(void) {
    if (dColor == 0 && dFrontColor != 0) {                    /* trek.asm:1506-1517 */
        if (dFrontHeight == 1) copyfrag(0x0000);              /* plateroof   */
        if (dSideColor == 0)   copyfrag(0x0001);              /* plateside   */
    }
    if (dHeight < 3) {                                        /* trek.asm:1519-1530 */
        if (dFrontHeight == 3)                     copyfrag(0x0200); /* wallroof */
        if (dFrontHeight >= 3 && dSideHeight < 3)  copyfrag(0x0201); /* wallside */
    }
    if (dHeight == 1 && dFrontHeight == 2)                    /* trek.asm:1532-1547 */
        for (int i = 0; i < 6; i++) copyfrag(0x0400 + i);     /* tunnelroof1..6 */
    if (dHeight < 4 && dFrontHeight == 4) {                   /* trek.asm:1549-1558 */
        copyfrag(0x0500);                                     /* dwallroof */
        if (dSideHeight < 4) copyfrag(0x0501);                /* dwallside */
    }
}
```

**`findfrg1`** — present pass conditions (trek.asm:1566-1724), verbatim:

```c
void findfrg1(void) {
    if (dHeight == 1 && dFrontColor != dColor && dFrontHeight == 1)
        copyfrag(0x0000);                                     /* trek.asm:1568-1578 */
    if (dColor != 0 && dFrontColor == 0 &&
        (dColumns <= 2 || dDiagHeight == 1))
        copyfrag(0x8002);                                     /* platefront, full: 1580-1589 */
    if (dSideColor == 0 && dFrontColor != dColor && dDiagHeight == 1)
        copyfrag(0x0001);                                     /* plateside: 1591-1599 */
    if ((dHeight == 2 || dHeight == 3) &&
        (dFrontHeight != dHeight || dColor1 != dFrontColor1))
        copyfrag(0x0200);                                     /* wallroof: 1601-1612 */
    if (dHeight >= 2 && dFrontHeight < 3 &&
        !(dHeight == 2 && dFrontHeight == 2))
        copyfrag(0x8300);                                     /* wallfront, full: 1614-1623 */
    if ((dHeight == 2 && dFrontHeight != 2) ||
        (dHeight >= 3 && dFrontHeight < 3 && dSideHeight < 3))
        copyfrag(0x0201);                                     /* wallside: 1625-1636 */
    if (dHeight == 4 && (dFrontHeight != 4 || dColor1 != dFrontColor1))
        copyfrag(0x0500);                                     /* dwallroof: 1638-1647 */
    if (dHeight == 4 && dFrontHeight < 4)
        copyfrag(0x8502);                                     /* dwallfront, full: 1649-1654 */
    if (dHeight == 4 && dFrontHeight < 4 && dSideHeight < 4)
        copyfrag(0x0501);                                     /* dwallside: 1656-1663 */
    if (dFrontHeight > dHeight) {                             /* trek.asm:1665-1721 */
        if (dHeight <= 2) {
            if (dFrontHeight == 4) {
                copyfrag(0x0500);
                if (dSideHeight != 4 && dDiagHeight != 4) copyfrag(0x0501);
                if (dSideHeight < 3 && dDiagHeight < 3)   copyfrag(0x0201);
            } else if (dFrontHeight == 2) {
                for (int i = 0; i < 6; i++) copyfrag(0x0400 + i);
            } else {                                          /* dFrontHeight == 3 */
                copyfrag(0x0200);
                if (dSideHeight < 3 && dDiagHeight < 3) copyfrag(0x0201);
            }
        } else {                                              /* dHeight==3, front==4 */
            copyfrag(0x0500);
            if (dSideHeight != 4 && dDiagHeight != 4) copyfrag(0x0501);
        }
    }
}
```

**`copyfrag` / `copycar`** (trek.asm:1728-1908):

```c
static const uint8_t *nth_rec(const uint8_t *blk, uint16_t off, int n) {
    const uint8_t *r = blk + off;                 /* skip n records: trek.asm:1754-1761 */
    while (n--) r = skiprec(r);
    return r;
}
void copyfrag(uint16_t code) {                    /* trek.asm:1728-1852 */
    int field = (code >> 8) & 0x7f;               /* table_t entry 0..5 */
    int sub   =  code & 0xff;                     /* records to step within chain */
    int slot  = (ROWS - dRows)*(COLS/2+1) + (COLS/2+1) - dColumns; /* vrow*4+col: 1738-1746 */
    const uint8_t *orec = nth_rec(LastPhBlk,
        LastIndexT[(slot + IndexOfsDifSlots)*6 + field], sub);
    int obase = orec[1] | orec[2] << 8;           /* old element base: trek.asm:1762 */
    const uint8_t *nrec = nth_rec(PhBlk, IndexT[slot*6 + field], sub);
    int nbase = nrec[1] | nrec[2] << 8;
    const uint8_t *lp = nrec + 3;
    if (!(code & 0x8000))                         /* partial: only lines from old top down */
        while (*lp != 0xff && nbase < obase) { lp += 3; nbase += 320; } /* 1778-1787 */
    for (int base = LINE0*320 + nbase; *lp != 0xff; lp += 3, base += 320) {
        int x2 = lp[0], w = lp[1];
        if (!w) continue;
        int at = Side ? base + x2 - w : base - x2;  /* same span math as drawelm; the asm
                                                       word-aligns the copy (1812-1840) —
                                                       copying the exact span is equivalent */
        memcpy(Dst + at, Src + at, w);
    }
}

void copysprite(int ofs, const uint8_t *mask, int rows) {  /* trek.asm:1890-1908 */
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < CARW; c++)
            if (mask[r*CARW + c] == 2)
                Dst[ofs + r*320 + c] = Src[ofs + r*320 + c];
}
void copycar(int pass) {                          /* trek.asm:1856-1888 */
    copysprite(LastCarOfs, LastCarMask,            CARH);   /* erase / carry old car    */
    copysprite(LastShdOfs, LastCarMask+CARH*CARW,  SHDH);   /* erase / carry old shadow */
    if (pass) {                                   /* only when Dst is the real screen  */
        copysprite(CarOfs, CarMask,                CARH);
        copysprite(ShdOfs, CarMask+CARH*CARW,      SHDH);
    }
}
```

### 3.2 Port checklist / pitfalls

- **Draw order is load-bearing** (painter's algorithm): background → rows far-to-near, each row left half (cols 0→3) then right half (cols 6→3, center twice) → at the ground row: upper half, then car+shadow, then lower half and the remaining near rows (which may legitimately overdraw the car when the ship is low).
- The road frame is offset `LINE0*320` from the page; **the car/shadow are not** — `CarOfs`/`ShdOfs` are page-absolute (trek.asm:1039-1046 vs. 365-371). `XLimits` and the makeroad `xlim` clip are both in *absolute* screen y, and together keep road, car, and shadow out of the dashboard hump (abs y 129..137, center strip `160±xlim`) and off the dashboard proper (y ≥ 138).
- The asm **pokes colors into the shared records** before drawing (`mov [si],al`, e.g. trek.asm:477, 501, 524); the C version passes the color as a parameter instead — the records can then be `const`.
- The car sprite is **column-major 29×24**; the game's `x` is a pixel coordinate (game.c converts its 16.16 slab-unit x, 1.0 = 46 px, `BEG_X=3<<16` = middle slab).
- The VGA shadow is a **palette remap of the destination pixel** (61→64, 1..15→+45), applied after the car; it can darken the car's own bottom rows where the silhouette overlaps — that is faithful original behavior (the EGA path had an extra mask check, trek.asm:1223, which the VGA path deliberately dropped).
- `y − LastY` handling: 0 ⇒ only car copy; ≥ 8 ⇒ full copy of lines 32..137; `AllScreen` (first frame / inside tunnel) ⇒ full copy including the sky lines 0..31, which are otherwise never refreshed.
- Simplest correct port: treat every frame as `AllScreen` (full background copy + full present) and delete §3.1 entirely; keep it only if you want the original's dirty-region performance model.
