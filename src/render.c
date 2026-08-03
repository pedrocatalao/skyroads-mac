/* render.c — C port of trek.asm (the SkyRoads 3D road renderer), VGA path.
 * Follows native/docs/trek_blueprint.md; full-redraw model (AllScreen every
 * frame), so the dirty-fragment machinery (§3.1) is intentionally absent.
 */
#include "assets.h"
#include "render.h"
#include <string.h>

#include "render_tables.h"

enum { LINE0 = 32, MINX = 110, MAXX = MINX + 319,
       CARW = 29, CARH = 24, GROUNDZ = 80,
       MINZ = GROUNDZ - CARH - 36, MAXZ = MINZ - 1 + 138,
       ROWS = 11, VIRTUAL_ROWS = ROWS + 2, GROUNDROWS = 4, COLS = 7,
       PHASES = 8, PHAS = 3, ELEMENTS = 20,
       SHADOWS = 5, SHDCONST = 5, SHDH = 9, SHDZDIF = 8,
       INDEXSIZE = VIRTUAL_ROWS * (COLS / 2 + 1) * 6 * 2,
       WALLROOFCOL = 61, WALLRIGHTCOL = 64, PLATESIDECOL0 = 31,
       PLATEFRONTCOL0 = 16, ARCHINSIDECOL = 65, TUNNELINSIDECOL = 67,
       T_TUNNEL = 1, T_WALL = 2, T_DWALL = 4 };

typedef struct { uint8_t vgaleft, vgaright; } colinfo_t;
static colinfo_t ColInfo[74];

static const uint8_t XLimits9[9] = { 89, 111, 123, 133, 138, 143, 148, 153, 158 };
static uint8_t XLimits[138];

static uint8_t *Page;                 /* work page (framebuffer) */
static int X, Y, Z, ShadowH;
static const uint8_t *CarPtr;
static int Side;
static uint8_t CarMask[(CARH + SHDH) * CARW];
static int CarOfs, ShdOfs;

static const uint16_t *g_index;       /* current phase Index[13][4][6] */
static uint8_t *g_ph;                 /* current phase block */
static const duint *g_cell;           /* current Road_Dat cell */
static const uint16_t *g_tab;         /* 6 index entries for (vrow,col) */

#define RTYPE(w) (((w) >> 8) & 0xf)

/* ---- initvid: expand phase blocks + build ColInfo/XLimits ---- */
static void expand(uint8_t *blk) {                 /* trek.asm:1966-2001 */
    uint8_t *src = blk + *(duint *)blk;
    uint8_t *dst = blk;
    memmove(dst, src, INDEXSIZE);
    dst += INDEXSIZE; src += INDEXSIZE;
    for (int n = 0; n < VIRTUAL_ROWS * (COLS / 2 + 1) * ELEMENTS; n++) {
        *dst++ = *src++;                           /* color */
        *dst++ = *src++; *dst++ = *src++;          /* base u16 */
        for (;;) {
            uint8_t x2 = *src++;
            *dst++ = x2;
            if (x2 == 0xff) break;
            *dst++ = *src++;                       /* width */
            *dst++ = 0;                            /* filler */
        }
    }
}

void initvid(void) {
    for (int p = 0; p < PHASES; p++)
        expand(seg_ptr(PicDatSegments[p]));
    for (int c = 0; c < 74; c++) {                 /* §1.3 formulas */
        uint8_t l = (uint8_t)c, r = (uint8_t)c;
        if (c >= 31 && c <= 45) r = (uint8_t)(c + 15);
        else if (c == 63) r = 64;
        else if (c >= 68 && c <= 73) {
            static const uint8_t lt[6] = { 71, 70, 69, 68, 69, 70 };
            static const uint8_t rt[6] = { 70, 69, 68, 69, 70, 71 };
            l = lt[c - 68]; r = rt[c - 68];
        }
        ColInfo[c].vgaleft = l; ColInfo[c].vgaright = r;
    }
    memset(XLimits, 0, sizeof XLimits);
    memcpy(XLimits + 129, XLimits9, 9);
}

