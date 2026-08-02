/* dump each muzax.lzs song's instrument patches as hex (11 bytes each) */
#include "assets.h"
#include <stdio.h>
int main(int argc, char **argv) {
    set_data_dir(argc > 1 ? argv[1] : ".");
    for (int s = 0; s < 32; s++) {
        struct { duint offset, instruments, songlen; } hdr;
        int h = xopenr("muzax.lzs");
        xseek(h, s * (long)sizeof hdr, 0);
        xread(h, &hdr, sizeof hdr);
        if (SysErr || !hdr.songlen || hdr.instruments > 32) { xclose(h); SysErr = 0; break; }
        xseek(h, hdr.offset, 0);
        init_bit_i(h, 0, 4096, 0);
        static uint8_t buf[16000];
        if (hdr.songlen < sizeof buf) extr_lzss(buf, hdr.songlen);
        xclose(h);
        if (SysErr) { SysErr = 0; continue; }
        for (duint i = 0; i < hdr.instruments; i++) {
            printf("song%02d ins%02u ", s, i);
            for (int b = 0; b < 11; b++) printf("%02x", buf[i * 16 + b]);
            printf("\n");
        }
    }
    return 0;
}
