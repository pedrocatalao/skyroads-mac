/*
 * compat.h — portable reimplementation of the SkyRoads DOS runtime
 * (the missing def.h / str.h / pack.h / video.h + slib286.lib / pack.lib).
 *
 * Memory model: the original returns 8086 paragraph segments from xalloc and
 * builds far pointers via FP_SEG/FP_OFF.  We emulate with one flat 1 MiB
 * arena; a "segment" is (arena offset >> 4).  Segment 0xA000 is the VGA
 * framebuffer, which lives inside the arena at A000<<4 so far-pointer
 * arithmetic just works.
 */
#ifndef SKY_COMPAT_H
#define SKY_COMPAT_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  uchar;
typedef uint16_t uint16;
typedef uint32_t ulong32;
typedef int16_t  sint;
typedef int32_t  slong;

/* The DOS code's 16-bit `uint`.  Wraparound semantics matter (timers,
 * fixed-point math), so it must stay 16-bit. */
typedef uint16_t duint;

/* ---- flat memory arena («conventional memory» + VGA) ---- */
#define ARENA_SIZE   (1u << 20)               /* 1 MiB, segs 0x0000..0xFFFF */
#define SEG_VGA      0xA000u
#define VGA_W        320
#define VGA_H        200

extern uint8_t g_arena[ARENA_SIZE];

static inline uint8_t *seg_ptr(duint seg)              { return g_arena + ((uint32_t)seg << 4); }
static inline uint8_t *far_ptr(duint seg, duint off)   { return g_arena + ((uint32_t)seg << 4) + off; }
static inline uint8_t *vga_mem(void)                   { return seg_ptr(SEG_VGA); }

/* allocator (slib286 xalloc/xfree/xresize + intro.c alloc stack) */
duint xalloc(uint32_t bytes);          /* returns segment, 0 + SysErr on fail */
void  xfree(duint seg);
void  arena_reset(void);               /* fresh run */

/* ---- error model ---- */
enum { SYSERR_OK = 0, NO_MEM = 1, ERR_FILE = 2, ERR_EOF = 3 };
extern int SysErr;
void norm_sys_err(void);
void sys_err(int code);

/* ---- file I/O (handle-based, data dir aware) ---- */
void  set_data_dir(const char *dir);
const char *sky_data_dir(void);
int   xopenr(const char *name);        /* handle or 0; SysErr on fail */
int   xcreate(const char *name, int attr);
void  xclose(int h);
duint xread(int h, void *buf, duint len);
void  xwrite(int h, const void *buf, duint len);
long  xseek(int h, long off, int whence);

/* ---- pack.lib: buffered bit/byte input stream + LZSS ---- */
void  init_bit_i(int handle, int unused1, duint bufsize, int unused2);
duint rd_word(void);
duint rd_byte(void);
void  rd_mem(void *buf, duint len);
void  extr_lzss(uint8_t *dest, duint outlen);

/* ---- video services (video.h) ---- */
extern uint8_t g_palette[256][3];      /* 6-bit VGA DAC values as in data files */
extern int     g_palette_dirty;
void set_color_regs(duint start, duint count, const uint8_t (*rgb)[3]);

#endif