/* ---- span drawing (vgadrwl/vgadrwr) ---- */
static const uint8_t *drawelm(const uint8_t *rec, int color) {
    uint8_t *es = Page + LINE0 * 320;
    int c = (color >= 0) ? color : rec[0];
    c = Side ? ColInfo[c].vgaright : ColInfo[c].vgaleft;
    int base = rec[1] | (rec[2] << 8);
    rec += 3;
    for (;;) {
        uint8_t x2 = *rec++;
        if (x2 == 0xff) return rec;
        uint8_t w = *rec++; rec++;
        if (w) {
            if (!Side) memset(es + base - x2, c, w);
            else       memset(es + base + x2 - w, c, w);
        }
        base += 320;
    }
}

static const uint8_t *skiprec(const uint8_t *rec) {
    rec += 3;
    while (*rec != 0xff) rec += 3;
    return rec + 1;
}

#define REC(f)  (g_ph + g_tab[f])
#define FT      RTYPE(g_cell[-COLS])
#define ST      RTYPE(g_cell[Side ? -1 : +1])
enum { F_SLAB = 0, F_INSIDE = 1, F_OUTSIDE = 2, F_FRONT = 3, F_TUNNEL = 4, F_DWALL = 5 };

/* ---- element constructors (trek.asm:470-723) ---- */
static void el_plate(void) {
    int C = *g_cell & 0xf;
    if (!C) return;
    const uint8_t *si = drawelm(REC(F_SLAB), C);
    if ((g_cell[Side ? -1 : +1] & 0xf) == 0)
        si = drawelm(si, PLATESIDECOL0 - 1 + C);
    else si = skiprec(si);
    if ((g_cell[-COLS] & 0xf) == 0)
        drawelm(si, PLATEFRONTCOL0 - 1 + C);
}

static void el_tunnel(void) {
    el_plate();
    if (FT < T_TUNNEL) drawelm(REC(F_INSIDE), TUNNELINSIDECOL);
    const uint8_t *si = REC(F_TUNNEL);
    for (int i = 0; i < 6; i++) si = drawelm(si, -1);
    if (FT < T_TUNNEL) { si = drawelm(si, -1); drawelm(si, -1); }
}

static void el_wall(void) {
    el_plate();
    if (FT < T_WALL) drawelm(REC(F_FRONT), -1);
    int B = (*g_cell >> 4) & 0xf;
    const uint8_t *si = drawelm(REC(F_OUTSIDE), B ? B : WALLROOFCOL);
    if (ST < T_WALL) drawelm(si, -1);
}

static void el_arch(void) {
    el_plate();
    if (FT < T_WALL) drawelm(REC(F_INSIDE), ARCHINSIDECOL);
    int B = (*g_cell >> 4) & 0xf;
    const uint8_t *si = drawelm(REC(F_OUTSIDE), B ? B : WALLROOFCOL);
    if (ST < T_WALL) drawelm(si, -1);
    if (FT < T_WALL) {
        const uint8_t *s2 = skiprec(REC(F_FRONT));
        s2 = drawelm(s2, -1);
        drawelm(s2, -1);
    }
}

static void el_dwall(void) {
    el_plate();
    if (FT < T_WALL) drawelm(REC(F_FRONT), -1);
    const uint8_t *si = skiprec(REC(F_OUTSIDE));
    if (ST < T_WALL) drawelm(si, -1);
    int B = (*g_cell >> 4) & 0xf;
    si = drawelm(REC(F_DWALL), B ? B : WALLROOFCOL);
    if (ST < T_DWALL) si = drawelm(si, -1); else si = skiprec(si);
    if (FT < T_DWALL) drawelm(si, -1);
}

static void el_darch(void) {
    el_plate();
    if (FT < T_WALL) drawelm(REC(F_INSIDE), ARCHINSIDECOL);
    const uint8_t *si = skiprec(REC(F_OUTSIDE));
    if (ST < T_WALL) drawelm(si, -1);
    if (FT < T_WALL) {
        const uint8_t *s2 = skiprec(REC(F_FRONT));
        s2 = drawelm(s2, -1);
        drawelm(s2, -1);
    }
    int B = (*g_cell >> 4) & 0xf;
    si = drawelm(REC(F_DWALL), B ? B : WALLROOFCOL);
    if (ST < T_DWALL) si = drawelm(si, -1); else si = skiprec(si);
    if (FT < T_DWALL) drawelm(si, -1);
}

