/*
 * unlzs.c — brute-force the SkyRoads pack.lib LZSS bitstream format.
 *
 * Known (from tree sources):
 *   - pack_lzss(handle, buf, len, len_bits, pos_bits1, pos_bits2)
 *   - each packed payload starts with 3 bytes = (len_bits, pos_bits1, pos_bits2)
 *     [observed: trekdat.lzs block starts 04 0a 0d = (4,10,13) matching makeroad.c]
 *   - extr_lzss(dest, outlen) emits exactly `outlen` decompressed bytes, then
 *     the stream is byte-aligned for subsequent rd_word() raw reads.
 *   - trekdat.lzs = 8 blocks (PHASES): {u16 memlen, u16 disklen, payload}, EOF-terminated.
 *   - roads.lzs   = {u16 offs,u16 len}[31] header; per road: 6 bytes params,
 *                   72*3 palette, payload -> len bytes of u16 road words (rows of 7).
 *
 * Unknowns swept here: bit order, flag polarity, selector polarity,
 * min match length, distance bias, distance semantics.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    const uint8_t *buf;
    size_t len, pos;      /* byte position */
    int bitcnt;           /* bits consumed of current byte */
    int msb_first;
    int error;
} bits_t;

static void bits_init(bits_t *b, const uint8_t *buf, size_t len, int msb) {
    b->buf = buf; b->len = len; b->pos = 0; b->bitcnt = 0; b->msb_first = msb; b->error = 0;
}
static int bits_get1(bits_t *b) {
    if (b->pos >= b->len) { b->error = 1; return 0; }
    int bit;
    uint8_t byte = b->buf[b->pos];
    if (b->msb_first) bit = (byte >> (7 - b->bitcnt)) & 1;
    else              bit = (byte >> b->bitcnt) & 1;
    if (++b->bitcnt == 8) { b->bitcnt = 0; b->pos++; }
    return bit;
}
static unsigned bits_get(bits_t *b, int n) {   /* n<=16, first-read bit is MSB of value */
    unsigned v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | bits_get1(b);
    return v;
}
static unsigned bits_get_rev(bits_t *b, int n) { /* first-read bit is LSB of value */
    unsigned v = 0;
    for (int i = 0; i < n; i++) v |= (unsigned)bits_get1(b) << i;
    return v;
}
static void bits_align(bits_t *b) { if (b->bitcnt) { b->bitcnt = 0; b->pos++; } }

typedef struct {
    int msb_first;      /* bit order in bytes */
    int val_rev;        /* multi-bit values: 0 = first bit is MSB, 1 = first bit is LSB */
    int flag_lit;       /* flag bit value that means "literal" */
    int sel_short;      /* selector bit value that means "use pos_bits1 (short)" */
    int minmatch;       /* added to length field */
    int dist_bias;      /* added to distance field */
} scheme_t;

/*
 * Exact scheme recovered by disassembling extr_lzss in german/skyroads.exe
 * (pack.lib, _TEXT:0x668d; map sky2.map 0000:669B):
 *
 *   payload := len_bits pos_bits1 pos_bits2 (3 raw bytes), then MSB-first bitstream:
 *     flag 0            : short match: dist = get(pos1)+2,            len = get(len_bits)+2
 *     flag 1, 0         : long  match: dist = get(pos2)+(1<<pos1)+2,  len = get(len_bits)+2
 *     flag 1, 1         : literal    : get(8)
 *   Loop while out < end. A match token whose len+1 >= remaining terminates
 *   the stream WITHOUT copying (over-long match = end marker).
 *   All values assembled MSB-first (verified: mask table 80 C0 E0 F0 F8 FC FE FF).
 */
