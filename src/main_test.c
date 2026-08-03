/* main_test.c — milestone 1: decode mainmenu.lzs (CMAP+PICT) with the
 * cracked LZSS and display it in the SDL window.  Mirrors intro.c's
 * load_palette/load_picture/draw_picture exactly. */
#include "compat.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

typedef struct { duint seg, addr, lines, len; } pic_t;

static duint g_file;

static duint load_palette(uint8_t (*pal)[3], duint offset) {
    rd_word(); rd_word();                      /* "CMAP" */
    duint colors = rd_byte();
    static uint8_t egapal[512];
    if (!SysErr) rd_mem((uint8_t *)pal + 3 * offset, 3 * colors);
    rd_mem(egapal, 2 * colors);
    return colors;
}

static void move_colors(uint8_t *p, uint32_t len, duint offs) {
    for (uint32_t i = 0; i < len; i++)
        if (p[i]) p[i] = (uint8_t)(p[i] + offs);
}

static void load_picture(pic_t *p, duint begcol) {
    rd_word();                                 /* "PI" */
    rd_mem(p, sizeof *p);                      /* "CT" -> seg (junk), addr, lines, len */
    uint32_t len = (uint32_t)p->lines * p->len;
    p->seg = xalloc(len);
    extr_lzss(seg_ptr(p->seg), (duint)len);
    move_colors(seg_ptr(p->seg), len, begcol);
}

static void draw_picture(const pic_t *p) {
    const uint8_t *src = seg_ptr(p->seg);
    uint8_t *dst = vga_mem() + p->addr;
    for (duint i = 0; i < p->lines; i++, dst += 320, src += p->len)
        memcpy(dst, src, p->len);
}

int main(int argc, char **argv) {
    set_data_dir(argc > 1 ? argv[1] : "../..");
    if (plat_init("SkyRoads (native) — asset test", 3) != 0) {
        fprintf(stderr, "SDL init failed\n");
        return 1;
    }

    const char *file = argc > 2 ? argv[2] : "mainmenu.lzs";
    duint begcol = (duint)(argc > 3 ? atoi(argv[3]) : 190);   /* MAIN_MENU_COLOR */

    g_file = xopenr(file);
    if (SysErr) { fprintf(stderr, "cannot open %s\n", file); return 1; }
    init_bit_i(g_file, 0, 4096, 0);

    uint8_t pal[256][3] = {0};
    duint colors = load_palette(pal, begcol);
    printf("palette: %u colors at %u (SysErr=%d)\n", colors, begcol, SysErr);
    set_color_regs(0, 256, pal);

    /* load every PICT chunk in the file and draw it */
    int pics = 0;
    while (!SysErr) {
        pic_t p;
        load_picture(&p, begcol);
        if (SysErr) break;
        printf("pic %d: addr=%u (x=%u,y=%u) lines=%u len=%u\n",
               pics, p.addr, p.addr % 320, p.addr / 320, p.lines, p.len);
        draw_picture(&p);
        pics++;
    }
    norm_sys_err();
    xclose(g_file);
    printf("%d picture(s) drawn\n", pics);

    if (getenv("SKY_DUMP")) {                  /* headless verification */
        FILE *f = fopen(getenv("SKY_DUMP"), "wb");
        fprintf(f, "P6\n%d %d\n255\n", VGA_W, VGA_H);
        const uint8_t *src = vga_mem();
        for (int i = 0; i < VGA_W * VGA_H; i++) {
            const uint8_t *c = g_palette[src[i]];
            uint8_t px[3] = { (uint8_t)((c[0] << 2) | (c[0] >> 4)),
                              (uint8_t)((c[1] << 2) | (c[1] >> 4)),
                              (uint8_t)((c[2] << 2) | (c[2] >> 4)) };
            fwrite(px, 1, 3, f);
        }
        fclose(f);
        plat_quit();
        return 0;
    }

    while (plat_pump()) {
        plat_tick_update();
        plat_present();
        if (plat_getch() == 27) break;
        SDL_Delay(15);
    }
    plat_quit();
    return 0;
}
