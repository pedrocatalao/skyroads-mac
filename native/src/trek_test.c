/* trek_test.c — headless validation of the trek.asm C port.
 * Loads real game data, renders frames along road 1, dumps PPMs. */
#include "assets.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t g_palette[256][3];

static void dump_ppm(const char *path, const uint8_t *fb) {
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n320 200\n255\n");
    for (int i = 0; i < 320 * 200; i++) {
        const uint8_t *c = g_palette[fb[i]];
        uint8_t px[3] = { (uint8_t)((c[0] << 2) | (c[0] >> 4)),
                          (uint8_t)((c[1] << 2) | (c[1] >> 4)),
                          (uint8_t)((c[2] << 2) | (c[2] >> 4)) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    set_data_dir(argc > 1 ? argv[1] : ".");
    int road = argc > 2 ? atoi(argv[2]) : 1;

    load_trekdat();
    check_error();
    initvid();

    start_alloc();
    load_game_data();               /* cars + dashboard + sfx */
    start_alloc();
    road_len = load_road(road);
    load_background((road - 1) / 3);
    check_error();
    printf("road %d: len=%u slabs, gravity=%u fuel=%u oxy=%u\n",
           road, road_len, gravity, fuel_distance, oxy_time);

    set_color_regs(0, 256, game_palette);

    duint page = xalloc(320 * 200);
    /* dashboard into the page bottom once (game.c does this via vga_buf) */
    uint8_t *pg = seg_ptr(page);
    memcpy(pg + 138 * 320, seg_ptr(Background_Seg) + 138 * 320, 62 * 320);

    const uint8_t *car = seg_ptr(Cars_Seg) + 44 * 720;   /* ship, level pose */
    int x = 160 + 50 - 14;                        /* trek1.c:235 */

    for (int i = 0; i < 5; i++) {
        int y = i * 12;                           /* advance 1.5 rows per shot */
        video(x, y, 100, car, 0, 20, page);
        /* also overlay dashboard rows under the viewport for the dump */
        memcpy(pg + 138 * 320, seg_ptr(Background_Seg) + 138 * 320, 62 * 320);
        char name[64];
        snprintf(name, sizeof name, "trek_%02d.ppm", i);
        dump_ppm(name, pg);
        printf("frame y=%d -> %s\n", y, name);
    }
    return 0;
}