static int decode(const uint8_t *in, size_t inlen, size_t *consumed,
                  uint8_t *out, size_t outlen, const scheme_t *s) {
    (void)s;
    if (inlen < 3) return -1;
    int len_bits = in[0], pos1 = in[1], pos2 = in[2];
    if (len_bits < 2 || len_bits > 8 || pos1 < 4 || pos1 > 14 || pos2 < pos1 || pos2 > 16)
        return -2;
    bits_t b; bits_init(&b, in + 3, inlen - 3, 1);
    size_t op = 0;
    while (op < outlen) {
        if (bits_get1(&b)) {
            if (bits_get1(&b)) {                    /* 1,1: literal */
                unsigned c = bits_get(&b, 8);
                if (b.error) return -3;
                out[op++] = (uint8_t)c;
                continue;
            }
            /* 1,0: long match */
            unsigned dist = bits_get(&b, pos2) + (1u << pos1) + 2;
            unsigned lenf = bits_get(&b, len_bits);
            if (b.error) return -3;
            unsigned cnt = lenf + 1;                /* copies cnt+1 = lenf+2 bytes */
            if (cnt >= outlen - op) break;          /* end marker */
            if (dist > op) return -4;
            for (unsigned i = 0; i < cnt + 1; i++, op++) out[op] = out[op - dist];
        } else {
            /* 0: short match */
            unsigned dist = bits_get(&b, pos1) + 2;
            unsigned lenf = bits_get(&b, len_bits);
            if (b.error) return -3;
            unsigned cnt = lenf + 1;
            if (cnt >= outlen - op) break;          /* end marker */
            if (dist > op) return -4;
            for (unsigned i = 0; i < cnt + 1; i++, op++) out[op] = out[op - dist];
        }
        if (b.error) return -3;
    }
    bits_align(&b);
    *consumed = 3 + b.pos;
    return 0;
}

static uint8_t *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc(*len);
    fread(p, 1, *len, f); fclose(f);
    return p;
}

static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

/* Validate a scheme against trekdat.lzs: chained blocks must consume file exactly. */
static int validate_trekdat(const uint8_t *f, size_t flen, const scheme_t *s, int verbose) {
    size_t pos = 0; int blocks = 0;
    static uint8_t out[65536];
    while (pos + 4 <= flen) {
        unsigned memlen = rd16(f + pos), disklen = rd16(f + pos + 2);
        if (memlen < disklen || memlen > 65535) return -10;
        size_t consumed;
        int rc = decode(f + pos + 4, flen - pos - 4, &consumed, out, disklen, s);
        if (rc) return rc;
        pos += 4 + consumed;
        blocks++;
        if (verbose) printf("  block %d: memlen=%u disklen=%u consumed=%zu (pos now %zu/%zu)\n",
                            blocks, memlen, disklen, consumed, pos, flen);
    }
    if (pos != flen) return -11;
    return blocks;
}

/* Validate against roads.lzs: decoded words must be valid road elements. */
static int validate_roads(const uint8_t *f, size_t flen, const scheme_t *s, int verbose) {
    /* hdr: {u16 offs,u16 len}[31] */
    static uint8_t out[16384];
    int roads_ok = 0;
    for (int r = 0; r < 31; r++) {
        unsigned offs = rd16(f + r * 4), len = rd16(f + r * 4 + 2);
        if (!offs || !len) break;
        if (offs + 6 + 72 * 3 >= flen) return -20;
        const uint8_t *p = f + offs + 6 + 72 * 3;  /* skip params + palette */
        size_t consumed;
        int rc = decode(p, flen - (p - f), &consumed, out, len, s);
        if (rc) return rc;
        if (len % 14) return -21;                  /* rows are 7 u16 words */
        for (unsigned i = 0; i < len; i += 2) {
            unsigned w = rd16(out + i);
            if ((w & 0xf00) > 0x500 || (w & 0xf000)) return -22;  /* element type 0..5 */
        }
        roads_ok++;
        if (verbose && r < 3) printf("  road %d: len=%u first row: %04x %04x %04x %04x %04x %04x %04x\n",
            r, len, rd16(out), rd16(out+2), rd16(out+4), rd16(out+6), rd16(out+8), rd16(out+10), rd16(out+12));
    }
    return roads_ok;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[1024];
    size_t tlen, rlen;
    snprintf(path, sizeof path, "%s/trekdat.lzs", dir);
    uint8_t *trek = slurp(path, &tlen);
    snprintf(path, sizeof path, "%s/roads.lzs", dir);
    uint8_t *roads = slurp(path, &rlen);

    printf("trekdat.lzs: %zu bytes, roads.lzs: %zu bytes\n", tlen, rlen);
    printf("trekdat first block: memlen=%u disklen=%u params=%d,%d,%d\n",
           rd16(trek), rd16(trek + 2), trek[4], trek[5], trek[6]);

    scheme_t s = {0};
    int tb = validate_trekdat(trek, tlen, &s, 1);
    printf("trekdat validation: %d\n", tb);
    int rb = validate_roads(roads, rlen, &s, 1);
    printf("roads validation: %d\n", rb);
    int found = tb > 0 && rb > 0;
    printf(found ? "*** SCHEME CONFIRMED ***\n" : "FAILED\n");
    return !found;
}
