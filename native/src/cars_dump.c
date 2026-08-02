/* cars_dump.c — dump the ship sprite (cars.lzs frame 0) as 29x24 RGBA
 * for the app-icon generator.  Usage: cars_dump <datadir> <out.bin> */
#include "assets.h"
#include <stdio.h>

#define CARW 29
#define CARH 24

int main(int argc, char **argv) {
    if (argc < 3) return 1;
    set_data_dir(argv[1]);
    pic_t cars;
    open_picture("cars.lzs");
    load_palette(game_palette, CAR_COLOR);
    load_picture(&cars, CAR_COLOR);
    close_picture();
    if (SysErr) return 1;

    const uint8_t *d = seg_ptr(cars.seg);   /* column-major, 24 rows/col */
    FILE *f = fopen(argv[2], "wb");
    if (!f) return 1;
    for (int r = 0; r < CARH; r++)
        for (int c = 0; c < CARW; c++) {
            uint8_t idx = d[c * CARH + r];
            const uint8_t *p = game_palette[idx];
            uint8_t px[4] = { (uint8_t)((p[0] << 2) | (p[0] >> 4)),
                              (uint8_t)((p[1] << 2) | (p[1] >> 4)),
                              (uint8_t)((p[2] << 2) | (p[2] >> 4)),
                              (uint8_t)(idx ? 255 : 0) };
            fwrite(px, 1, 4, f);
        }
    fclose(f);
    return 0;
}
