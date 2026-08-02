#include "platform.h"
#include <SDL.h>

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static uint32_t      rgba[VGA_W * VGA_H];
static unsigned      keymask;
static int           lastch;

volatile duint Time = 0;
static double tick_origin;

/* PIT divisor 0x19e4 = 6628 -> 1193182/6628 = 180.02 Hz int8?  The game's
 * comments say 36 volatile ticks/sec; miscasm chains 1:5.  Net: Time += 1
 * at ~36.4 Hz.  We derive Time from wall time. */
#define TICK_HZ (1193182.0 / 0x19e4 / 5.0)

double plat_now(void) {
    return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

int plat_init(const char *title, int scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) return -1;
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           VGA_W * scale, VGA_H * scale * 6 / 5,   /* 4:3 aspect (200->240) */
                           SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!win) return -1;
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(ren, VGA_W * 4, VGA_H * 4 * 6 / 5);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
                            SDL_TEXTUREACCESS_STREAMING, VGA_W, VGA_H);
    tick_origin = plat_now();
    return 0;
}

void plat_quit(void) {
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}

void plat_tick_update(void) {
    Time = (duint)((plat_now() - tick_origin) * TICK_HZ);
}

void plat_present(void) {
    const uint8_t *src = vga_mem();
    for (int i = 0; i < VGA_W * VGA_H; i++) {
        const uint8_t *c = g_palette[src[i]];
        /* 6-bit DAC -> 8-bit */
        uint32_t r = (c[0] << 2) | (c[0] >> 4);
        uint32_t g = (c[1] << 2) | (c[1] >> 4);
        uint32_t b = (c[2] << 2) | (c[2] >> 4);
        rgba[i] = 0xff000000u | (b << 16) | (g << 8) | r;
    }
    SDL_UpdateTexture(tex, NULL, rgba, VGA_W * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

static void toggle_fullscreen(void) {
    static int fs;
    fs = !fs;
    SDL_SetWindowFullscreen(win, fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

static void key_event(SDL_Keycode k, int down) {
    if (down && (k == SDLK_F11 ||
                 (k == SDLK_f && (SDL_GetModState() & KMOD_GUI)))) {
        toggle_fullscreen();
        return;
    }
    unsigned bit = 0;
    switch (k) {
    case SDLK_LEFT:  bit = K_LEFT;  break;
    case SDLK_RIGHT: bit = K_RIGHT; break;
    case SDLK_UP:    bit = K_UP;    break;
    case SDLK_DOWN:  bit = K_DOWN;  break;
    case SDLK_SPACE: bit = K_SPACE; break;
    case SDLK_ESCAPE: bit = K_ESC;  break;
    case SDLK_RETURN: bit = K_RET;  break;
    default: break;
    }
    if (down) {
        keymask |= bit;
        if (k >= 32 && k < 127) lastch = (int)k;
        else if (k == SDLK_ESCAPE) lastch = 27;
        else if (k == SDLK_RETURN) lastch = 13;
        else if (k == SDLK_UP)    lastch = 0x148;
        else if (k == SDLK_DOWN)  lastch = 0x150;
        else if (k == SDLK_LEFT)  lastch = 0x14b;
        else if (k == SDLK_RIGHT) lastch = 0x14d;
    } else keymask &= ~bit;
}

int plat_getch_ext(void) { return plat_getch(); }
void plat_sleep(int ms)  { SDL_Delay((Uint32)ms); }

int plat_pump(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return 0;
        if (e.type == SDL_KEYDOWN && !e.key.repeat) key_event(e.key.keysym.sym, 1);
        if (e.type == SDL_KEYUP) key_event(e.key.keysym.sym, 0);
    }
    return 1;
}

unsigned plat_keys(void) { return keymask; }
int plat_getch(void) { int c = lastch; lastch = 0; return c; }

const char *plat_pref_path(void) {
    static char *p;
    if (!p) p = SDL_GetPrefPath("SkyRoadsNative", "SkyRoads");
    return p ? p : "./";
}
const char *plat_base_path(void) {
    static char *p;
    if (!p) p = SDL_GetBasePath();
    return p;
}