static void (*const ElmDraw[6])(void) =
    { el_plate, el_tunnel, el_wall, el_arch, el_dwall, el_darch };

/* ---- car mask (trek.asm:1080-1165) ---- */
static void carmask(void) {
    memset(CarMask, 0, sizeof CarMask);
    int z = Z, k = MAXZ - Z;
    for (int r = 0; r < CARH + SHDH; r++) {
        if (z >= MINZ && z <= MAXZ) {
            int beg = MINX, end = MAXX + 1, xl = XLimits[k];
            if (xl) {
                int a = MINX + 160 - xl;
                if (X >= a) beg = MINX + 160 + xl; else end = a;
            }
            int lo = beg - X;
            if (lo < 0) lo = 0;
            if (lo < CARW) {
                int cnt = CARW - lo, over = X + CARW - end;
                if (over > 0) cnt -= over;
                if (cnt > 0) memset(&CarMask[r * CARW + lo], 1, cnt);
            }
        }
        z--; k++;
        if (r == CARH - 1) { z -= ShadowH - SHDZDIF; k += ShadowH - SHDZDIF; }
    }
}

/* ---- car + shadow (vgacar/vgashd) ---- */
static void vgacar(void) {
    carmask();
    CarOfs = (MAXZ - Z) * 320 + X - MINX;
    for (int c = 0; c < CARW; c++)                 /* sprite is column-major */
        for (int r = 0; r < CARH; r++) {
            uint8_t p = CarPtr[c * CARH + r];
            if (p && CarMask[r * CARW + c]) {
                CarMask[r * CARW + c] = 2;
                Page[CarOfs + r * 320 + c] = p;
            }
        }
    unsigned n = (unsigned)ShadowH / SHDCONST;
    if (n >= SHADOWS) return;
    const uint8_t *shape = &ShdShapes[n][0][0];
    ShdOfs = (MAXZ - Z + CARH - SHDZDIF + ShadowH) * 320 + X - MINX;
    for (int c = 0; c < CARW; c++)
        for (int r = 0; r < SHDH; r++) {
            int m = CARH * CARW + r * CARW + c;
            if (shape[r * CARW + c] && CarMask[m]) {
                CarMask[m] = 2;
                uint8_t p = Page[ShdOfs + r * 320 + c];
                if (p == WALLROOFCOL) p = WALLRIGHTCOL;
                else if (p >= 1 && p < 16) p = (uint8_t)(p + 45);
                Page[ShdOfs + r * 320 + c] = p;
            }
        }
}

/* ---- video() (trek.asm:339-463), AllScreen model ---- */
void video(int x, int y, int z, const uint8_t *carptr,
           int car_inside_tunnel, int surface_relative_z, duint page_seg) {
    (void)car_inside_tunnel;
    X = x; Y = y; Z = z; CarPtr = carptr; ShadowH = surface_relative_z;
    Page = seg_ptr(page_seg);

    /* background restore: full copy of the viewport (sky + road area) */
    memcpy(Page, seg_ptr(Background_Seg), 320 * (MAXZ - MINZ + 1));

    g_ph = seg_ptr(PicDatSegments[Y & (PHASES - 1)]);
    g_index = (const uint16_t *)g_ph;
    int roadrow = (Y >> PHAS) + (ROWS - GROUNDROWS);
    int Half = 0;

    for (int Rows = ROWS; Rows >= 1; Rows--, roadrow--) {
    again:;
        int vrow = ROWS - Rows;
        if (Rows == GROUNDROWS) vrow = ROWS + Half;
        for (Side = 0; Side < 2; Side++)
            for (int c = 0; c < COLS / 2 + 1; c++) {
                int col = Side ? COLS - 1 - c : c;
                g_cell = &Road_Dat[roadrow][col];
                g_tab = &g_index[(vrow * (COLS / 2 + 1) + c) * 6];
                int t = RTYPE(*g_cell);
                if (t < 6) ElmDraw[t]();
            }
        if (Rows == GROUNDROWS && Half == 0) {
            Half = 1;
            vgacar();
            goto again;
        }
    }
}
