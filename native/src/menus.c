/* menus.c — port of intro.c's intro/main_menu/controls_menu/help_menu/gomenu.
 * English retail flow (DEMO/GERMAN/XMAS paths dropped). */
#include "assets.h"
#include "platform.h"
#include <string.h>
#include <stdlib.h>

#define TITLES     7
#define MAX_FRAMES 300
#define FRAME_TIME 2
#define LOGOSPEED  18
#define CURLEN     48

enum { KEY_ESC = 27, KEY_RET = 13, KEY_UP = 0x148, KEY_DOWN = 0x150,
       KEY_LEFT = 0x14b, KEY_RIGHT = 0x14d };

/* blocking getch on top of SDL — keeps original control flow */
static int mgetch(void) {
    for (;;) {
        if (!plat_pump()) exit(0);
        plat_tick_update();
        plat_present();
        int c = plat_getch_ext();
        if (c) return c;
        plat_sleep(10);
    }
}

static pic_t menu_pic, cur_pic[10];

static void draw_controls_cursor(duint cur, duint tcol) {
    init_mix(menu_pic.seg, SEG_VGA, tcol, 1);
    mix_picture(&cur_pic[cur]);
}

static void draw_selected_cursors(void) {
    for (duint i = 0; i < 5; i++) {
        duint c = 255;
        if (i == cfg.control_device || i == cfg.silence + 3) c = 1;
        draw_controls_cursor(i + 5, c);
    }
}

static void controls_menu(void) {                  /* intro.c:548 */
    duint cur = 0, lcur = 0;
    start_alloc();
    open_picture("setmenu.lzs");
    load_palette(menu_palette, CTRL_MENU_COLOR);
    load_picture(&menu_pic, CTRL_MENU_COLOR);
    load_palette(menu_palette, CTRL_CURSOR_COLOR);
    for (int i = 0; i < 10; i++)
        load_picture(&cur_pic[i], CTRL_CURSOR_COLOR);
    close_picture();

    draw_picture(&menu_pic);
    draw_selected_cursors();
    draw_controls_cursor(0, 1);
    fade(menu_palette, 1, FADE_TIME);

    for (;;) {
        if (cur != lcur) {
            draw_controls_cursor(lcur, 255);
            draw_controls_cursor(lcur = cur, 1);
        }
        switch (mgetch()) {
        case KEY_ESC: goto end;
        case KEY_RET:
            if (cur <= 2) cfg.control_device = cur;
            else cfg.silence = cur - 3;
            if (cfg.silence) stop_song();
            else play_song(1);
            draw_selected_cursors();
            break;
        case KEY_LEFT:  if (cur) cur--; break;
        case KEY_RIGHT: if (cur < 4) cur++; break;
        case KEY_UP:    if (cur == 3) cur = 0; if (cur == 4) cur = 1; break;
        case KEY_DOWN:  if (cur == 0) cur = 3; else if (cur < 3) cur = 4; break;
        }
    }
end:
    free_memory();
    save_cfg();
}

static duint disp_help_page(void) {                /* intro.c:596 */
    pic_t help_pic;
    start_alloc();
    load_palette(menu_palette, 200);
    load_picture(&help_pic, 200);
    draw_picture(&help_pic);
    fade(menu_palette, 1, FADE_TIME);
    free_memory();
    int c = mgetch();
    fade(menu_palette, 0, FADE_TIME);
    return c != KEY_ESC;
}

static void help_menu(void) {                      /* intro.c:610, retail: 2 pages */
    open_picture("helpmenu.lzs");
    if (disp_help_page())
        disp_help_page();
    close_picture();
}

