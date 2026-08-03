/* export_art.c — decode all 2D art from the game data into PNGs.
 * Usage: export_art <datadir> <outdir>
 *
 * Layout of the .lzs containers (lbm/pack_pic.c): tagged chunks —
 *   "CMAP" colors:u8 rgb[3*n] egapal[2*n]
 *   "PICT" addr:u16 lines:u16 len:u16 + LZSS payload (lines*len bytes)
 *   "ANIM" frames:u16, then per frame: boxes:u16 + that many PICTs
 * Pixel index 0 is transparent in overlay pictures.
 */
#include "assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint8_t pal[256][3];
static const char *outdir;

/* ---- minimal PNG writer (RGBA8) ---- */
static void png_chunk(FILE *f, const char *tag, const uint8_t *d, uint32_t n) {
    uint8_t hdr[8] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n,
                       (uint8_t)tag[0], (uint8_t)tag[1], (uint8_t)tag[2], (uint8_t)tag[3] };
    fwrite(hdr, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uint32_t crc = (uint32_t)crc32(crc32(0, hdr + 4, 4), d, n);
    uint8_t c[4] = { (uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc };
    fwrite(c, 1, 4, f);
}

static void write_png(const char *name, const uint8_t *idx, int w, int h,
                      int stride, int transparent0) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s.png", outdir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13] = { (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                         (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h,
                         8, 6, 0, 0, 0 };
    png_chunk(f, "IHDR", ihdr, 13);
    uint32_t rawlen = (uint32_t)h * (1 + 4 * w);
    uint8_t *raw = malloc(rawlen);
    for (int y = 0; y < h; y++) {
        uint8_t *row = raw + y * (1 + 4 * w);
        row[0] = 0;
        for (int x = 0; x < w; x++) {
            uint8_t i = idx[y * stride + x];
            const uint8_t *c = pal[i];
            row[1 + 4 * x + 0] = (uint8_t)((c[0] << 2) | (c[0] >> 4));
            row[1 + 4 * x + 1] = (uint8_t)((c[1] << 2) | (c[1] >> 4));
            row[1 + 4 * x + 2] = (uint8_t)((c[2] << 2) | (c[2] >> 4));
            row[1 + 4 * x + 3] = (transparent0 && !i) ? 0 : 255;
        }
    }
    uLongf zlen = compressBound(rawlen);
    uint8_t *z = malloc(zlen);
    compress2(z, &zlen, raw, rawlen, 9);
    png_chunk(f, "IDAT", z, (uint32_t)zlen);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw); free(z);
}

/* ---- chunk walking ---- */
static int read_cmap(void) {                 /* after tag; palette at offset 0 */
    duint colors = rd_byte();
    if (SysErr) return 0;
    rd_mem(pal, 3 * colors);
    static uint8_t ega[512];
    rd_mem(ega, 2 * colors);
    return 1;
}

static int load_pict(pic_t *p) {
    struct { duint addr, lines, len; } h;
    rd_mem(&h, sizeof h);
    if (SysErr) return 0;
    p->addr = h.addr; p->lines = h.lines; p->len = h.len;
    uint32_t n = (uint32_t)h.lines * h.len;
    p->seg = alloc(n + 320);
    extr_lzss(seg_ptr(p->seg), (duint)n);
    return !SysErr;
}

static void export_generic(const char *file, const char *base, int transparent0) {
    open_picture(file);
    if (SysErr) { SysErr = 0; printf("skip %s\n", file); return; }
    int pict = 0;
    for (;;) {
        duint t1 = rd_word(), t2 = rd_word();
        if (SysErr) break;
        if (t1 == 0x4d43 && t2 == 0x5041) {          /* "CMAP" */
            if (!read_cmap()) break;
        } else if (t1 == 0x4950 && t2 == 0x5443) {   /* "PICT" */
            pic_t p;
            if (!load_pict(&p)) break;
            char name[128];
            snprintf(name, sizeof name, "%s_%02d_x%u_y%u", base, pict++,
                     p.addr % 320, p.addr / 320);
            write_png(name, seg_ptr(p.seg), (int)p.len, (int)p.lines,
                      (int)p.len, transparent0);
            free_top();
        } else break;
    }
    norm_sys_err(); SysErr = 0;
    close_picture();
    printf("%-14s -> %d pictures\n", file, pict);
}

static void export_cars(void) {
    open_picture("cars.lzs");
    rd_word(); rd_word(); read_cmap();
    rd_word(); rd_word();
    pic_t p;
    load_pict(&p);
    close_picture();
    /* sprite sheet: 720 bytes per frame = 30 column-major columns of 24 */
    int frames = (int)((uint32_t)p.lines * p.len / 720);
    uint8_t frame[24 * 29];
    const uint8_t *d = seg_ptr(p.seg);
    for (int fr = 0; fr < frames; fr++) {
        for (int y = 0; y < 24; y++)
            for (int x = 0; x < 29; x++)
                frame[y * 29 + x] = d[fr * 720 + x * 24 + y];
        char name[64];
        snprintf(name, sizeof name, "car_%02d", fr);
        write_png(name, frame, 29, 24, 29, 1);
    }
    printf("cars.lzs       -> %d sprite frames (29x24)\n", frames);
}

static void export_anim(void) {
    open_picture("anim.lzs");
    if (SysErr) { SysErr = 0; return; }
    rd_word(); rd_word();
    duint frames = rd_word();
    rd_word(); rd_word(); read_cmap();
    int boxes_total = 0;
    for (duint f = 0; f < frames && !SysErr; f++) {
        duint boxes = rd_word();
        for (duint b = 0; b < boxes && !SysErr; b++) {
            rd_word(); rd_word();                    /* "PICT" */
            pic_t p;
            if (!load_pict(&p)) break;
            char name[128];
            snprintf(name, sizeof name, "anim_f%03u_b%02u_x%u_y%u",
                     f, b, p.addr % 320, p.addr / 320);
            write_png(name, seg_ptr(p.seg), (int)p.len, (int)p.lines,
                      (int)p.len, 1);
            free_top();
            boxes_total++;
        }
    }
    close_picture();
    printf("anim.lzs       -> %u frames, %d boxes\n", frames, boxes_total);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: export_art <datadir> <outdir>\n"); return 1; }
    set_data_dir(argv[1]);
    outdir = argv[2];
    start_alloc();

    export_cars();
    export_anim();
    export_generic("dashbrd.lzs",  "dashboard", 0);
    export_generic("mainmenu.lzs", "mainmenu",  1);
    export_generic("gomenu.lzs",   "gomenu",    1);
    export_generic("setmenu.lzs",  "setmenu",   1);
    export_generic("helpmenu.lzs", "help",      0);
    export_generic("intro.lzs",    "intro",     1);
    for (int w = 0; w < 10; w++) {
        char f[16], b[16];
        snprintf(f, sizeof f, "world%d.lzs", w);
        snprintf(b, sizeof b, "world%d", w);
        export_generic(f, b, 0);
    }
    return 0;
}