duint main_menu(duint draw) {                      /* intro.c:622 */
    pic_t menu[3], bkgrpic, flashpic, fadepic;
    pal_t fadepal1, fadepal2;
    duint i;
    start_alloc();
    play_song(1);
    open_picture("mainmenu.lzs");
    load_palette(menu_palette, MAIN_MENU_COLOR);
    for (i = 0; i < 3; i++)
        load_picture(&menu[i], MAIN_MENU_COLOR);
    close_picture();

    open_picture("intro.lzs");
    load_palette(menu_palette, DEMO_ROAD_COLOR);
    load_picture(&bkgrpic, DEMO_ROAD_COLOR);
    start_alloc();
    load_palette(menu_palette, FLASH_COLOR);
    load_palette(menu_palette, FLASH_COLOR);
    load_picture(&flashpic, FLASH_COLOR);
    load_palette_t(&fadepal1, TEXT_FADE_COLOR);
    load_palette_t(&fadepal2, TEXT_FADE_COLOR);
    load_picture(&fadepic, TEXT_FADE_COLOR);
    close_picture();
    init_mix(bkgrpic.seg, bkgrpic.seg, 1, 0);      /* burn logo into background */
    mix_picture(&flashpic);
    if (draw) {
        draw_picture(&bkgrpic);
        fade(menu_palette, 1, FADE_TIME);
    } else {
        set_color_regs(MAIN_MENU_COLOR, 3,
                       (const uint8_t (*)[3])menu_palette[MAIN_MENU_COLOR]);
    }
    set_palette(&fadepal1);
    init_mix(0, SEG_VGA, 0, 1);
    mix_picture(&fadepic);
    fade_palette(&fadepal1, &fadepal2, 50);
    free_memory();

    i = 0;
    for (;;) {
        init_mix(bkgrpic.seg, SEG_VGA, 1, 0);
        mix_picture(&menu[i]);
        switch (mgetch()) {
        case KEY_UP:   if (i) i--; break;
        case KEY_DOWN: if (i < 2) i++; break;
        case KEY_ESC:  exit(0);
        case KEY_RET:
            fade(menu_palette, 0, FADE_TIME);
            if (!i) goto end;
            if (i == 1) { controls_menu(); fade(menu_palette, 0, FADE_TIME); }
            else help_menu();
            draw_picture(&bkgrpic);
            fade(menu_palette, 1, FADE_TIME);
        }
    }
end:
    free_memory();
    return 0;
}

static void draw_road_cursor(duint nr, duint on, duint bkseg) {  /* intro.c:701 */
    pic_t cur;
    start_alloc();
    duint seg = alloc(CURLEN * 9);
    check_error();
    uint8_t *p = seg_ptr(seg);
    memset(p, 1, CURLEN * 9);                      /* WORLD_COLOR+1 */
    for (duint i = CURLEN + 1; i < 8 * CURLEN; i += CURLEN)
        memset(p + i, 0, CURLEN - 2);
    cur.seg = seg;
    cur.lines = 9;
    cur.len = CURLEN;
    cur.addr = ((nr / 3) % 5) * 320 * 39 + 12 * 320 + 62 + (nr % 3) * 9 * 320;
    if (nr >= 15) cur.addr += 160;
    init_mix(bkseg, SEG_VGA, on ? 0 : 255, 1);
    mix_picture(&cur);
    free_memory();
}

duint gomenu(void) {                               /* intro.c:748 */
    pic_t bkgr, curpic;
    duint lcur = 1000, ret;
    start_alloc();
    play_song(1);
    open_picture("gomenu.lzs");
    load_palette(menu_palette, GO_MENU_COLOR);
    load_picture(&bkgr, GO_MENU_COLOR);
    load_palette(menu_palette, GO_MENU_CUR_COLOR);
    load_picture(&curpic, GO_MENU_CUR_COLOR);
    close_picture();
    init_mix(0, bkgr.seg, 0, 0);
    for (duint i = 0; i < WORLDS * 3; i++) {       /* completion ticks */
        duint j = i % 15;
        curpic.addr = (j / 3) * 39 * 320 + (j % 3) * 9 * 320 + 14 * 320 + 112;
        if (i >= 15) curpic.addr += 160;
        for (j = 0; j < cfg.road_completed[i] && j < 7; j++, curpic.addr += 7)
            mix_picture(&curpic);
    }
    draw_picture(&bkgr);
    fade(menu_palette, 1, FADE_TIME);
    for (;;) {
        if (Cur >= WORLDS * 3) Cur = WORLDS * 3 - 1;
        if (Cur != lcur) {
            if (lcur < 1000) draw_road_cursor(lcur, 0, bkgr.seg);
            draw_road_cursor(lcur = Cur, 1, bkgr.seg);
        }
        switch (mgetch()) {
        case KEY_LEFT:  Cur = (Cur >= 15) ? Cur - 15 : 0; break;
        case KEY_RIGHT: Cur += 15; break;
        case KEY_UP:    if (Cur) Cur--; break;
        case KEY_DOWN:  Cur++; break;
        case KEY_ESC:   ret = 1; goto end;
        case KEY_RET:   ret = 0; goto end;
        }
    }
end:
    fade(menu_palette, 0, FADE_TIME);
    free_memory();
    return ret;
}
